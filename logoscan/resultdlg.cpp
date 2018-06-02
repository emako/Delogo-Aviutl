﻿/*====================================================================
* 	解析結果ダイアログ			resultdlg.cpp
* 
* 2003
* 	06/18:	背景色の指定をRGBに変更
* 	10/18:	VirtualAllocをやめてみた（SSE2対策)
* 
*===================================================================*/
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include "filter.h"
#include "logo.h"
#include "resultdlg.h"
#include "resource.h"
#include "send_lgd.h"
#include "dlg_util.h"


#define LGD_FILTER  "Logo data file (*.lgd)\0*.lgd\0"\
	                "Logo data file v2 (*.lgd2)\0*.lgd2\0"\
                    "All file (*.*)\0*.*\0"
#define LGD_DEFAULT "*.lgd"


FILTER* dlgfp;	// FILTER構造体
char    defname[LOGO_MAX_NAME];	// デフォルトロゴ名

static PIXEL* pix;	// 表示用ビットマップ
static BITMAPINFO  bmi;

static UINT WM_SEND_LOGO_DATA;	// ロゴデータ送信メッセージ
static FILTER* delogofp;	// ロゴ消しフィルタFILTER構造体


extern void* logodata;	// ロゴデータ（解析結果）[filter.cpp]
extern char  filter_name[];	// フィルタ名 [filter.cpp]

static ITEM_SIZE defaultWindow, border;
static int TargetIDs[] ={
	IDC_EDIT, IDC_GROUP, IDC_SEND, IDC_SAVE, IDC_CLOSE, IDC_PANEL,
	IDC_RED, IDC_TEXT_R, IDC_SPINR,
	IDC_GREEN, IDC_TEXT_G, IDC_SPING,
	IDC_BLUE, IDC_TEXT_B, IDC_SPINB,
};
static ITEM_SIZE defaultControls[_countof(TargetIDs)];

//----------------------------
// 	プロトタイプ
//----------------------------
static void Wm_initdialog(HWND hdlg);
static void DispLogo(HWND hdlg);
static void idc_save(HWND hdlg);
static void ExportLogoData(char *fname, void *data, HWND hdlg);
static void SendLogoData(HWND hdlg);
static PIXEL_YC* get_bgyc(HWND hdlg);
static void RGBtoYCbCr(PIXEL_YC *ycp, const PIXEL *rgb);
static void on_wm_sizing(HWND hdlg, RECT *rect);


/*====================================================================
* 	ResultDlgProc()		コールバックプロシージャ
*===================================================================*/
BOOL CALLBACK ResultDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
		case WM_INITDIALOG:
			Wm_initdialog(hdlg);
			break;

		case WM_PAINT:
			DispLogo(hdlg);
			break;

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDCANCEL:
				case IDC_CLOSE:	// 閉じるボタン
					if (pix) VirtualFree(pix, 0, MEM_RELEASE);
					pix = NULL;
					EndDialog(hdlg, LOWORD(wParam));
					break;

				case IDC_SAVE:	// 保存ボタン
					idc_save(hdlg);
					break;

				case IDC_SEND:	// 送信ボタン
					SendLogoData(hdlg);
					break;

				//--------------------------------背景色設定

				case IDC_RED:
				case IDC_GREEN:
				case IDC_BLUE:
					DispLogo(hdlg);
					return TRUE;
			}
			break;
		case WM_SIZING:
			on_wm_sizing(hdlg, (RECT *)lParam);
			return TRUE;
	}

	return FALSE;
}

