﻿/*====================================================================
* 	オプションダイアログ			optdlg.c
*===================================================================*/
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include "filter.h"
#include "logo.h"
#include "optdlg.h"
#include "resource.h"
#include "editdlg.h"
#include "dlg_util.h"


#define LGD_FILTER_WRITE  "Logo data file v2 (*.lgd2)\0*.lgd2\0"\
	                      "Logo data file (*.lgd)\0*.lgd\0"\
                          "All files (*.*)\0*.*\0"
#define LGD_FILTER_READ   "Logo data file (*.lgd;*.lgd2)\0*.lgd;*.lgd2\0"\
                          "All files (*.*)\0*.*\0"
#define LGD_DEFAULT "*.lgd2"


//----------------------------
//	関数プロトタイプ
//----------------------------
static void Wm_initdialog(HWND hdlg);
static BOOL on_IDOK(HWND, WPARAM);
static BOOL on_IDCANCEL(HWND, WPARAM);
static void on_IDC_ADD(HWND hdlg);
static void on_IDC_DEL(HWND hdlg);
static void on_IDC_EXPORT(HWND hdlg);
static void on_IDC_UP(HWND hdlg);
static void on_IDC_DOWN(HWND hdlg);
static void on_IDC_EDIT(HWND hdlg);

static void AddItem(HWND hdlg, void *data);
void InsertItem(HWND hdlg, int n, void *data);
void DeleteItem(HWND list, int num);
static int  ReadLogoData(char *fname, HWND hdlg);
static void ExportLogoData(char *, void *, HWND);
static void CopyLBtoCB(HWND list, HWND combo);
static void CopyCBtoLB(HWND combo, HWND list);
static void DispLogo(HWND hdlg);
static void set_bgyc(HWND hdlg);
static void RGBtoYCbCr(PIXEL_YC *ycp, const PIXEL *rgb);
static void on_wm_sizing(HWND hdlg, RECT *rect);


//----------------------------
//	グローバル変数
//----------------------------
static const PIXEL_YC yc_black = {    0,    0,    0 };	// 黒
//const PIXEL_YC yc_white = { 4080,    0,    0 };	// 白
//const PIXEL_YC yc_red   = { 1220, -688, 2040 };	// 赤
//const PIXEL_YC yc_green = { 2393,-1351,-1707 };	// 緑
//const PIXEL_YC yc_blue  = {  467, 2040, -333 };	// 青

FILTER *optfp = NULL;
HWND    hcb_logo = NULL;	// コンボボックスのハンドル
HWND    hoptdlg = NULL;
static PIXEL  *pix = NULL;		// 表示用ピクセル
static PIXEL_YC bgyc = yc_black; // 背景色


static void **add_list = NULL;
static void **del_list = NULL;
static int  add_num = 0, del_num = 0;
static int  add_buf = 0, del_buf = 0;

extern char filter_name[];

static ITEM_SIZE defaultWindow, border;
static int TargetIDs[] ={
	IDC_LIST, IDC_PANEL,
	IDC_ADD, IDC_DEL, IDC_EXPORT, IDC_EDIT, IDC_UP, IDC_DOWN,
	IDOK, IDCANCEL,
	IDC_GROUP,
	IDC_STATIC_R, IDC_RED, IDC_SPINR,
	IDC_STATIC_G, IDC_GREEN, IDC_SPING,
	IDC_STATIC_B, IDC_BLUE, IDC_SPINB
};
static ITEM_SIZE defaultControls[_countof(TargetIDs)];

