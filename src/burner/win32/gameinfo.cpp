#include "burner.h"
#include "png.h"

static HWND hGameInfoDlg = NULL;
static HWND hTabControl = NULL;
static HMODULE hRiched = NULL;

static TCHAR szFullName[1024];

static HBRUSH hWhiteBGBrush;

static HBITMAP hBmp = NULL;
static HBITMAP hPreview = NULL;

#define HORIZONTAL_MAX_SIZE	740
#define VERTICAL_MAX_SIZE	370

#define PNG_SIG_CHECK_BYTES 8

typedef struct tagIMAGE {
	LONG    width;
	LONG    height;
	DWORD   rowbytes;
	DWORD   imgbytes;
	BYTE**	rowptr;
	BYTE*	bmpbits;
} IMAGE;

static void img_free(IMAGE* img)
{
	free(img->rowptr);
	free(img->bmpbits);
}

static int img_alloc(IMAGE* img)
{
	img->rowbytes = ((DWORD)img->width * 24 + 31) / 32 * 4;
	img->imgbytes = img->rowbytes * img->height;
	img->rowptr = (BYTE**)malloc((size_t)img->height * sizeof(BYTE*));
	img->bmpbits = (BYTE*)malloc((size_t)img->imgbytes);

	if (img->rowptr == NULL || img->bmpbits == NULL) {
		img_free(img);
		return 0;
	}

	for (int y = 0; y < img->height; y++) {
		img->rowptr[img->height - y - 1] = img->bmpbits + y * img->rowbytes;
	}

	return 1;
}

// Resize the image to the required size using point filtering
static int img_resize(IMAGE* img, int Screenshot)
{
	IMAGE new_img;

	memset(&new_img, 0, sizeof(IMAGE));
	int xAspect, yAspect;
	
	BurnDrvGetAspect(&xAspect, &yAspect);
	
	double AspRatio = (double)xAspect / yAspect;
	
	if (!Screenshot) {
		AspRatio = (double)img->width / img->height;
	}	
	
	if (AspRatio > 1) {
		new_img.width = HORIZONTAL_MAX_SIZE;
		new_img.height = (int)((double)HORIZONTAL_MAX_SIZE / AspRatio);
		
		if (new_img.height > VERTICAL_MAX_SIZE) {
			new_img.height = VERTICAL_MAX_SIZE;
			new_img.width = (int)((double)VERTICAL_MAX_SIZE * AspRatio);
		}
	} else {
		new_img.height = VERTICAL_MAX_SIZE;
		new_img.width = (int)((double)VERTICAL_MAX_SIZE * AspRatio);
	}
	
	img_alloc(&new_img);
	
	for (int y = 0; y < new_img.height; y++) {
		int row = img->height * y / new_img.height;
		for (int x = 0; x < new_img.width; x++) {
			new_img.rowptr[y][x * 3 + 0] = img->rowptr[row][img->width * x / new_img.width * 3 + 0];
			new_img.rowptr[y][x * 3 + 1] = img->rowptr[row][img->width * x / new_img.width * 3 + 1];
			new_img.rowptr[y][x * 3 + 2] = img->rowptr[row][img->width * x / new_img.width * 3 + 2];
		}
	}

	img_free(img);

	memcpy(img, &new_img, sizeof(IMAGE));

	return 0;
}