/*--------------------------------------------------------------------
* 	Wm_initdialog()		初期化
*-------------------------------------------------------------------*/
static void Wm_initdialog(HWND hdlg)
{
	// 最大文字数セット
	SendDlgItemMessage(hdlg, IDC_EDIT, EM_LIMITTEXT, LOGO_MAX_NAME-2, 0);
	// デフォルトロゴ名セット
	SetDlgItemText(hdlg, IDC_EDIT, defname);

	// RGBエディット・スピンのレンジ設定
	SendDlgItemMessage(hdlg, IDC_RED,    EM_SETLIMITTEXT,  3, 0);
	SendDlgItemMessage(hdlg, IDC_GREEN,  EM_SETLIMITTEXT,  3, 0);
	SendDlgItemMessage(hdlg, IDC_BLUE,   EM_SETLIMITTEXT,  3, 0);
	SendDlgItemMessage(hdlg, IDC_SPINR,  UDM_SETRANGE,  0,  255);
	SendDlgItemMessage(hdlg, IDC_SPING,  UDM_SETRANGE,  0,  255);
	SendDlgItemMessage(hdlg, IDC_SPINB,  UDM_SETRANGE,  0,  255);

	// メモリ確保
	pix = NULL;//(PIXEL*)VirtualAlloc(NULL, bmi.bmiHeader.biWidth*bmi.bmiHeader.biHeight*sizeof(PIXEL), MEM_RESERVE, PAGE_READWRITE);

	get_initial_dialog_size(hdlg, defaultWindow, border, defaultControls, TargetIDs);

	// ロゴ消しフィルタを探す
	delogofp = NULL;
	for (int n = 0; (delogofp = (FILTER*)dlgfp->exfunc->get_filterp(n)) != NULL; n++) {
		if (lstrcmp(delogofp->name, LOGO_FILTER_NAME) == 0) { // 名前で判別
			// ロゴ消しフィルタが見つかった
			WM_SEND_LOGO_DATA = RegisterWindowMessage(wm_send_logo_data);
			return;
		}
	}
	// みつからなかった時
	delogofp = NULL;
	EnableWindow(GetDlgItem(hdlg, IDC_SEND), FALSE); // 送信禁止
}

/*--------------------------------------------------------------------
* 	on_wm_sizing()
*-------------------------------------------------------------------*/
static void on_wm_sizing(HWND hdlg, RECT *rect) {
	SendMessage(hdlg, WM_SETREDRAW, 0, 0);

	rect->right  = max(rect->right, rect->left + defaultWindow.w);
	rect->bottom = rect->top + defaultWindow.h;
	int new_width = rect->right - rect->left;
			
	SetWindowPos(GetDlgItem(hdlg, IDC_EDIT), 0, 0, 0, defaultControls[0].w + (new_width - defaultWindow.w), defaultControls[0].h, SWP_NOMOVE | SWP_NOZORDER);
			
	int group_fade_move_x = new_width / 2 - defaultControls[1].w / 2 - border.rect.left - defaultControls[1].rect.left;
	for (int i = 1; i < _countof(TargetIDs); i++) {
		MoveControl(hdlg, TargetIDs[i], &defaultControls[i], group_fade_move_x);
	}

	SendMessage(hdlg, WM_SETREDRAW, 1, 0);
	InvalidateRect(hdlg,NULL,true);
}

/*--------------------------------------------------------------------
* 	DispLogo()	ロゴを表示
*-------------------------------------------------------------------*/
static void DispLogo(HWND hdlg)
{
	LOGO_HEADER *lgh = (LOGO_HEADER*)logodata;

	// 背景色取得
	PIXEL_YC *ycbg = get_bgyc(hdlg);

	// BITMAPINFO設定
	ZeroMemory(&bmi, sizeof(BITMAPINFO));
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = lgh->w + (4 - lgh->w % 4);	// ４の倍数
	bmi.bmiHeader.biHeight      = lgh->h;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 24;
	bmi.bmiHeader.biCompression = BI_RGB;

	// メモリ再確保
	pix = (PIXEL *)VirtualAlloc(pix, bmi.bmiHeader.biWidth * bmi.bmiHeader.biHeight * sizeof(PIXEL), MEM_COMMIT, PAGE_READWRITE);
	if (pix == NULL) {
		MessageBox(hdlg, "Allocate memory failure\nDispLogo()", filter_name, MB_OK|MB_ICONERROR);
		return;	// 何もしない
	}

	LOGO_PIXEL *lgp = (LOGO_PIXEL *)(lgh + 1);

	// RGBデータ作成
	for (int i = 0; i < lgh->h; i++) {
		for (int j = 0; j < lgh->w; j++) {
			PIXEL_YC yc;
			// 輝度
			yc.y  = ((long)ycbg->y  * ((long)LOGO_MAX_DP-lgp->dp_y)  + (long)lgp->y  * lgp->dp_y) / LOGO_MAX_DP;
			// 色差(青)
			yc.cb = ((long)ycbg->cb * ((long)LOGO_MAX_DP-lgp->dp_cb) + (long)lgp->cb * lgp->dp_cb) / LOGO_MAX_DP;
			// 色差(赤)
			yc.cr = ((long)ycbg->cr * ((long)LOGO_MAX_DP-lgp->dp_cr) + (long)lgp->cr * lgp->dp_cb) / LOGO_MAX_DP;

			// YCbCr -> RGB
			dlgfp->exfunc->yc2rgb(&pix[bmi.bmiHeader.biWidth * (lgh->h-1-i)+j], &yc, 1);

			lgp++;
		}
	}

	// ウィンドウハンドル取得
	HWND panel = GetDlgItem(hdlg, IDC_PANEL);

	// rect設定
	RECT  rec;
	GetClientRect(panel, &rec);
	rec.left  = 2;
	rec.top   = 8;
	rec.right  -= 3;
	rec.bottom -= 3;

	// 表示画像の倍率・位置
	double magnify;	// 表示倍率
	if (rec.right-rec.left >= lgh->w*2) { // 幅が収まる時
		if (rec.bottom-rec.top >= lgh->h*2) { // 高さも収まる時
			magnify = 2;
		} else { // 高さのみ収まらない
			magnify = ((double)rec.bottom-rec.top) / lgh->h;
		}
	} else {
		if (rec.bottom-rec.top >= lgh->h*2) { // 幅のみ収まらない
			magnify = ((double)rec.right-rec.left) / lgh->w;
		} else { // 幅も高さも収まらない
			magnify = ((double)rec.bottom-rec.top) / lgh->h; // 高さで計算
			magnify = (magnify>((double)rec.right-rec.left) / lgh->w) ? // 倍率が小さい方
								((double)rec.right-rec.left) / lgh->w : magnify;
		}
	}

	int i = (int)((rec.right-rec.left - lgh->w*magnify +1)/2) + rec.left;	// 中央に表示するように
	int j = (int)((rec.bottom-rec.top - lgh->h*magnify +1)/2) + rec.top;	// left, topを計算

	// デバイスコンテキスト取得
	HDC hdc = GetDC(panel);

	SetStretchBltMode(hdc, COLORONCOLOR);
	// 2倍に拡大表示
	StretchDIBits(hdc, i, j, (int)(lgh->w * magnify), (int)(lgh->h * magnify), 
					0, 0, lgh->w, lgh->h, pix, &bmi, DIB_RGB_COLORS, SRCCOPY);

	ReleaseDC(panel, hdc);
}

