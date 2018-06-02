﻿/*********************************************************************
* 	ロゴ解析プラグイン		ver 0.07
* 
* 2003
* 	04/06:	とりあえず完成。
* 	04/07:	バッファオーバーフロー回避
* 	04/09:	newとかmallocしたとたんに落ちるのはなぜ？
* 	04/10:	new[]に渡すパラメータがおかしかった為だった。修正
* 	04/14:	背景色計算関数の合計する時のバグを修正。
* 			メディアン化してから平均を取るようにした。
* 			回帰直線の取得アルゴリズムを少し変更。
* 	04/27:	ロゴ範囲最大値の変更（幅･高さに１ずつ余裕を持たせた）
* 	04/28:	解析結果ダイアログ表示中にAviUtlを終了できないように変更
* 			（エラーを出して落ちるバグ回避）
* 
* [β版公開]
* 
* 	05/10:	ロゴ範囲の最大値を約４倍にした。(logo.h)
* 			背景値計算を変更（ソートして真中らへんだけ平均）
* 			解析が255フレームを超えると落ちるバグ修正		(β02)
* 	06/16:	エラーメッセージを一部修正
* 			ロゴ範囲の形に色が変になるバグ修正
* 			wとhを間違えていたなんて…鬱だ
* 			背景が単一色かどうかを内部で判定するようにした
* 			選択範囲内だけを解析するようにした
* 	06/17:	昨日の修正で入れてしまったバグを修正	(β03)
* 	06/18:	最初のフレームを表示させていると解析できないバグ修正 (β03a)
* 			結果ダイアログのプレビュー背景色をRGBで指定するように変更
* 
* [正式版]
* 	07/02:	中断できるようにした。
* 			このために処理の流れを大幅に変更。
* 	07/03:	プロファイルの変更フレームで無限ループになるバグ回避（0.04)
* 	08/02:	処理順序の見直しして高速化
* 			細かな修正、解析完了時にビープを鳴らすようにした。
* 	09/22:	キャッシュの幅と高さを８の倍数にした。(SSE2対策になったかな?)
* 	09/27:	filter.hをAviUtl0.99SDKのものに差し替え。(0.05)
* 	10/14:	キャッシュ幅･高さを元に戻した。
* 	10/18:	有効フレームをマーク･ログファイル出力できるようにした。
* 			VirtualAllocをやめてmallocを使うようにした。(0.06)
* 	10/20:	VirtualAllocにもどした。
* 			exfunc->rgb2ycをやめて、自前でRGB->YCbCr
* 			有効フレームリストを保存のチェックが入っていない時動かないバグ修正
* 			ログファイルのデフォルト名をソースファイル名からつくるようにした。(0.06a)
* 	10/23:	有効フレームリスト保存ダイアログでキャンセルすると落ちるバグ修正。
* 			妙な記述があったのを修正。（何で動いてたんだろ…
* 2008
* 	01/07:	ロゴサイズ制限撤廃
* 			ロゴファイルのデータ数拡張に伴う修正
* 
*********************************************************************/
/*	TODO:
* 	・拡大ツール機能（気まぐれバロンさんのアイディア)
* 	・セーブ中は何もしないようにする
* 	・結果ダイアログで開始･終了･フェードを書き込めるようにする
* 
* 	MEMO:
* 	・背景値計算改善策①：メディアン化してから平均とか
* 	・背景値計算改善策②：ソートして真中らへんだけで計算とか
* 
* 	・背景が単色かどうかの判定：背景値の平均と、最大or最小との差が閾値以上のとき単一でないとするのはどうか
* 		→最大と最小の差が閾値以上のとき単一でないと判断のほうがよさそう。
* 
* 	・SSE2処理時に落ちる：get_ycp_filtering_cache_exがぁゃιぃ。とりあえず幅高さを８の倍数に。
* 		→だめぽ。VirtualAllocかなぁ。とりあえず試してみる。
* 		AviUtl本家の掲示板にrgb2ycが動かないとの報告が?!こいつだったのか。
* 			→自前で変換。
* 
*/
/**********************************************************************
  2015/01/31: r04 (rigaya)
              ロゴ名の文字列を255文字までに拡張。
              あわせて解析結果ダイアログのサイズを変更できるように。

  2015/02/11: r06 (rigaya)
              解析結果ダイアログの挙動が不審なのを修正。
              ロゴ解析時のメモリ使用量を大幅に削減(約1/5～1/6に)。
			  冗長なメモリ確保を削るとともに、一定量ずつzlibで圧縮。

  2015/02/12: r07 (rigaya)
              エラー発生時の対応を改善。
              出力時の拡張子によって、設定ファイルの形式を変更できるように。

**********************************************************************/
#include <windows.h>
#include <stdlib.h>
#include <stdarg.h>
#include "filter.h"
#include "logo.h"
#include "scanpix.h"
#include "resultdlg.h"
#include "abort.h"
#include "logoscan.h"