/*====================================================================
* 	OptDlgProc()		コールバックプロシージャ
*===================================================================*/
#pragma warning (push)
#pragma warning (disable: 4100) //C4100: 'lParam' : 引数は関数の本体部で 1 度も参照されません。
BOOL CALLBACK OptDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
		case WM_INITDIALOG:
			Wm_initdialog(hdlg);
			break;

		case WM_PAINT:
			DispLogo(hdlg); // 表示
			break;

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				//------------------------------------------ボタン動作
				case IDOK:
					return on_IDOK(hdlg, wParam);

				case IDCANCEL:
					return on_IDCANCEL(hdlg, wParam);

				case IDC_ADD: // 追加
					on_IDC_ADD(hdlg);
					break;

				case IDC_DEL: // 削除
					on_IDC_DEL(hdlg);
					break;

				case IDC_EXPORT: // 書き出し
					on_IDC_EXPORT(hdlg);
					break;

				case IDC_EDIT: // 編集
					on_IDC_EDIT(hdlg);
					break;

				case IDC_UP: // ↑
					on_IDC_UP(hdlg);
					break;

				case IDC_DOWN: // ↓
					on_IDC_DOWN(hdlg);
					break;

				//------------------------------------------背景色変更


				case IDC_RED:
				case IDC_GREEN:
				case IDC_BLUE:
					//bgyc = yc_red;
					set_bgyc(hdlg);
					DispLogo(hdlg);
					return TRUE;

				//-------------------------------------- リストボックス
				case IDC_LIST:
					switch (HIWORD(wParam)) {
						case LBN_SELCHANGE:
							DispLogo(hdlg);
							break;

						case LBN_DBLCLK: // ダブルクリック
							on_IDC_EDIT(hdlg); // 編集
					}
					break;
			}
			break;
		case WM_SIZING:
			on_wm_sizing(hdlg, (RECT *)lParam);
			return TRUE;
		default:
			break;
	}

	return FALSE;
}
#pragma warning (pop)
/*--------------------------------------------------------------------
* 	Wm_initdialog()	初期化
*-------------------------------------------------------------------*/
static void Wm_initdialog(HWND hdlg)
{
	hoptdlg = hdlg;
	add_list = (void **)malloc(4 * sizeof(void *));
	del_list = (void **)malloc(4 * sizeof(void *));
	add_num = del_num = 0;
	add_buf = del_buf = 4;
	pix = NULL;

	// コンボボックスからアイテムをコピー
	CopyCBtoLB(hdlg, hcb_logo);
	// RGBエディット・スピンのレンジ設定
	SendDlgItemMessage(hdlg, IDC_RED,   EM_SETLIMITTEXT, 3,   0);
	SendDlgItemMessage(hdlg, IDC_GREEN, EM_SETLIMITTEXT, 3,   0);
	SendDlgItemMessage(hdlg, IDC_BLUE,  EM_SETLIMITTEXT, 3,   0);
	SendDlgItemMessage(hdlg, IDC_SPINR, UDM_SETRANGE,    0, 255);
	SendDlgItemMessage(hdlg, IDC_SPING, UDM_SETRANGE,    0, 255);
	SendDlgItemMessage(hdlg, IDC_SPINB, UDM_SETRANGE,    0, 255);

	// 背景色に黒を選択
//	SendDlgItemMessage(hdlg, IDC_BLACK, BM_SETCHECK, (WPARAM)BST_CHECKED, 0);
	SetDlgItemInt(hdlg, IDC_RED,   0, FALSE);
	SetDlgItemInt(hdlg, IDC_GREEN, 0, FALSE);
	SetDlgItemInt(hdlg, IDC_BLUE,  0, FALSE);
	bgyc = yc_black;
	
	// 一番上のリストアイテムを選択
	//なぜか最初描画されないので、もう選択は諦める
	//SendDlgItemMessage(hdlg, IDC_LIST, LB_SETCURSEL, 0, 0);

	get_initial_dialog_size(hdlg, defaultWindow, border, defaultControls, TargetIDs);

	//ダイアログのサイズを拡大する
	MoveWindow(hdlg, defaultWindow.rect.left, defaultWindow.rect.top, 440, 520, TRUE);
	RECT changed = defaultWindow.rect;
	changed.right = changed.left + 440;
	changed.bottom = changed.top + 520;
	on_wm_sizing(hdlg, &changed);
}