/*--------------------------------------------------------------------
* 	SaveLogoData
*-------------------------------------------------------------------*/
static void idc_save(HWND hdlg)
{
	ZeroMemory(defname, sizeof(defname));
	GetDlgItemText(hdlg, IDC_EDIT, defname, LOGO_MAX_NAME);
	if (lstrlen(defname) == 0) {
		MessageBox(hdlg, "Enter the name of logo", filter_name, MB_OK|MB_ICONERROR);
		return;
	}

	// ロゴ名設定
	defname[LOGO_MAX_NAME-1] = '\0';	// 終端
	lstrcpy((char*)logodata, defname);

	// 保存ファイル名取得
	char filename[MAX_PATH];
	wsprintf(filename, "%s.lgd", defname);
	if (!dlgfp->exfunc->dlg_get_save_name(filename, LGD_FILTER, filename))
		return;

	ExportLogoData(filename, logodata, hdlg);
}

/*--------------------------------------------------------------------
* 	ExportLogoData()	ロゴデータを書き出す
*-------------------------------------------------------------------*/
static void ExportLogoData(char *fname, void *data, HWND hdlg)
{
	// ファイルを開く
	HANDLE hFile = CreateFile(fname, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == NULL) {
		MessageBox(hdlg, "Failed to open file", filter_name, MB_OK|MB_ICONERROR);
	}
	SetFilePointer(hFile, 0,  0,  FILE_BEGIN); // 先頭へ
	
	int logo_header_version = (0 == _stricmp(PathFindExtension(fname), ".lgd2")) ? 2 : 1;

	LOGO_FILE_HEADER lfh;
	ZeroMemory(&lfh, sizeof(LOGO_FILE_HEADER));
	strcpy_s(lfh.str, (logo_header_version == 2) ? LOGO_FILE_HEADER_STR : LOGO_FILE_HEADER_STR_OLD);
	lfh.logonum.l = SWAP_ENDIAN(1);

	// ヘッダ書き込み
	int ret = 0;
	DWORD data_written = 0;
	WriteFile(hFile, &lfh, sizeof(LOGO_FILE_HEADER), &data_written, NULL);
	if (data_written != sizeof(LOGO_FILE_HEADER)) { // 書き込み失敗
		MessageBox(hdlg, "Failed to save the logo data(1)", filter_name, MB_OK|MB_ICONERROR);
		ret = 1;
	} else {
		// 成功
		// データ書き込み
		DWORD size = logo_data_size((LOGO_HEADER *)data); // データサイズ取得
		char *tmp = nullptr;
		if (logo_header_version == 1) {
			//古い形式に変換
			tmp = (char *)calloc(size, 1);
			LOGO_HEADER *header_src = (LOGO_HEADER *)data;
			LOGO_HEADER_OLD *header = (LOGO_HEADER_OLD *)tmp;
			strncpy_s(header->name, header_src->name, _TRUNCATE);
			memcpy(((char *)header) + sizeof(header->name), ((char *)header_src) + sizeof(header_src->name), sizeof(short) * 8);
			memcpy(header + 1, header_src + 1, logo_pixel_size(header_src));
			size = sizeof(LOGO_HEADER_OLD) + logo_pixel_size(header_src);
		}
		data_written = 0;
		WriteFile(hFile, (tmp) ? tmp : data, size, &data_written, NULL);
		if (data_written != size) {
			MessageBox(hdlg, "Failed to save the logo data(2)", filter_name, MB_OK|MB_ICONERROR);
			ret = 1;
		}
	}

	CloseHandle(hFile);

	if (ret) // エラーがあったとき
		DeleteFile(fname); // ファイル削除
}