static HBITMAP LoadPNG(HWND hDlg, FILE* fp, int Screenshot)
{
	IMAGE img;
	png_uint_32 width, height;
	int bit_depth, color_type;

	// check signature
	unsigned char pngsig[PNG_SIG_CHECK_BYTES];
	fread(pngsig, 1, PNG_SIG_CHECK_BYTES, fp);
	if (png_sig_cmp(pngsig, 0, PNG_SIG_CHECK_BYTES)) {
		return 0;
	}

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		return 0;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		return 0;
	}

	memset(&img, 0, sizeof(IMAGE));
	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, PNG_SIG_CHECK_BYTES);
	png_read_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
		return 0;
	}

	if (width > 1024 || height > 1024) {
		longjmp(png_ptr->jmpbuf, 1);
	}

	// Instruct libpng to convert the image to 24-bit RGB format
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png_ptr);
	}
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
		png_set_gray_to_rgb(png_ptr);
	}
	if (bit_depth == 16) {
		png_set_strip_16(png_ptr);
	}
	if (color_type & PNG_COLOR_MASK_ALPHA) {
		png_set_strip_alpha(png_ptr);
	}

	img.width = (LONG)width;
	img.height = (LONG)height;

	// Initialize our img structure
	if (!img_alloc(&img)) {
		longjmp(png_ptr->jmpbuf, 1);
	}

	// If bad things happen in libpng we need to do img_free(&img) as well
	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
		img_free(&img);
		return 0;
	}

	// Read the .PNG image
	png_set_bgr(png_ptr);
	png_read_update_info(png_ptr, info_ptr);
	png_read_image(png_ptr, img.rowptr);
	png_read_end(png_ptr, (png_infop)NULL);
	png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);

	if (img_resize(&img, Screenshot)) {
		img_free(&img);
		return 0;
	}

	// Create a bitmap for the image
	BITMAPINFO* bi = (BITMAPINFO*)LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD));
	if (bi == NULL) {
		img_free(&img);
		return 0;
	}

	bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi->bmiHeader.biWidth = img.width;
	bi->bmiHeader.biHeight = img.height;
	bi->bmiHeader.biPlanes = 1;
	bi->bmiHeader.biBitCount = 24;
	bi->bmiHeader.biCompression = BI_RGB;
	bi->bmiHeader.biSizeImage = img.imgbytes;
	bi->bmiHeader.biXPelsPerMeter = 0;
	bi->bmiHeader.biYPelsPerMeter = 0;
	bi->bmiHeader.biClrUsed = 0;
	bi->bmiHeader.biClrImportant = 0;

	HDC hDC = GetDC(hDlg);
	BYTE* pBits = NULL;
	HBITMAP hNewBmp = CreateDIBSection(hDC, (BITMAPINFO*)bi, DIB_RGB_COLORS, (void**)&pBits, NULL, 0);
	if (pBits) {
		memcpy(pBits, img.bmpbits, img.imgbytes);
	}
	ReleaseDC(hDlg, hDC);
	LocalFree(bi);
	img_free(&img);

	return hNewBmp;
}

#if defined (_UNICODE)
static int UpdatePreview(wchar_t *szPreviewDir)
#else
static int UpdatePreview(char *szPreviewDir)
#endif
{
	TCHAR szFileName[MAX_PATH];
	FILE *fp;
	HBITMAP hNewImage = NULL;
	
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_LIST1), SW_HIDE);
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_MESSAGE_EDIT_ENG), SW_HIDE);
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_SCREENSHOT_H), SW_SHOW);
	UpdateWindow(hGameInfoDlg);
		
	if (hBmp) {
		DeleteObject((HGDIOBJ)hBmp);
		hBmp = NULL;
	}
	
	_stprintf(szFileName, _T("%s%s.png"), szPreviewDir, BurnDrvGetText(DRV_NAME));
	
	fp = _tfopen(szFileName, _T("rb"));
	if (!fp && BurnDrvGetText(DRV_PARENT)) {
		_stprintf(szFileName, _T("%s%s.png"), szPreviewDir, BurnDrvGetText(DRV_PARENT));
		
		fp = _tfopen(szFileName, _T("rb"));
	}
	
	if (!fp && BurnDrvGetText(DRV_BOARDROM)) {
		_stprintf(szFileName, _T("%s%s.png"), szPreviewDir, BurnDrvGetText(DRV_BOARDROM));
	
		fp = _tfopen(szFileName, _T("rb"));
	}
	
	if (fp) {
		int Screenshot = 0;
		if (szPreviewDir == szAppPreviewsPath || szPreviewDir == szAppTitlesPath) Screenshot = 1;
		hNewImage = LoadPNG(hGameInfoDlg, fp, Screenshot);
		fclose(fp);
	}
	
	if (hNewImage) {
		DeleteObject((HGDIOBJ)hBmp);
		hBmp = hNewImage;
		
		SendDlgItemMessage(hGameInfoDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp);
	} else {
		if (!hBmp) {
			SendDlgItemMessage(hGameInfoDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hPreview);
		}
	}
	
	return 0;
}

static int DisplayRomInfo()
{
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_SCREENSHOT_H), SW_HIDE);
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_MESSAGE_EDIT_ENG), SW_HIDE);
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_LIST1), SW_SHOW);
	UpdateWindow(hGameInfoDlg);
	
	return 0;
}