/*--------------------------------------------------------------------
* 	on_wm_sizing()
*-------------------------------------------------------------------*/
static void on_wm_sizing(HWND hdlg, RECT *rect) {
	SendMessage(hdlg, WM_SETREDRAW, 0, 0);

	rect->right  = max(rect->right, rect->left + defaultWindow.w);
	rect->bottom = max(rect->bottom, rect->top + defaultWindow.h);
	int new_width = rect->right - rect->left;
	int new_height = rect->bottom - rect->top;
	
	MoveWindow(GetDlgItem(hdlg, TargetIDs[0]), defaultControls[0].rect.left, defaultControls[0].rect.top, defaultControls[0].w + (new_width - defaultWindow.w), defaultControls[0].h + (new_height - defaultWindow.h), TRUE);
	MoveWindow(GetDlgItem(hdlg, TargetIDs[1]), defaultControls[1].rect.left, defaultControls[1].rect.top + (new_height - defaultWindow.h), defaultControls[1].w, defaultControls[1].h, TRUE);
	for (int i = 2; i < 8; i++) {
		MoveControl(hdlg, TargetIDs[i], &defaultControls[i], new_width - defaultWindow.w);
	}
	for (int i = 8; i < 10; i++) {
		MoveControl(hdlg, TargetIDs[i], &defaultControls[i], new_width - defaultWindow.w, new_height - defaultWindow.h);
	}
	int group_fade_move_x = new_width / 2 - defaultControls[10].w / 2 - border.rect.left - defaultControls[10].rect.left;
	for (int i = 10; i < _countof(TargetIDs); i++) {
		MoveControl(hdlg, TargetIDs[i], &defaultControls[i], group_fade_move_x, new_height - defaultWindow.h);
	}
	SendMessage(hdlg, WM_SETREDRAW, 1, 0);
	InvalidateRect(hdlg,NULL,true);
}


/*--------------------------------------------------------------------
* 	on_IDOK()	OKボタン動作
*-------------------------------------------------------------------*/
static BOOL on_IDOK(HWND hdlg, WPARAM wParam)
{
	// リストボックスからコンボボックスへコピー
	CopyLBtoCB(hdlg, hcb_logo);

	// del_listのアイテム開放
	for (int i = 0; i < del_num; i++)
		if (del_list[i]) free(del_list[i]);

	if (add_list) free(add_list);
	if (del_list) free(del_list);

	EndDialog(hdlg,LOWORD(wParam));
	hoptdlg = NULL;

	if (pix) VirtualFree(pix, 0, MEM_RELEASE);

	return TRUE;
}

/*--------------------------------------------------------------------
* 	on_IDCANCEL()	キャンセルボタン動作
*-------------------------------------------------------------------*/
static BOOL on_IDCANCEL(HWND hdlg,WPARAM wParam)
{
	// add_listのアイテム開放
	for (int i = 0; i < add_num; i++)
		if (add_list[i]) free(add_list[i]);

	if (add_list) free(add_list);
	if (del_list) free(del_list);

	EndDialog(hdlg,LOWORD(wParam));
	hoptdlg = NULL;

	if (pix) VirtualFree(pix, 0, MEM_RELEASE);

	return TRUE;
}
/*--------------------------------------------------------------------
* 	on_IDC_ADD()	追加ボタン動作
*-------------------------------------------------------------------*/
static void on_IDC_ADD(HWND hdlg)
{
	char filename[MAX_PATH] = { 0 };
	// ロードファイル名取得
	BOOL res = optfp->exfunc->dlg_get_load_name(filename, LGD_FILTER_READ, NULL);

	if (res == FALSE) // キャンセル
		return;

	// 読み込み
	ReadLogoData(filename,hdlg);
	DispLogo(hdlg);
}

/*--------------------------------------------------------------------
* 	on_IDC_DEL()	削除ボタン動作
*-------------------------------------------------------------------*/
static void on_IDC_DEL(HWND hdlg)
{
	// 選択アイテム番号取得
	int n = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCURSEL, 0, 0);

	if (n != LB_ERR)
		DeleteItem(hdlg, n); // 削除

	// アイテム数取得
	int c = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCOUNT, 0, 0);

	// カレントセルセット
	if (c != 0) {
		if (c == n) n--;
		SendDlgItemMessage(hdlg, IDC_LIST, LB_SETCURSEL, n, 0);
		DispLogo(hdlg);
	}
}
/*--------------------------------------------------------------------
* 	on_IDC_EXPORT()	書き出しボタン動作
*-------------------------------------------------------------------*/
static void on_IDC_EXPORT(HWND hdlg)
{
	// カレントセル取得
	int n = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCURSEL, 0, 0);

	if (n == LB_ERR) {	// 選択されていない
		MessageBox(hdlg, "Not selected logo file", filter_name, MB_OK|MB_ICONERROR);
		return;
	}

	void *data = (void *)SendDlgItemMessage(hdlg, IDC_LIST, LB_GETITEMDATA, n, 0);
	if (data == NULL) {
		MessageBox(hdlg, "Logo data was broken", filter_name, MB_OK|MB_ICONERROR);
		return;
	}

	// セーブファイル名取得
	// デフォルトファイル名：ロゴ名.lgd2
	char fname[MAX_PATH];
	wsprintf(fname, "%s.lgd2", (char *)data);
	BOOL res = optfp->exfunc->dlg_get_save_name(fname, LGD_FILTER_WRITE, fname);

	if (res == FALSE) // キャンセル
		return;

	ExportLogoData(fname, data, hdlg);
}
/*--------------------------------------------------------------------
* 	on_IDC_EDIT()		編集ボタン動作
*-------------------------------------------------------------------*/
static void on_IDC_EDIT(HWND hdlg)
{
	// 選択番号取得
	int n = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCURSEL, 0, 0);

	if (n != LB_ERR)
		DialogBoxParam(optfp->dll_hinst, "EDIT_DLG", hdlg, EditDlgProc, (LPARAM)n);
	else
		MessageBox(hdlg, "Not selected logo file", filter_name, MB_OK|MB_ICONERROR);

	// アイテムを選択しなおす
	SendDlgItemMessage(hdlg, IDC_LIST, LB_SETCURSEL, n, 0);
}