/*--------------------------------------------------------------------
* 	SendLogoData()	ロゴデータを送信する
*-------------------------------------------------------------------*/
static void SendLogoData(HWND hdlg)
{
	if (!delogofp) return;	 // ロゴ消しフィルタが無い
	if (!logodata) return; // ロゴデータが無い

	// ロゴ名設定
	ZeroMemory(defname, sizeof(defname));
	GetDlgItemText(hdlg, IDC_EDIT, defname, LOGO_MAX_NAME);
	if (lstrlen(defname) == 0) {
		MessageBox(hdlg, "Enter the name of logo", filter_name, MB_OK|MB_ICONERROR);
		return;
	}

	defname[LOGO_MAX_NAME-1] = '\0'; // 終端
	lstrcpy((char*)logodata, defname);

	SendMessage(delogofp->hwnd, WM_SEND_LOGO_DATA, (WPARAM)logodata, 0);
}

/*--------------------------------------------------------------------
* 	get_bgyc()	プレビュー背景色を取得
*-------------------------------------------------------------------*/
static PIXEL_YC* get_bgyc(HWND hdlg)
{
	BOOL  trans;
	int   t;
	PIXEL p;

	// RGB値取得
	t = GetDlgItemInt(hdlg, IDC_BLUE, &trans, FALSE);
	if (trans==FALSE) p.b = 0;
	else if (t > 255) p.b = 255;
	else if (t < 0)   p.b = 0;
	else  p.b = (char)t;
	if (t != p.b)
		SetDlgItemInt(hdlg, IDC_BLUE , p.b, FALSE);

	t = GetDlgItemInt(hdlg, IDC_GREEN, &trans, FALSE);
	if (trans==FALSE) p.g = 0;
	else if (t > 255) p.g = 255;
	else if (t < 0)   p.g = 0;
	else  p.g = (char)t;
	if (t != p.g)
		SetDlgItemInt(hdlg, IDC_GREEN, p.g, FALSE);

	t = GetDlgItemInt(hdlg, IDC_RED, &trans, FALSE);
	if (trans==FALSE) p.r = 0;
	else if (t > 255) p.r = 255;
	else if (t < 0)   p.r = 0;
	else  p.r = (char)t;
	if (t != p.r)
		SetDlgItemInt(hdlg, IDC_RED  , p.r, FALSE);

	// RGB -> YCbCr
	static PIXEL_YC bgyc;
	RGBtoYCbCr(&bgyc, &p);

	return &bgyc;
}

/*--------------------------------------------------------------------
* 	RGBtoYCbCr()
*-------------------------------------------------------------------*/
static void RGBtoYCbCr(PIXEL_YC *ycp, const PIXEL *rgb)
{
	ycp->y  = (short)( 0.2989*4096/256*rgb->r + 0.5866*4096/256*rgb->g + 0.1145*4096/256*rgb->b +0.5);
	ycp->cb = (short)(-0.1687*4096/256*rgb->r - 0.3312*4096/256*rgb->g + 0.5000*4096/256*rgb->b +0.5);
	ycp->cr = (short)( 0.5000*4096/256*rgb->r - 0.4183*4096/256*rgb->g - 0.0816*4096/256*rgb->b +0.5);
}