static int DisplayHistory()
{
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_SCREENSHOT_H), SW_HIDE);
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_LIST1), SW_HIDE);
	ShowWindow(GetDlgItem(hGameInfoDlg, IDC_MESSAGE_EDIT_ENG), SW_SHOW);
	UpdateWindow(hGameInfoDlg);
	
	return 0;
}

static int GameInfoInit()
{
	// Get the games full name
	TCHAR szText[1024] = _T("");
	TCHAR* pszPosition = szText;
	TCHAR* pszName = BurnDrvGetText(DRV_FULLNAME);

	pszPosition += _sntprintf(szText, 1024, pszName);
	
	pszName = BurnDrvGetText(DRV_FULLNAME);
	while ((pszName = BurnDrvGetText(DRV_NEXTNAME | DRV_FULLNAME)) != NULL) {
		if (pszPosition + _tcslen(pszName) - 1024 > szText) {
			break;
		}
		pszPosition += _stprintf(pszPosition, _T(SEPERATOR_2) _T("%s"), pszName);
	}
	
	_tcscpy(szFullName, szText);
	
	_stprintf(szText, _T("Game Information") _T(SEPERATOR_1) _T("%s"), szFullName);
	
	// Set the window caption
	SetWindowText(hGameInfoDlg, szText);
	
	// Setup the tabs
	hTabControl = GetDlgItem(hGameInfoDlg, IDC_TAB1);
        TC_ITEM TCI; 
        TCI.mask = TCIF_TEXT; 

#if defined (_UNICODE)
        TCI.pszText = L"In Game";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 0, (LPARAM) &TCI);        
	TCI.pszText = L"Title";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 1, (LPARAM) &TCI);        
        TCI.pszText = L"Flyer";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 2, (LPARAM) &TCI);
        TCI.pszText = L"Cabinet";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 3, (LPARAM) &TCI);
        TCI.pszText = L"Marquee";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 4, (LPARAM) &TCI);
        TCI.pszText = L"Controls";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 5, (LPARAM) &TCI);
        TCI.pszText = L"PCB";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 6, (LPARAM) &TCI);
        TCI.pszText = L"Rom Info";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 7, (LPARAM) &TCI);
        TCI.pszText = L"History";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 8, (LPARAM) &TCI);
#else
        TCI.pszText = "In Game";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 0, (LPARAM) &TCI);        
	TCI.pszText = "Title";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 1, (LPARAM) &TCI);        
        TCI.pszText = "Flyer";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 2, (LPARAM) &TCI);
        TCI.pszText = "Cabinet";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 3, (LPARAM) &TCI);
        TCI.pszText = "Marquee";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 4, (LPARAM) &TCI);
        TCI.pszText = "Controls";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 5, (LPARAM) &TCI);
        TCI.pszText = "PCB";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 6, (LPARAM) &TCI);
        TCI.pszText = "Rom Info";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 7, (LPARAM) &TCI);
        TCI.pszText = "History";
        SendMessage(hTabControl, TCM_INSERTITEM, (WPARAM) 8, (LPARAM) &TCI);