/*--------------------------------------------------------------------
* 	on_IDC_UP()		↑ボタン動作
*-------------------------------------------------------------------*/
static void on_IDC_UP(HWND hdlg)
{
	// 選択位置取得
	int n = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCURSEL, 0, 0);
	if (n == 0 || n == LB_ERR) {// 一番上のときor選択されていない
		SendDlgItemMessage(hdlg, IDC_LIST, LB_SETCURSEL, n, 0);
		return;
	}

	// データ・文字列取得
	char str[LOGO_MAX_NAME] = { 0 };
	void *data = (void *)SendDlgItemMessage(hdlg, IDC_LIST, LB_GETITEMDATA, n, 0);
	SendDlgItemMessage(hdlg, IDC_LIST, LB_GETTEXT, n, (LPARAM)str);

	// 削除
	SendDlgItemMessage(hdlg, IDC_LIST, LB_DELETESTRING, n, 0);

	// 挿入
	n--;	// 一つ上
	SendDlgItemMessage(hdlg, IDC_LIST, LB_INSERTSTRING, n, (LPARAM)str);
	SendDlgItemMessage(hdlg, IDC_LIST, LB_SETITEMDATA, n, (LPARAM)data);

	SendDlgItemMessage(hdlg, IDC_LIST, LB_SETCURSEL, n, 0);
}

/*--------------------------------------------------------------------
* 	on_IDC_DOWN()		↓ボタン動作
*-------------------------------------------------------------------*/
static void on_IDC_DOWN(HWND hdlg)
{
	// 選択位置取得
	int n = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCURSEL, 0, 0);
	int count = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCOUNT, 0, 0);
	if (n == count-1 || n == LB_ERR) { // 一番下or選択されていない
		SendDlgItemMessage(hdlg, IDC_LIST, LB_SETCURSEL, n, 0);
		return;
	}

	// データ・文字列取得
	char str[LOGO_MAX_NAME] = { 0 };
	void *data = (void *)SendDlgItemMessage(hdlg, IDC_LIST, LB_GETITEMDATA, n, 0);
	SendDlgItemMessage(hdlg, IDC_LIST, LB_GETTEXT, n, (LPARAM)str);

	// 削除
	SendDlgItemMessage(hdlg, IDC_LIST, LB_DELETESTRING, n, 0);

	// 挿入
	n++;	// 一つ下
	SendDlgItemMessage(hdlg, IDC_LIST, LB_INSERTSTRING, n, (LPARAM)str);
	SendDlgItemMessage(hdlg, IDC_LIST, LB_SETITEMDATA, n, (LPARAM)data);

	SendDlgItemMessage(hdlg, IDC_LIST, LB_SETCURSEL, n, 0);
}