// ボタン
#define ID_SCANBTN  40010
HWND scanbtn;

static short dn_x, dn_y;	// マウスダウン座標
static short up_x, up_y;	// アップ座標
static bool  flg_mouse_down = 0;	// マウスダウンフラグ
static short _x, _y, _w, _h, _thy;

void *logodata = NULL;	// ロゴデータ（解析結果）

#define LIST_FILTER  "Frame list (*.txt)\0*.txt\0"\
                     "All files (*.*)\0*.*\0"

//----------------------------
//	プロトタイプ宣言
//----------------------------
inline void create_dlgitem(HWND hwnd, HINSTANCE hinst);
inline void SetXYWH(FILTER* fp, void* editp);
inline void SetRange(FILTER* fp, void* editp);
inline void FixXYWH(FILTER* fp, void* editp);
void ScanLogoData(FILTER* fp, void* editp);

//----------------------------
//	FILTER_DLL構造体
//----------------------------
char filter_name[] = "Logo Analysis";
char filter_info[] = "Logo Analysis Plugin v0.07+r07 by rigaya (translated by ema)";

#define track_N 5
#if track_N
TCHAR *track_name[]   = { "Pos X","Pos Y","Width","Height","Threshold" };	// トラックバーの名前
int   track_default[] = { 1, 1, 1, 1,  30 };	// トラックバーの初期値
int   track_s[]       = { 1, 1, 1, 1,   0 };	// トラックバーの下限値
int   track_e[]       = { 1, 1, 1, 1, 255 };	// トラックバーの上限値
#endif

#define check_N 2
#if check_N
TCHAR *check_name[]   = { "Mark effective frame", "Save effective frame list" };	// チェックボックス
int   check_default[] = { 0,0 };	// デフォルト
#endif

#define tLOGOX   0
#define tLOGOY   1
#define tLOGOW   2
#define tLOGOH   3
#define tTHY     4
#define cMARK    0
#define cLIST    1

// 設定ウィンドウの高さ
#define WND_Y (60+24*track_N+20*check_N)


FILTER_DLL filter = {
	FILTER_FLAG_WINDOW_SIZE |	// 設定ウィンドウのサイズを指定出来るようにします
	FILTER_FLAG_MAIN_MESSAGE |	// func_WndProc()にWM_FILTER_MAIN_???のメッセージを送るようにします
	FILTER_FLAG_EX_INFORMATION,
#ifdef WND_Y
	320,WND_Y,			// 設定ウインドウのサイズ
#else
	NULL,NULL,
#endif
	filter_name,		// フィルタの名前
	track_N,        	// トラックバーの数
#if track_N
	track_name,     	// トラックバーの名前郡
	track_default,  	// トラックバーの初期値郡
	track_s,track_e,	// トラックバーの数値の下限上限
#else
	NULL,NULL,NULL,NULL,
#endif
	check_N,      	// チェックボックスの数
#if check_N
	check_name,   	// チェックボックスの名前郡
	check_default,	// チェックボックスの初期値郡
#else
	NULL,NULL,
#endif
	func_proc,   	// フィルタ処理関数
	NULL,NULL,   	// 開始時,終了時に呼ばれる関数
	NULL,        	// 設定が変更されたときに呼ばれる関数
	func_WndProc,	// 設定ウィンドウプロシージャ
	NULL,NULL,   	// システムで使用
	NULL,NULL,     	// 拡張データ領域
	filter_info,	// フィルタ情報
	NULL,			// セーブ開始直前に呼ばれる関数
	NULL,			// セーブ終了時に呼ばれる関数
	NULL,NULL,NULL,	// システムで使用
	NULL,			// 拡張領域初期値
};