#endif

	// Load the preview image
	hPreview = LoadBitmap(hAppInst, MAKEINTRESOURCE(BMP_PREVIEW));
	
	// Display preview image
	SendDlgItemMessage(hGameInfoDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)NULL);
	SendDlgItemMessage(hGameInfoDlg, IDC_SCREENSHOT_V, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)NULL);
	UpdatePreview(szAppPreviewsPath);
	
	// Display the game title
	TCHAR szItemText[1024];
	HWND hInfoControl = GetDlgItem(hGameInfoDlg, IDC_TEXTCOMMENT);
	SendMessage(hInfoControl, WM_SETTEXT, (WPARAM)0, (LPARAM)szFullName);
	
	// Display the romname
	bool bBracket = false;
	hInfoControl = GetDlgItem(hGameInfoDlg, IDC_TEXTROMNAME);
	_stprintf(szItemText, _T("%s"), BurnDrvGetText(DRV_NAME));
	if ((BurnDrvGetFlags() & BDF_CLONE) && BurnDrvGetTextA(DRV_PARENT)) {
		int nOldDrvSelect = nBurnDrvSelect;
		pszName = BurnDrvGetText(DRV_PARENT);

		_stprintf(szItemText + _tcslen(szItemText), _T(" (clone of %s"), BurnDrvGetText(DRV_PARENT));

		for (nBurnDrvSelect = 0; nBurnDrvSelect < nBurnDrvCount; nBurnDrvSelect++) {
			if (!_tcsicmp(pszName, BurnDrvGetText(DRV_NAME))) {
				break;
			}
		}
		if (nBurnDrvSelect < nBurnDrvCount) {
			if (BurnDrvGetText(DRV_PARENT)) {
				_stprintf(szItemText + _tcslen(szItemText), _T(", uses ROMs from %s"), BurnDrvGetText(DRV_PARENT));
			}
		}
		nBurnDrvSelect = nOldDrvSelect;
		bBracket = true;
	} else {
		if (BurnDrvGetTextA(DRV_PARENT)) {
			_stprintf(szItemText + _tcslen(szItemText), _T("%suses ROMs from %s"), bBracket ? _T(", ") : _T(" ("), BurnDrvGetText(DRV_PARENT));
			bBracket = true;
		}
	}
	if (bBracket) {
		_stprintf(szItemText + _tcslen(szItemText), _T(")"));
	}
	SendMessage(hInfoControl, WM_SETTEXT, (WPARAM)0, (LPARAM)szItemText);
	
	//Display the rom info
	bool bUseInfo = false;
	szItemText[0] = _T('\0');
	hInfoControl = GetDlgItem(hGameInfoDlg, IDC_TEXTROMINFO);
	if (BurnDrvGetFlags() & BDF_PROTOTYPE) {
		_stprintf(szItemText + _tcslen(szItemText), _T("prototype"));
		bUseInfo = true;
	}
	if (BurnDrvGetFlags() & BDF_BOOTLEG) {
		_stprintf(szItemText + _tcslen(szItemText), _T("%sbootleg / hack"), bUseInfo ? _T(", ") : _T(""));
		bUseInfo = true;
	}
	_stprintf(szItemText + _tcslen(szItemText), _T("%s%i player%s"), bUseInfo ? _T(", ") : _T(""), BurnDrvGetMaxPlayers(), (BurnDrvGetMaxPlayers() != 1) ? _T("s max") : _T(""));
	bUseInfo = true;
	if (BurnDrvGetText(DRV_BOARDROM)) {
		_stprintf(szItemText + _tcslen(szItemText), _T("%suses board-ROMs from %s"), bUseInfo ? _T(", ") : _T(""), BurnDrvGetText(DRV_BOARDROM));
		SendMessage(hInfoControl, WM_SETTEXT, (WPARAM)0, (LPARAM)szItemText);
		bUseInfo = true;
	}
	SendMessage(hInfoControl, WM_SETTEXT, (WPARAM)0, (LPARAM)szItemText);
	
	// Display the release info
	szItemText[0] = _T('\0');
	hInfoControl = GetDlgItem(hGameInfoDlg, IDC_TEXTSYSTEM);
	_stprintf(szItemText, _T("%s (%s, %s hardware)"), BurnDrvGetTextA(DRV_MANUFACTURER) ? BurnDrvGetText(DRV_MANUFACTURER) : _T("unknown"), BurnDrvGetText(DRV_DATE), BurnDrvGetText(DRV_SYSTEM));
	SendMessage(hInfoControl, WM_SETTEXT, (WPARAM)0, (LPARAM)szItemText);
	
	// Display any comments
	szItemText[0] = _T('\0');
	hInfoControl = GetDlgItem(hGameInfoDlg, IDC_TEXTNOTES);
	_stprintf(szItemText, _T("%s"), BurnDrvGetTextA(DRV_COMMENT) ? BurnDrvGetText(DRV_COMMENT) : _T(""));
	SendMessage(hInfoControl, WM_SETTEXT, (WPARAM)0, (LPARAM)szItemText);
	
	// Set up the rom info list
	HWND hList = GetDlgItem(hGameInfoDlg, IDC_LIST1);
	LV_COLUMN LvCol;
	LV_ITEM LvItem;
	
	ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);
	
	memset(&LvCol, 0, sizeof(LvCol));
	LvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
	LvCol.cx = 200;
	LvCol.pszText = _T("Name");	
	SendMessage(hList, LVM_INSERTCOLUMN , 0, (LPARAM)&LvCol);
	LvCol.cx = 100;
	LvCol.pszText = _T("Size (bytes)");	
	SendMessage(hList, LVM_INSERTCOLUMN , 1, (LPARAM)&LvCol);
	LvCol.cx = 100;
	LvCol.pszText = _T("CRC32");	
	SendMessage(hList, LVM_INSERTCOLUMN , 2, (LPARAM)&LvCol);
	LvCol.cx = 200;
	LvCol.pszText = _T("Type");	
	SendMessage(hList, LVM_INSERTCOLUMN , 3, (LPARAM)&LvCol);
	LvCol.cx = 100;
	LvCol.pszText = _T("Flags");	
	SendMessage(hList, LVM_INSERTCOLUMN , 4, (LPARAM)&LvCol);
	LvCol.cx = 100;
	
	memset(&LvItem, 0, sizeof(LvItem));
	LvItem.mask=  LVIF_TEXT;
	LvItem.cchTextMax = 256;
	int RomPos = 0;
	for (int i = 0; i < 0x100; i++) { // assume max 0x100 roms per game
		int nRet;
		struct BurnRomInfo ri;
		char nLen[10] = "";
		char nCrc[8] = "";
		char *szRomName = NULL;
		char Type[100] = "";
		char FormatType[100] = "";

		memset(&ri, 0, sizeof(ri));

		nRet = BurnDrvGetRomInfo(&ri, i);
		nRet += BurnDrvGetRomName(&szRomName, i, 0);
		
		if (ri.nLen == 0) continue;		
		if (ri.nType & BRF_BIOS) continue;
		
		LvItem.iItem = RomPos;
		LvItem.iSubItem = 0;
		LvItem.pszText = ANSIToTCHAR(szRomName, NULL, 0);
		SendMessage(hList, LVM_INSERTITEM, 0, (LPARAM)&LvItem);
		
		sprintf(nLen, "%d", ri.nLen);
		LvItem.iSubItem = 1;
		LvItem.pszText = ANSIToTCHAR(nLen, NULL, 0);
		SendMessage(hList, LVM_SETITEM, 0, (LPARAM)&LvItem);
		
		sprintf(nCrc, "%08X", ri.nCrc);
		if (!(ri.nType & BRF_NODUMP)) {
			LvItem.iSubItem = 2;
			LvItem.pszText = ANSIToTCHAR(nCrc, NULL, 0);
			SendMessage(hList, LVM_SETITEM, 0, (LPARAM)&LvItem);
		}
		
		if (ri.nType & BRF_ESS) sprintf(Type, "%s, Essential", Type);
		if (ri.nType & BRF_OPT) sprintf(Type, "%s, Optional", Type);
		if (ri.nType & BRF_PRG)	sprintf(Type, "%s, Program", Type);
		if (ri.nType & BRF_GRA) sprintf(Type, "%s, Graphics", Type);
		if (ri.nType & BRF_SND) sprintf(Type, "%s, Sound", Type);
		if (ri.nType & BRF_BIOS) sprintf(Type, "%s, BIOS", Type);
		
		for (int j = 0; j < 98; j++) {
			FormatType[j] = Type[j + 2];
		}
		
		LvItem.iSubItem = 3;
		LvItem.pszText = ANSIToTCHAR(FormatType, NULL, 0);
		SendMessage(hList, LVM_SETITEM, 0, (LPARAM)&LvItem);
		
		LvItem.iSubItem = 4;
		LvItem.pszText = _T("");
		if (ri.nType & BRF_NODUMP) LvItem.pszText = _T("No Dump");
		SendMessage(hList, LVM_SETITEM, 0, (LPARAM)&LvItem);
		
		RomPos++;
	}
	
	// Check for board roms
	if (BurnDrvGetTextA(DRV_BOARDROM)) {
		char szBoardName[8] = "";
		unsigned int nOldDrvSelect = nBurnDrvSelect;
		strcpy(szBoardName, BurnDrvGetTextA(DRV_BOARDROM));
			
		for (unsigned int i = 0; i < nBurnDrvCount; i++) {
			nBurnDrvSelect = i;
			if (!strcmp(szBoardName, BurnDrvGetTextA(DRV_NAME))) break;
		}
			
		for (int j = 0; j < 0x100; j++) {
			int nRetBoard;
			struct BurnRomInfo riBoard;
			char nLenBoard[10] = "";
			char nCrcBoard[8] = "";
			char *szBoardRomName = NULL;
			char BoardType[100] = "";
			char BoardFormatType[100] = "";

			memset(&riBoard, 0, sizeof(riBoard));

			nRetBoard = BurnDrvGetRomInfo(&riBoard, j);
			nRetBoard += BurnDrvGetRomName(&szBoardRomName, j, 0);
		
			if (riBoard.nLen == 0) continue;
				
			LvItem.iItem = RomPos;
			LvItem.iSubItem = 0;
			LvItem.pszText = ANSIToTCHAR(szBoardRomName, NULL, 0);
			SendMessage(hList, LVM_INSERTITEM, 0, (LPARAM)&LvItem);
		
			sprintf(nLenBoard, "%d", riBoard.nLen);
			LvItem.iSubItem = 1;
			LvItem.pszText = ANSIToTCHAR(nLenBoard, NULL, 0);
			SendMessage(hList, LVM_SETITEM, 0, (LPARAM)&LvItem);
		
			sprintf(nCrcBoard, "%08X", riBoard.nCrc);
			if (!(riBoard.nType & BRF_NODUMP)) {
				LvItem.iSubItem = 2;
				LvItem.pszText = ANSIToTCHAR(nCrcBoard, NULL, 0);
				SendMessage(hList, LVM_SETITEM, 0, (LPARAM)&LvItem);
			}
			
			if (riBoard.nType & BRF_ESS) sprintf(BoardType, "%s, Essential", BoardType);
			if (riBoard.nType & BRF_OPT) sprintf(BoardType, "%s, Optional", BoardType);
			if (riBoard.nType & BRF_PRG) sprintf(BoardType, "%s, Program", BoardType);
			if (riBoard.nType & BRF_GRA) sprintf(BoardType, "%s, Graphics", BoardType);
			if (riBoard.nType & BRF_SND) sprintf(BoardType, "%s, Sound", BoardType);
			if (riBoard.nType & BRF_BIOS) sprintf(BoardType, "%s, BIOS", BoardType);
		
			for (int k = 0; k < 98; k++) {
				BoardFormatType[k] = BoardType[k + 2];
			}
		
			LvItem.iSubItem = 3;
			LvItem.pszText = ANSIToTCHAR(BoardFormatType, NULL, 0);
			SendMessage(hList, LVM_SETITEM, 0, (LPARAM)&LvItem);
		
			LvItem.iSubItem = 4;
			LvItem.pszText = _T("");
			if (riBoard.nType & BRF_NODUMP) LvItem.pszText = _T("No Dump");
			SendMessage(hList, LVM_SETITEM, 0, (LPARAM)&LvItem);
			
			RomPos++;
		}
		
		nBurnDrvSelect = nOldDrvSelect;
	}
	
	// Get the history info
	CHAR szFileName[MAX_PATH] = "";
	sprintf(szFileName, "%shistory.dat", TCHARToANSI(szAppHistoryPath, NULL, 0));
	
	FILE *fp = fopen(szFileName, "rt");	
	char Temp[1000];
	int inGame = 0;
	TCHAR szBuffer[50000] = _T("{\\rtf1\\ansi{\\fonttbl(\\f0\\fswiss\\fprq2 Tahoma;)}{\\colortbl;\\red0\\green0\\blue0;\\red110\\green107\\blue106;}");
	
	if (fp) {		
		while (!feof(fp)) {
			char *Tokens;
			
			fgets(Temp, 1000, fp);
			if (!strncmp("$info=", Temp, 6)) {
				Tokens = strtok(Temp, "=,");
				while (Tokens != NULL) {
					if (!strcmp(Tokens, BurnDrvGetTextA(DRV_NAME))) {
						inGame = 1;
						break;
					}

					Tokens = strtok(NULL, "=,");
				}
			}
			
			if (inGame) {
				int nTitleWrote = 0;
				while (strncmp("$end", Temp, 4)) {
					fgets(Temp, 1000, fp);

					if (!strncmp("$", Temp, 1)) continue;
					if (!strncmp("\n", Temp, 1)) _stprintf(szBuffer, _T("%s\\par"), szBuffer);
						
					if (!nTitleWrote) {
						_stprintf(szBuffer, _T("%s{\\b\\f0\\fs28\\cf1 %s}"), szBuffer, ANSIToTCHAR(Temp, NULL, 0));
					} else {
						if (!strncmp("- ", Temp, 2)) {
							_stprintf(szBuffer, _T("%s{\\b\\f0\\fs16\\cf1 %s}"), szBuffer, ANSIToTCHAR(Temp, NULL, 0));
						} else {
							_stprintf(szBuffer, _T("%s{\\f0\\fs16\\cf2 %s}"), szBuffer, ANSIToTCHAR(Temp, NULL, 0));
						}
					}
						
					if (strcmp("\n", Temp)) nTitleWrote = 1;
				}
				break;
			}
		}
		fclose(fp);
	}
	
	_stprintf(szBuffer, _T("%s}"), szBuffer);
	SendMessage(GetDlgItem(hGameInfoDlg, IDC_MESSAGE_EDIT_ENG), WM_SETTEXT, (WPARAM)0, (LPARAM)szBuffer);
	
	// Make a white brush
	hWhiteBGBrush = CreateSolidBrush(RGB(0xFF,0xFF,0xFF));
	
	return 0;
}