/*--------------------------------------------------------------------
* 	ReadLogoData()	ロゴデータを読み込む
*-------------------------------------------------------------------*/
static int ReadLogoData(char *fname, HWND hdlg)
{
	// ファイルオープン
	HANDLE hFile = CreateFile(fname, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		MessageBox(hdlg, "The logo data file not found", filter_name, MB_OK|MB_ICONERROR);
		return 0;
	}
	if (GetFileSize(hFile, NULL) <= sizeof(LOGO_HEADER)) { // サイズ確認
		CloseHandle(hFile);
		MessageBox(hdlg, "Logo data file is invalid", filter_name, MB_OK|MB_ICONERROR);
		return 0;
	}

//	SetFilePointer(hFile, 31, 0, FILE_BEGIN); // 先頭から31byteへ
//	ReadFile(hFile, &num, 1, &readed, NULL); // データ数取得
	DWORD readed = 0;
	LOGO_FILE_HEADER logo_file_header;
	ReadFile(hFile, &logo_file_header, sizeof(LOGO_FILE_HEADER), &readed, NULL);
	if (readed != sizeof(LOGO_FILE_HEADER)) {
		CloseHandle(hFile);
		MessageBox(hdlg, "To read the logo data file failed", filter_name, MB_OK|MB_ICONERROR);
		return 0;
	}

	int logo_header_ver = get_logo_file_header_ver(&logo_file_header);
	if (logo_header_ver == 0) {
		CloseHandle(hFile);
		MessageBox(hdlg, "Logo data file is invalid", filter_name, MB_OK|MB_ICONERROR);
		return 0;
	}
	
	const size_t logo_header_size = (logo_header_ver == 2) ? sizeof(LOGO_HEADER) : sizeof(LOGO_HEADER_OLD);
	int n = 0;	// 読み込みデータカウンタ
	int num = SWAP_ENDIAN(logo_file_header.logonum.l); // ファイルに含まれるデータの数

	for (int i = 0; i < num; i++) {

		// LOGO_HEADER 読み込み
		readed = 0;
		LOGO_HEADER logo_header;
		ReadFile(hFile, &logo_header, logo_header_size, &readed, NULL);
		if (readed != logo_header_size) {
			MessageBox(hdlg, "To read the logo data failed", filter_name, MB_OK|MB_ICONERROR);
			break;
		}
		if (logo_header_ver == 1) {
			convert_logo_header_v1_to_v2(&logo_header);
		}

		// 同名ロゴがあるか
		int same = SendDlgItemMessage(hdlg, IDC_LIST, LB_FINDSTRING, (WPARAM)-1, (WPARAM)logo_header.name);
		if (same != CB_ERR) {
			char message[256];
			wsprintf(message, "The same logo file already exists.\nDo you want to replace it?\n\n%s", logo_header.name);
			if (MessageBox(hdlg, message, filter_name, MB_YESNO|MB_ICONQUESTION) == IDYES) {
				// 削除
				DeleteItem(hdlg, same);
			} else { // 上書きしない
				// ファイルポインタを進める
				SetFilePointer(hFile, logo_pixel_size(&logo_header), 0, FILE_CURRENT);
				continue;
			}
		}

		// メモリ確保
		BYTE *data = (BYTE *)malloc(logo_data_size(&logo_header));
		if (data == NULL) {
			MessageBox(hdlg, "There is not enough memory", filter_name, MB_OK|MB_ICONERROR);
			break;
		}

		// LOGO_HEADERコピー
		memcpy(data, &logo_header, sizeof(LOGO_HEADER));

		LOGO_HEADER *ptr = (LOGO_HEADER *)(data + sizeof(LOGO_HEADER));

		// LOGO_PIXEL読み込み
		readed = 0;
		ReadFile(hFile, ptr, logo_pixel_size(&logo_header), &readed, NULL);

		if (logo_pixel_size(&logo_header) > (int)readed) { // 尻切れ対策
			readed -= readed % 2;
			ptr    += readed;
			memset(ptr, 0, logo_pixel_size(&logo_header) - readed);
		}

		// リストボックスに追加
		AddItem(hdlg, data);

		n++;
	}

	CloseHandle(hFile);

	if (n) {
		// リストボックスのカーソル設定
		// 読み込んだもので一番上に
		int i = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCOUNT, 0, 0) - n;
		SendDlgItemMessage(hdlg, IDC_LIST, LB_SETCURSEL, i, 0);
	}

	if (logo_header_ver == 2 && 0 == _stricmp(".lgd", PathFindExtension(fname))) {
		char new_filename[1024] = { 0 };
		strcpy_s(new_filename, fname);
		strcat_s(new_filename, "2");
		MoveFile(fname, new_filename);
	}

	return n;
}