/*********************************************************************
*	DLL Export
*********************************************************************/
EXTERN_C FILTER_DLL __declspec(dllexport) * __stdcall GetFilterTable( void )
{
	return &filter;
}

/*====================================================================
*	フィルタ処理関数
*===================================================================*/
BOOL func_proc(FILTER *fp, FILTER_PROC_INFO *fpip)
{
	// 編集中以外は何もしない
	if (!fp->exfunc->is_editing(fpip->editp))
		return FALSE;

	// 範囲外
	if (fp->track[tLOGOX]==0 || fp->track[tLOGOY]==0) return FALSE;
	if (fp->track[tLOGOX]+fp->track[tLOGOW] > fpip->w) return FALSE;
	if (fp->track[tLOGOY]+fp->track[tLOGOH] > fpip->h) return FALSE;

	// 枠を書き込む(Ⅰピクセル外側に）
	// X-1,Y-1に移動
	PIXEL_YC* ptr = fpip->ycp_edit + (fp->track[tLOGOX]-1) + (fp->track[tLOGOY]-1) * fpip->max_w;
	// 横線（上）ネガポジ
	int i;
	for (i = 0; i <= fp->track[tLOGOW]+1; i++) {
		ptr->y = 4096 - ptr->y;
		ptr->cb *= -1;
		ptr->cr *= -1;
		ptr++;
	}
	ptr += fpip->max_w - i;
	// 縦線
	for (i = 1; i <= fp->track[tLOGOH]; i++) {
		// 左線
		ptr->y = 4096 - ptr->y;
		ptr->cb *= -1;
		ptr->cr *= -1;
		// 右線
		if (fp->track[tLOGOW]>=0) {
			ptr[fp->track[tLOGOW]+1].y  = 4096 - ptr[fp->track[tLOGOW]+1].y;
			ptr[fp->track[tLOGOW]+1].cb *= -1;
			ptr[fp->track[tLOGOW]+1].cr *= -1;
		}
		ptr += fpip->max_w;
	}
	// 横線（下）
	if (fp->track[tLOGOH] >= 0) {
		for (i = 0; i <= fp->track[tLOGOW]+1; i++) {
			ptr->y = 4096 - ptr->y;
			ptr->cb *= -1;
			ptr->cr *= -1;
			ptr++;
		}
	}

	return TRUE;
}