static void MyEndDialog()
{
	SendDlgItemMessage(hGameInfoDlg, IDC_SCREENSHOT_H, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)NULL);
	
	if (hBmp) {
		DeleteObject((HGDIOBJ)hBmp);
		hBmp = NULL;
	}
	if (hPreview) {
		DeleteObject((HGDIOBJ)hPreview);
		hPreview = NULL;
	}
	
	hTabControl = NULL;
	memset(szFullName, 0, 1024);
	
	EndDialog(hGameInfoDlg, 0);
}

static BOOL CALLBACK DialogProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (Msg == WM_INITDIALOG) {
		hGameInfoDlg = hDlg;

		if (bDrvOkay) {
			if (!kNetGame && bAutoPause) bRunPause = 1;
			AudSoundStop();
		}
		
		GameInfoInit();
		
		return TRUE;
	}
	
	if (Msg == WM_CLOSE) {
		MyEndDialog();
		DeleteObject(hWhiteBGBrush);
		
		EnableWindow(hScrnWnd, TRUE);
		DestroyWindow(hGameInfoDlg);
		
		FreeLibrary(hRiched);
		hRiched = NULL;
		
		if (bDrvOkay) {
			if(!bAltPause && bRunPause) bRunPause = 0;
			AudSoundPlay();
		}
		
		return 0;
	}
	
	if (Msg == WM_COMMAND) {
		int Id = LOWORD(wParam);
		int Notify = HIWORD(wParam);
		
		if (Id == IDCANCEL && Notify == BN_CLICKED) {
			SendMessage(hGameInfoDlg, WM_CLOSE, 0, 0);
			return 0;
		}
	}
	
	if (Msg == WM_NOTIFY) {
		NMHDR* pNmHdr = (NMHDR*)lParam;

		if (pNmHdr->code == TCN_SELCHANGE) {
			int TabPage= SendMessage(hTabControl, TCM_GETCURSEL, 0, 0);
			if (TabPage == 0) UpdatePreview(szAppPreviewsPath);
			if (TabPage == 1) UpdatePreview(szAppTitlesPath);
			if (TabPage == 2) UpdatePreview(szAppFlyersPath);
			if (TabPage == 3) UpdatePreview(szAppCabinetsPath);
			if (TabPage == 4) UpdatePreview(szAppMarqueesPath);
			if (TabPage == 5) UpdatePreview(szAppControlsPath);
			if (TabPage == 6) UpdatePreview(szAppPCBsPath);
			if (TabPage == 7) DisplayRomInfo();
			if (TabPage == 8) DisplayHistory();
			return FALSE;
		}
	}
	
	if (Msg == WM_CTLCOLORSTATIC) {
		if ((HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_LABELCOMMENT) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_LABELROMNAME) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_LABELROMINFO) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_LABELSYSTEM) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_LABELNOTES) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_TEXTCOMMENT) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_TEXTROMNAME) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_TEXTROMINFO) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_TEXTSYSTEM) || (HWND)lParam == GetDlgItem(hGameInfoDlg, IDC_TEXTNOTES)) {
			return (BOOL)hWhiteBGBrush;
		}
	}

	return 0;
}

int GameInfoDialogCreate()
{	
#if defined (_UNICODE)
	hRiched = LoadLibrary(L"RICHED20.DLL");
#else
	hRiched = LoadLibrary("RICHED20.DLL");
#endif

	if (hRiched) {	
		DestroyWindow(hGameInfoDlg);
	
		hGameInfoDlg = FBACreateDialog(hAppInst, MAKEINTRESOURCE(IDD_GAMEINFO), hScrnWnd, DialogProc);
		if (hGameInfoDlg == NULL) {
			return 1;
		}

		WndInMid(hGameInfoDlg, hScrnWnd);
		ShowWindow(hGameInfoDlg, SW_NORMAL);
	}
	
	return 0;
}