/*--------------------------------------------------------------------
* 	ExportLogoData()	ロゴデータを書き出す
*-------------------------------------------------------------------*/
static void ExportLogoData(char *fname, void *data, HWND hdlg)
{
	BOOL error = FALSE;

	// ファイルを開く
	HANDLE hFile = CreateFile(fname, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == NULL) {
		MessageBox(hdlg, "Failed to open file", filter_name, MB_OK|MB_ICONERROR);
	}
	SetFilePointer(hFile, 0, 0, FILE_BEGIN); // 先頭へ

	// ヘッダ書き込み
	int logo_header_version = (0 == _stricmp(PathFindExtension(fname), ".lgd2")) ? 2 : 1;

	LOGO_FILE_HEADER logo_file_header = { 0 };
	strcpy_s(logo_file_header.str, (logo_header_version == 2) ? LOGO_FILE_HEADER_STR : LOGO_FILE_HEADER_STR_OLD);
	logo_file_header.logonum.l = SWAP_ENDIAN(1); // データ数は必ず１

	DWORD data_written = 0;
	WriteFile(hFile, &logo_file_header, sizeof(logo_file_header), &data_written, NULL);
	if (data_written != sizeof(logo_file_header)) { // 書き込み失敗
		MessageBox(hdlg,"Logo data save failed(1)", filter_name, MB_OK|MB_ICONERROR);
		error = TRUE;
	} else { // 成功
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
			MessageBox(hdlg, "Logo data save failed(2)", filter_name, MB_OK|MB_ICONERROR);
			error = TRUE;
		}
		if (tmp)
			free(tmp);
	}

	CloseHandle(hFile);

	if (error) // エラーがあったとき
		DeleteFile(fname); // ファイル削除
}


/*--------------------------------------------------------------------
* 	AddItem()	リストアイテムを追加
*-------------------------------------------------------------------*/
static void AddItem(HWND hdlg, void *data)
{
	int n = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCOUNT, 0, 0);

	SendDlgItemMessage(hdlg, IDC_LIST, LB_INSERTSTRING, n, (LPARAM)(char *)data);
	SendDlgItemMessage(hdlg, IDC_LIST, LB_SETITEMDATA, n, (LPARAM)data);

	if (add_buf == add_num) { // バッファがいっぱいのとき
		add_buf += 4;
		add_list = (void **)realloc(add_list, add_buf * sizeof(void*));
	}

	add_list[add_num] = data;
	add_num++;
}

/*--------------------------------------------------------------------
* 	InsertItem()	リストアイテムを挿入
*-------------------------------------------------------------------*/
void InsertItem(HWND hdlg,int n,void *data)
{
	SendDlgItemMessage(hdlg, IDC_LIST, LB_INSERTSTRING, n, (LPARAM)(char *)data);
	SendDlgItemMessage(hdlg, IDC_LIST, LB_SETITEMDATA, n, (LPARAM)data);

	if (add_buf == add_num) { // バッファがいっぱいのとき
		add_buf += 4;
		add_list = (void **)realloc(add_list, add_buf * sizeof(void*));
	}

	add_list[add_num] = data;
	add_num++;
}


/*--------------------------------------------------------------------
* 	DeleteItem()	リストアイテムを削除する
*-------------------------------------------------------------------*/
void DeleteItem(HWND hdlg,int num)
{
	if (del_buf == del_num) { // バッファがいっぱいのとき
		del_buf += 4;
		del_list = (void **)realloc(del_list, del_buf * sizeof(void *));
	}

	del_list[del_num] = (void *)SendDlgItemMessage(hdlg, IDC_LIST, LB_GETITEMDATA, num, 0);
	del_num++;

	SendDlgItemMessage(hdlg, IDC_LIST, LB_DELETESTRING, num, 0);
}

/*--------------------------------------------------------------------
* 	CopyLBtoCB()	リストボックスからコンボボックスへコピー
*-------------------------------------------------------------------*/
static void CopyLBtoCB(HWND hdlg, HWND combo)
{
	// コンボボックスクリア
	int num = SendMessage(combo, CB_GETCOUNT, 0, 0);
	for (int i = 0; i < num; i++)
		SendMessage(combo, CB_DELETESTRING, 0, 0);

	// コピー
	num = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCOUNT, 0, 0);
	for (int i = 0; i < num; i++) {
		// アイテム取得
		char str[LOGO_MAX_NAME] = { 0 };
		void *data = (void *)SendDlgItemMessage(hdlg, IDC_LIST, LB_GETITEMDATA, i, 0);
		SendDlgItemMessage(hdlg, IDC_LIST, LB_GETTEXT, i, (LPARAM)str);

		// コンボボックスにセット
		SendMessage(combo, CB_INSERTSTRING, i, (LPARAM)str);
		SendMessage(combo, CB_SETITEMDATA, i, (LPARAM)data);
	}
}