/*====================================================================
*	設定ウィンドウプロシージャ
*===================================================================*/
BOOL func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, void *editp, FILTER *fp)
{
	static bool scanning;

	switch (message) {
		case WM_FILTER_INIT: // 初期化
			create_dlgitem(hwnd,fp->dll_hinst);
			scanning = false;
			break;

		case WM_FILTER_CHANGE_PARAM:
			if (scanning) {
				FixXYWH(fp,editp);
				return TRUE;
			}
			SetRange(fp,editp);
			return FALSE;

		//--------------------------------------------マウスメッセージ
		case WM_FILTER_MAIN_MOUSE_DOWN:
			if (!fp->exfunc->is_filter_active(fp))
				return FALSE; // 無効の時何もしない
			dn_x = up_x = (short)LOWORD(lparam);
			dn_y = up_y = (short)HIWORD(lparam);
			flg_mouse_down = true;
			if (!scanning) SetXYWH(fp,editp);
			return TRUE;

		case WM_FILTER_MAIN_MOUSE_UP:
			if (!fp->exfunc->is_filter_active(fp))
				return FALSE;
			if (flg_mouse_down) { // マウスが押されている時
				up_x = (short)LOWORD(lparam);
				up_y = (short)HIWORD(lparam);
				flg_mouse_down = false;
				if (!scanning) SetXYWH(fp,editp);
				return TRUE;
			}
			break;

		case WM_FILTER_MAIN_MOUSE_MOVE:
			if (!fp->exfunc->is_filter_active(fp))
				return FALSE;
			if (flg_mouse_down) { // マウスが押されている時
				up_x = (short)LOWORD(lparam);
				up_y = (short)HIWORD(lparam);
				if (!scanning) SetXYWH(fp,editp);
				return TRUE;
			}
			break;

		//----------------------------------------------ロゴ解析ボタン
		case WM_COMMAND:
			switch (LOWORD(wparam)) {
				case ID_SCANBTN:
					scanning = true;
					ScanLogoData(fp,editp);
					scanning = false;
					break;
			}
			break;

		case WM_KEYUP: // メインウィンドウへ送る
		case WM_KEYDOWN:
		case WM_MOUSEWHEEL:
			SendMessage(GetWindow(hwnd, GW_OWNER), message, wparam, lparam);
			break;

		//----------------------------------------------独自メッセージ
		case WM_SP_DRAWFRAME:
			fp->exfunc->set_frame(editp,lparam);
			return TRUE;
	}

	return FALSE;
}

/*--------------------------------------------------------------------
*	ダイアログアイテムを作る
*-------------------------------------------------------------------*/
inline void create_dlgitem(HWND hwnd, HINSTANCE hinst)
{
#define ITEM_Y (14+24*track_N+20*check_N)
	HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	// ロゴ解析ボタン
	scanbtn = CreateWindow("BUTTON", "Logo Analysis", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON|BS_VCENTER,
									10, ITEM_Y, 295, 18, hwnd, (HMENU)ID_SCANBTN, hinst, NULL);
	SendMessage(scanbtn, WM_SETFONT, (WPARAM)font, 0);
}

/*--------------------------------------------------------------------
*	設定ウィンドウの各値を固定する
*-------------------------------------------------------------------*/
#pragma warning (push)
#pragma warning (disable:4100)
inline void FixXYWH(FILTER* fp, void* editp)
{
	fp->track[tLOGOX] = _x;
	fp->track[tLOGOY] = _y;
	fp->track[tLOGOW] = _w;
	fp->track[tLOGOH] = _h;
	fp->track[tTHY]   = _thy;

	fp->exfunc->filter_window_update(fp);	// 更新
}
#pragma warning (pop)

/*--------------------------------------------------------------------
*	設定ウィンドウの各値を設定する
*-------------------------------------------------------------------*/
inline void SetXYWH(FILTER* fp,void* editp)
{
	int h, w;

	if (!fp->exfunc->get_frame_size(editp, &w, &h))
		// 取得失敗
		return;

	// 画像内に収める
	if (dn_x < 0) dn_x = 0;
	else if (dn_x >= w) dn_x = (short)(w - 1);
	if (dn_y < 0) dn_y = 0;
	else if (dn_y >= h) dn_y = (short)(h - 1);
	if (up_x < 0) up_x = 0;
	else if (up_x >= w) up_x = (short)(w - 1);
	if (up_y < 0) up_y = 0;
	else if (up_y >= h) up_y = (short)(h - 1);


	// 設定ウィンドの各値を設定
	fp->track_e[tLOGOX] = (short)w;    // X最大値
	fp->track_e[tLOGOY] = (short)h;    // Y最大値
	fp->track[tLOGOX]   = ((dn_x < up_x) ? dn_x : up_x) + 1;
	fp->track[tLOGOY]   = ((dn_y < up_y) ? dn_y : up_y) + 1;
	fp->track_e[tLOGOW] = (short)(w - fp->track[tLOGOX]);
	fp->track_e[tLOGOH] = (short)(h - fp->track[tLOGOY]);
	fp->track[tLOGOW]   = ((dn_x < up_x) ? up_x : dn_x) - fp->track[tLOGOX];
	fp->track[tLOGOH]   = ((dn_y < up_y) ? up_y : dn_y) - fp->track[tLOGOY];

	_x = (short)fp->track[tLOGOX]; _y = (short)fp->track[tLOGOY];
	_w = (short)fp->track[tLOGOW]; _h = (short)fp->track[tLOGOH];
	_thy = (short)fp->track[tTHY];

	fp->exfunc->filter_window_update(fp);	// 更新
}