/*--------------------------------------------------------------------
* 	CopyCBtoLB()	コンボボックスからリストボックスへコピー
*-------------------------------------------------------------------*/
static void CopyCBtoLB(HWND hdlg, HWND combo)
{
	// コピー
	int num = SendMessage(combo, CB_GETCOUNT, 0, 0);
	for (int i = 0; i < num; i++) {
		// アイテム取得
		char str[LOGO_MAX_NAME] = { 0 };
		void *data = (void *)SendMessage(combo, CB_GETITEMDATA, i, 0);
		SendMessage(combo,CB_GETLBTEXT, i, (LPARAM)str);

		// リストボックスにセット
		SendDlgItemMessage(hdlg, IDC_LIST, LB_INSERTSTRING, i, (LPARAM)str);
		SendDlgItemMessage(hdlg, IDC_LIST, LB_SETITEMDATA, i, (LPARAM)data);
	}
}

/*--------------------------------------------------------------------
* 	DispLogo()	ロゴを表示
*-------------------------------------------------------------------*/
static void DispLogo(HWND hdlg)
{
	// 選択ロゴデータ取得
	int num = SendDlgItemMessage(hdlg, IDC_LIST, LB_GETCURSEL, 0, 0);
	if (num == LB_ERR) return; // 選択されていない時何もしない

	LOGO_HEADER *logo_header = (LOGO_HEADER *)SendDlgItemMessage(hdlg, IDC_LIST, LB_GETITEMDATA, num, 0);

	// BITMAPINFO設定
	BITMAPINFO  bmi = { 0 };
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = logo_header->w + (4 - logo_header->w%4); // 4の倍数
	bmi.bmiHeader.biHeight      = logo_header->h;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 24;
	bmi.bmiHeader.biCompression = BI_RGB;

	// メモリ確保
	pix = (PIXEL *)VirtualAlloc(pix, bmi.bmiHeader.biWidth * bmi.bmiHeader.biHeight * sizeof(PIXEL), MEM_COMMIT, PAGE_READWRITE);
	if (pix == NULL) { // メモリ確保失敗
		return; // 何もしない
	}

	LOGO_PIXEL *logo_pixel = (LOGO_PIXEL *)(logo_header + 1);

	// RGBデータ作成
	for (int i = 0; i < logo_header->h; i++) {
		for (int j = 0; j < logo_header->w; j++) {
			PIXEL_YC yc;
			// 輝度
			yc.y  = (bgyc.y*(LOGO_MAX_DP-logo_pixel->dp_y) + logo_pixel->y*logo_pixel->dp_y) / LOGO_MAX_DP;
			// 色差(青)
			yc.cb = (bgyc.cb*(LOGO_MAX_DP-logo_pixel->dp_cb) + logo_pixel->cb*logo_pixel->dp_cb) / LOGO_MAX_DP;
			// 色差(赤)
			yc.cr = (bgyc.cr*(LOGO_MAX_DP-logo_pixel->dp_cr) + logo_pixel->cr*logo_pixel->dp_cr) / LOGO_MAX_DP;

			// YCbCr -> RGB
			optfp->exfunc->yc2rgb(&pix[(bmi.bmiHeader.biWidth)*(logo_header->h-1-i)+j],&yc,1);

			logo_pixel++;
		}
	}

	// rect設定
	HWND panel = GetDlgItem(hdlg, IDC_PANEL);
	RECT  rec = { 0 };
	GetClientRect(panel, &rec);
	rec.left  = 2;
	rec.top   = 8;	// FONT-1
	rec.right  -= 3;
	rec.bottom -= 3;

	// デバイスコンテキスト取得
	HDC hdc = GetDC(panel);

	// 塗りつぶす
	FillRect(GetDC(panel), &rec, (HBRUSH)(COLOR_ACTIVEBORDER+1));

	
	double magnify = 1.5;	// 表示倍率
	// 表示画像の倍率
	if (rec.right-rec.left >= logo_header->w*1.5) { // 幅が収まる時
		if (rec.bottom-rec.top >= logo_header->h*1.5) // 高さも収まる時
			magnify = 1.5;
		else	// 高さのみ収まらない
			magnify = ((double)rec.bottom-rec.top)/logo_header->h;
	} else {
		if (rec.bottom-rec.top >= logo_header->h*1.5) { // 幅のみ収まらない
			magnify = ((double)rec.right-rec.left)/logo_header->w;
		} else { // 幅も高さも収まらない
			magnify = ((double)rec.bottom-rec.top)/logo_header->h; // 高さで計算
			magnify = (magnify>((double)rec.right-rec.left)/logo_header->w) ? // 倍率が小さい方
								((double)rec.right-rec.left)/logo_header->w : magnify;
		}
	}

	int x = (rec.right-rec.left - (int)(logo_header->w * magnify + 0.5) +1)/2 + rec.left; // 中央に表示するように
	int y = (rec.bottom-rec.top - (int)(logo_header->h * magnify + 0.5) +1)/2 + rec.top; // left,topを計算

	SetStretchBltMode(hdc, COLORONCOLOR);
	// 拡大表示
	StretchDIBits(hdc, x, y,
		            (int)(logo_header->w * magnify + 0.5), (int)(logo_header->h * magnify + 0.5),
		            0, 0, logo_header->w, logo_header->h, pix, &bmi, DIB_RGB_COLORS, SRCCOPY);

	ReleaseDC(panel, hdc);
}