/*--------------------------------------------------------------------
*	トラックバーの最大値を設定する
*-------------------------------------------------------------------*/
inline void SetRange(FILTER* fp, void* editp)
{
	int h, w;

	if (!fp->exfunc->get_frame_size(editp, &w, &h))
		// 取得失敗
		return;

	fp->track_e[tLOGOX] = w;    // X最大値
	fp->track_e[tLOGOY] = h;    // Y最大値
	fp->track_e[tLOGOW] = w - fp->track[tLOGOX] -1; // 幅最大値
	fp->track_e[tLOGOH] = h - fp->track[tLOGOY] -1; // 高さ最大値

	if (fp->track_e[tLOGOX] < fp->track[tLOGOX])
		fp->track[tLOGOX] = fp->track_e[tLOGOX];    // 最大値にあわせる
	if (fp->track_e[tLOGOY] < fp->track[tLOGOY])
		fp->track[tLOGOY] = fp->track_e[tLOGOY];
	if (fp->track_e[tLOGOW] < fp->track[tLOGOW])
		fp->track[tLOGOW] = fp->track_e[tLOGOW];
	if (fp->track_e[tLOGOH] < fp->track[tLOGOH])
		fp->track[tLOGOH] = fp->track_e[tLOGOH];

	_x = (short)fp->track[tLOGOX]; _y = (short)fp->track[tLOGOY];
	_w = (short)fp->track[tLOGOW]; _h = (short)fp->track[tLOGOH];
	_thy = (short)fp->track[tTHY];

	fp->exfunc->filter_window_update(fp); // 更新
}

/*--------------------------------------------------------------------
*	ScanPixelを設定する
*-------------------------------------------------------------------*/
int SetScanPixel(FILTER* fp, int w, int h, int s, int e, void* editp, char* list) {
	// 範囲チェック
	if (fp->track[tLOGOW]<=0 || fp->track[tLOGOH]<=0) {
		ShowErrorMessage("Not specified area");
		return 1;
	}
	if ((fp->track[tLOGOX] + fp->track[tLOGOW] > w-1) ||
		(fp->track[tLOGOY] + fp->track[tLOGOH] > h-1)) {
		ShowErrorMessage("A portion of the area outside the screen");
		return 1;
	}

	// メモリ確保
	SCAN_PIXEL *sp = (SCAN_PIXEL *)calloc(fp->track[tLOGOW] * fp->track[tLOGOH], sizeof(sp[0]));
	if (sp == nullptr) {
		ShowErrorMessage(ERROR_MALLOC);
		return 1;
	}

	AbortDlgParam param;

	param.fp     = fp;
	param.editp  = editp;
	param.sp     = sp;
	param.s      = s;
	param.e      = e;
	param.max_w  = w;
	param.x      = fp->track[tLOGOX];
	param.y      = fp->track[tLOGOY];
	param.w      = fp->track[tLOGOW];
	param.h      = fp->track[tLOGOH];
	param.t      = fp->track[tTHY];
	param.data   = &logodata;
	param.mark   = fp->check[cMARK];
	param.list   = NULL;

	if (*list) {
		if (fopen_s(&param.list, list, "w") || param.list == NULL) {
			ShowErrorMessage("Create a list of frame file failed");
			free(sp);
			return 1;
		}
		fprintf(param.list, "<Frame List>\n");
	}

	DialogBoxParam(fp->dll_hinst, "ABORT_DLG", GetWindow(fp->hwnd, GW_OWNER), AbortDlgProc, (LPARAM)&param);

	if (param.list) {
		fclose(param.list);
		param.list = NULL;
	}
	if (sp) {
		free(sp);
	}
	return param.ret;
}

/*--------------------------------------------------------------------
*	ロゴデータを解析する
*-------------------------------------------------------------------*/
void ScanLogoData(FILTER* fp, void* editp)
{
	int ret = 0;
	EnableWindow(scanbtn, FALSE); // 解析ボタン無効化

	int w, h;       // 幅,高さ
	int start, end; // 選択開始・終了フレーム
	int frame = -1;      // 現在の表示フレーム
	char list[MAX_PATH] = "\0";	// フレームリストファイル名
	
	// 必要な情報を集める
	if (fp->exfunc->is_filter_active(fp) == FALSE) {
		// フィルタが有効でない時
		ret = 1; ShowErrorMessage("Please to enable the filter");
	} else if (0 == (frame = fp->exfunc->get_frame_n(editp))) {
		ret = 1; ShowErrorMessage("The clip was not loaded");
	} else if (0 > (frame = fp->exfunc->get_frame(editp))) {
		ret = 1; ShowErrorMessage("The clip was not loaded");
	} else if (!fp->exfunc->get_select_frame(editp, &start, &end) || end-start < 1) {
		ret = 1; ShowErrorMessage("It is not enough that the number of image");
	} else if (!fp->exfunc->get_frame_size(editp, &w, &h)) {
		ret = 1; ShowErrorMessage("Could not get the image size");
	} else {
		// ロゴ名の初期値
		GetWindowText(GetWindow(fp->hwnd, GW_OWNER), defname, LOGO_MAX_NAME-9); // タイトルバー文字列取得
		for (int i = 1; i < LOGO_MAX_NAME-9; i++)
			if (defname[i]=='.') defname[i] = '\0'; // 2文字目以降の'.'を終端にする（.aviを削除）
		wsprintf(defname, "%s %dx%d", defname, w, h); // デフォルトロゴ名作成

		// キャッシュサイズ設定
		fp->exfunc->set_ycp_filtering_cache_size(fp, w, h, 1, NULL);

		if (fp->check[cLIST]) { // リスト保存時ファイル名取得
			// ロゴ名の初期値
			GetWindowText(GetWindow(fp->hwnd, GW_OWNER), list, MAX_PATH-10);	// タイトルバー文字列取得
			for (int i = 1; list[i] && i < MAX_PATH-10; i++)
				if (list[i] == '.') list[i] = '\0'; // 2文字目以降の'.'を終端にする（拡張子を削除）
			wsprintf(list, "%s_scan.txt", list); // デフォルトロゴ名作成

			if (!fp->exfunc->dlg_get_save_name(list, LIST_FILTER, list))
				list[0] = '\0'; // キャンセル時
		}
		
		// ScanPixelを設定する+解析・ロゴデータ作成
		ret = SetScanPixel(fp, w, h, start, end, editp, list);
	}

	// 表示フレームを戻す
	if (frame >= 0)
		fp->exfunc->set_frame(editp,frame);

	// 解析結果ダイアログ
	dlgfp = fp;
	if (ret == 0)
		DialogBox(fp->dll_hinst, "RESULT_DLG", GetWindow(fp->hwnd, GW_OWNER), ResultDlgProc);

	if (logodata) {
		free(logodata);
		logodata = nullptr;
	}

	EnableWindow(scanbtn,TRUE);	// ボタンを有効に戻す
}

/*--------------------------------------------------------------------
*	ShowErrorMessage()
*-------------------------------------------------------------------*/
void ShowErrorMessage(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int len = _vscprintf(format, args) + 1; // _vscprintf doesn't count terminating '\0'
	char *buffer = (char *)malloc(len * sizeof(buffer[0]));
	if (buffer) {
		vsprintf_s(buffer, len, format, args);
		MessageBoxA(NULL, buffer, filter_name, MB_OK|MB_ICONERROR);
		free(buffer);
	}
}

//*/