/*--------------------------------------------------------------------
* 	set_bgyc()	プレビュー背景色を取得
*-------------------------------------------------------------------*/
static void set_bgyc(HWND hdlg)
{
	BOOL  trans;
	int   t;
	PIXEL p;

	// RGB値取得
	t = GetDlgItemInt(hdlg, IDC_BLUE, &trans, FALSE);
	if (trans==FALSE) p.b = 0;
	else if (t > 255) p.b = 255;
	else if (t < 0)   p.b = 0;
	else  p.b = (unsigned char)t;
	if (t != p.b)
		SetDlgItemInt(hdlg, IDC_BLUE, p.b, FALSE);

	t = GetDlgItemInt(hdlg, IDC_GREEN, &trans, FALSE);
	if (trans==FALSE) p.g = 0;
	else if (t > 255) p.g = 255;
	else if (t < 0)   p.g = 0;
	else  p.g = (unsigned char)t;
	if (t != p.g)
		SetDlgItemInt(hdlg, IDC_GREEN, p.g, FALSE);

	t = GetDlgItemInt(hdlg, IDC_RED, &trans, FALSE);
	if (trans==FALSE) p.r = 0;
	else if (t > 255) p.r = 255;
	else if (t < 0)   p.r = 0;
	else  p.r = (unsigned char)t;
	if (t != p.r)
		SetDlgItemInt(hdlg, IDC_RED, p.r, FALSE);

	// RGB -> YCbCr
	RGBtoYCbCr(&bgyc, &p);
}

/*--------------------------------------------------------------------
* 	RGBtoYCbCr()
*-------------------------------------------------------------------*/
static void RGBtoYCbCr(PIXEL_YC *ycp, const PIXEL *rgb)
{
	// ycp->y  =  0.2989*4096/256*rgb->r + 0.5866*4096/256*rgb->g + 0.1145*4096/256*rgb->b +0.5;
	// ycp->cb = -0.1687*4096/256*rgb->r - 0.3312*4096/256*rgb->g + 0.5000*4096/256*rgb->b +0.5;
	// ycp->cr =  0.5000*4096/256*rgb->r - 0.4183*4096/256*rgb->g - 0.0816*4096/256*rgb->b +0.5;

	ycp->y  = (( 4918*rgb->r+354)>>10)+(( 9655*rgb->g+585)>>10)+(( 1875*rgb->b+523)>>10);
	ycp->cb = ((-2775*rgb->r+240)>>10)+((-5449*rgb->g+515)>>10)+(( 8224*rgb->b+256)>>10);
	ycp->cr = (( 8224*rgb->r+256)>>10)+((-6887*rgb->g+110)>>10)+((-1337*rgb->b+646)>>10);
}

//*/
