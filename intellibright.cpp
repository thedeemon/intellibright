// intellibright.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <vd2/plugin/vdvideofilt.h>
#include <vector>
#include "resource.h"
#include <Commctrl.h>
#include <shellapi.h>

struct MyFilterData {
	int targetMin, targetMax; //0 .. 255
	int dynamicity, sceneThreshold, maxK; // 1 .. 100;  maxK = 40 means k <= 4.0
	int alpha; // 1..50; *0.1 too

	std::vector<int> lastColors, curve;
	double oldK;
	int oldMin;
	HBITMAP hbmp;
	bool drawn;

	void init() {
		targetMin = 0; targetMax = 255; dynamicity = 10; sceneThreshold = 20; oldMin = 0; oldK = 1.0; maxK = 25;
		alpha = 15;
		lastColors.resize(96);
		memset(&lastColors[0], 0, 96*sizeof(int));
		curve.resize(256);
		hbmp = NULL;
		drawn = false;
	}

	void createBmp() {
		hbmp = CreateCompatibleBitmap(GetDC(NULL), 256, 256);
	}

	void calcCurve() {
		for(int i=0;i<256;i++) {
			double x = i / 255.0;
			double a = alpha / 10.0;
			double k = maxK / 10.0;
			int t = targetMin + pow(x, a)*k * 255;
			if (t > targetMax) t = targetMax;
			curve[i] = t;
		}
		drawn = false;
	}

	void drawCurve() {
		if (hbmp==NULL) return;
		int* data = new int[256*256];
		int bgClr = 0x40, dataClr = 0x80f040;
		for(int x=0;x<256;x++) {
			for(int y=0;y<targetMin;y++)
				data[(255-y)*256+x] = bgClr;
			for(int y=targetMin; y <= curve[x]; y++)
				data[(255-y)*256+x] = dataClr;
			for(int y= curve[x]+1; y < 256; y++)
				data[(255-y)*256+x] = bgClr;
		}
		for(int n=1;n<10;n++) {
			for(int x=0;x<256;x++) {
				int y = n*255/10;
				data[y*256 + x] |= 0x808080;
				data[x*256 + y] |= 0x808080;
			}
		}
		SetBitmapBits(hbmp, 256*256*4, data);
		delete[] data;
		drawn = true;
	}

};

extern HINSTANCE g_hInst;


long paramProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff) 
{
	return FILTERPARAM_SWAP_BUFFERS;
}

int runProc(const VDXFilterActivation *fa, const VDXFilterFunctions *ff) 
{
	int           w        = fa->dst.w;
    int           h        = fa->dst.h;
    uint32       *dst      = (uint32 *)fa->dst.data;
    ptrdiff_t     dstpitch = fa->dst.pitch;
    const uint32 *src      = (const uint32 *)fa->src.data;
    ptrdiff_t     srcpitch = fa->dst.pitch;
	MyFilterData* pData = (MyFilterData*)fa->filter_data;

	int nr[256]={}, ng[256]={}, nb[256]={}, nrgb[256]={}, cls[96]={};

	//collect histos
    for(int y=0; y<h; ++y) {
        for(int x=0; x<w; ++x) {
			const BYTE* pixel = (const BYTE*)&src[x];
			nr[ pixel[0] ]++;
			ng[ pixel[1] ]++;
			nb[ pixel[2] ]++;
		}
        dst = (uint32 *)((char *)dst + dstpitch);
        src = (const uint32 *)((const char *)src + srcpitch);
    }
    
	for(int i=0;i<256;i++) {
		nrgb[i] = nr[i] + ng[i] + nb[i];
		cls[i/8] += nr[i];
		cls[i/8 + 32] += ng[i];
		cls[i/8 + 64] += nb[i];
	}

	int minBr = 0, maxBr = 255, shift = 0;
	while(minBr < 256 && nrgb[minBr]==0) minBr++;
	while(maxBr >= 0 && nrgb[maxBr]==0) maxBr--;
	double k = 1;
	bool work = false;
	if (maxBr > minBr && (minBr != pData->targetMin || maxBr != pData->targetMax)) {
		k = (double)(pData->targetMax - pData->targetMin) / (maxBr - minBr);
		if (k > pData->maxK / 10.0) {
			k = pData->maxK / 10.0;
		}
		work = true;
	}

	int sum=0, total=0; //scene change check
	for(int i=0; i<96; i++) {
		sum += abs(cls[i] - pData->lastColors[i]);
		total += cls[i];
	}
	double chng = (double)sum/total;
	if (chng * 100 < pData->sceneThreshold) { // not new scene
		double alpha = pData->dynamicity / 100.0;
		k = k * alpha + pData->oldK * (1.0 - alpha); // smooth the parameters
		minBr = minBr * alpha + pData->oldMin * (1.0 - alpha);
		work = true;
	}

	BYTE tbl[256];

    dst = fa->dst.data;
    src = (const uint32 *)fa->src.data;

	if (work) {
		for(int i=0;i<256;i++) {
			int v = (i - minBr) * k + pData->targetMin + 0.5;
			tbl[i] = v < pData->targetMin ? pData->targetMin : (v > pData->targetMax ? pData->targetMax : v);
		}
		for(int y=0; y<h; ++y) {
			for(int x=0; x<w; ++x) {
				const BYTE* pixel = (const BYTE*)&src[x];
				BYTE* resPixel = (BYTE*)&dst[x];
				resPixel[0] = tbl[pixel[0]];
				resPixel[1] = tbl[pixel[1]];
				resPixel[2] = tbl[pixel[2]];
				resPixel[3] = pixel[3]; //Alpha
			}        
			dst = (uint32 *)((char *)dst + dstpitch);
			src = (const uint32 *)((const char *)src + srcpitch);
		}
	} else { //copy
		for(int y=0; y<h; ++y) {
			memcpy(dst, src, w*4);        			
			dst = (uint32 *)((char *)dst + dstpitch);
			src = (const uint32 *)((const char *)src + srcpitch);
		}
	}    

	for(int i=0;i<96;i++)
		pData->lastColors[i] = cls[i];
	pData->oldK = k;
	pData->oldMin = minBr;
    return 0;
}

int startProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff)
{
	MyFilterData* pData = (MyFilterData*)fa->filter_data;
	if (pData->dynamicity==0) pData->init();
	return 0;
}

int endProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff)
{
	return 0;
}

INT_PTR CALLBACK SettingsDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) 
{
	MyFilterData* pData = (MyFilterData*)GetWindowLongPtr(hdlg, DWLP_USER);
	static bool editing = false;
	static MyFilterData tempData;
	TCHAR str[64];
	RECT imgRect;
	imgRect.left = 240; imgRect.top = 256;
	imgRect.right = imgRect.left + 256; imgRect.bottom = imgRect.top + 256;
    switch(msg) {
        case WM_INITDIALOG:
			SetWindowLongPtr(hdlg, DWLP_USER, lParam);
			pData = (MyFilterData*)lParam;
			if (pData->dynamicity==0) pData->init();
			tempData = *pData;
			editing = true;
			SetDlgItemInt(hdlg, IDC_MINBR, pData->targetMin, FALSE);
			SetDlgItemInt(hdlg, IDC_MAXBR, pData->targetMax, FALSE);
			SetDlgItemInt(hdlg, IDC_DYNA, pData->dynamicity, FALSE);
			SetDlgItemInt(hdlg, IDC_SCENE, pData->sceneThreshold, FALSE);
			_snwprintf(str, 64, L"%.1lf", pData->maxK / 10.0);
			SetDlgItemText(hdlg, IDC_MAXK, str);
			_snwprintf(str, 64, L"%.1lf", pData->alpha / 10.0);
			SetDlgItemText(hdlg, IDC_ALPHA, str);

			SendMessage(GetDlgItem(hdlg, IDC_SLMINBR), TBM_SETRANGE, 0, MAKELONG(0, 255)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLMINBR), TBM_SETPOS, 1, pData->targetMin); 
			SendMessage(GetDlgItem(hdlg, IDC_SLMAXBR), TBM_SETRANGE, 0, MAKELONG(0, 255)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLMAXBR), TBM_SETPOS, 1, pData->targetMax); 

			SendMessage(GetDlgItem(hdlg, IDC_SLDYNA), TBM_SETRANGE, 0, MAKELONG(1, 100)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLDYNA), TBM_SETPOS, 1, pData->dynamicity); 
			SendMessage(GetDlgItem(hdlg, IDC_SLSCENE), TBM_SETRANGE, 0, MAKELONG(1, 100)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLSCENE), TBM_SETPOS, 1, pData->sceneThreshold); 
			SendMessage(GetDlgItem(hdlg, IDC_SLMAXK), TBM_SETRANGE, 0, MAKELONG(1, 100)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLMAXK), TBM_SETPOS, 1, pData->maxK); 
			SendMessage(GetDlgItem(hdlg, IDC_SLALPHA), TBM_SETRANGE, 0, MAKELONG(1, 50)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLALPHA), TBM_SETPOS, 1, pData->alpha); 
			editing = false;
			tempData.createBmp();
			tempData.calcCurve();
			return TRUE;
		case WM_COMMAND:
			if (editing) return FALSE;
			switch(LOWORD(wParam)) {
				case IDOK:
					*pData = tempData;
					EndDialog(hdlg, TRUE);
					return TRUE;
				case IDCANCEL:
					EndDialog(hdlg, FALSE);
					return TRUE;
				case IDC_BTNWWW:
					ShellExecute(NULL, NULL, L"http://www.infognition.com/", NULL, NULL, SW_SHOW);
					break;
			}
			break;
		case WM_HSCROLL:
			{
			if (editing) return FALSE;
			editing = true;
			HWND h = (HWND)lParam;
			UINT id = GetWindowLong(h, GWL_ID);
			int x, y;
			switch(id) {
			case IDC_SLMINBR:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				tempData.targetMin = x;
				SetDlgItemInt(hdlg, IDC_MINBR, x, FALSE);
				y = SendMessage(GetDlgItem(hdlg, IDC_SLMAXBR), TBM_GETPOS, 0, 0); 
				if (y < x) {
					y = x < 255 ? x + 1 : 255;
					SendMessage(GetDlgItem(hdlg, IDC_SLMAXBR), TBM_SETPOS, 1, y); 
					SetDlgItemInt(hdlg, IDC_MAXBR, y, FALSE);
				}
				tempData.calcCurve();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			case IDC_SLMAXBR:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				tempData.targetMax = x;
				SetDlgItemInt(hdlg, IDC_MAXBR, x, FALSE);
				y = SendMessage(GetDlgItem(hdlg, IDC_SLMINBR), TBM_GETPOS, 0, 0); 
				if (y > x) {
					y = x > 0 ? x - 1 : 0;
					SendMessage(GetDlgItem(hdlg, IDC_SLMINBR), TBM_SETPOS, 1, y); 
					SetDlgItemInt(hdlg, IDC_MINBR, y, FALSE);
				}
				tempData.calcCurve();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			case IDC_SLDYNA:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				tempData.dynamicity = x;
				SetDlgItemInt(hdlg, IDC_DYNA, x, FALSE);
				break;
			case IDC_SLSCENE:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				tempData.sceneThreshold = x;
				SetDlgItemInt(hdlg, IDC_SCENE, x, FALSE);
				break;
			case IDC_SLMAXK:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				tempData.maxK = x;
				//SetDlgItemInt(hdlg, IDC_MAXK, x, FALSE);
				_snwprintf(str, 64, L"%.1lf", x / 10.0);
				SetDlgItemText(hdlg, IDC_MAXK, str);
				tempData.calcCurve();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			case IDC_SLALPHA:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				tempData.alpha = x;
				_snwprintf(str, 64, L"%.1lf", x / 10.0);
				SetDlgItemText(hdlg, IDC_ALPHA, str);
				tempData.calcCurve();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			}
			SetWindowLong(hdlg, DWLP_MSGRESULT, 0);
			editing = false;
			return TRUE;
			}
		case WM_PAINT:
			{
				if (tempData.hbmp==NULL) return 0;
				if (!tempData.drawn)
					tempData.drawCurve();
				PAINTSTRUCT ps; 
				HDC hdc; 
				hdc = BeginPaint(hdlg, &ps); 
				HDC memDC = CreateCompatibleDC(hdc);
				SelectObject(memDC, tempData.hbmp);
				//TextOut(hdc, 0, 0, "Hello, Windows!", 15); 
				BitBlt(hdc, imgRect.left, imgRect.top, 256,256, memDC, 0,0, SRCCOPY);
				EndPaint(hdlg, &ps); 
				DeleteObject(memDC);
			}
			break;
	}//switch msg
	return 0;
}

int configProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff, VDXHWND hwndParent) 
{
    MyFilterData* pData = (MyFilterData*)fa->filter_data;
    
    return !DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_SETTINGS), (HWND)hwndParent, SettingsDlgProc, (LPARAM)pData);
}

static void stringProc2(const VDXFilterActivation *fa, const VDXFilterFunctions *ff, char *buf, int maxlen) 
{
	MyFilterData* pData = (MyFilterData*)fa->filter_data;
	_snprintf(buf, maxlen, " (%d-%d)", pData->targetMin, pData->targetMax );
}

void stringProc(const VDXFilterActivation *fa, const VDXFilterFunctions *ff, char *buf)
{
	stringProc2(fa, ff, buf, 80);
}

void configScriptFunc(IVDXScriptInterpreter *isi, void *lpVoid, VDXScriptValue *argv, int argc) 
{
	VDXFilterActivation *fa = (VDXFilterActivation *)lpVoid;
	MyFilterData* pData = (MyFilterData*)fa->filter_data;
	if (pData->dynamicity==0) pData->init();
	if (argc==5) {
		pData->targetMin = argv[0].asInt(); pData->targetMax = argv[1].asInt();
		pData->dynamicity = argv[2].asInt(); pData->sceneThreshold = argv[3].asInt(); 
		pData->maxK = argv[4].asInt();
	}
}

VDXScriptFunctionDef script_functions[] = {
    { (VDXScriptFunctionPtr)configScriptFunc, "Config", "0iiiii" },
    { NULL, NULL, NULL },
};

VDXScriptObject script_obj = { NULL, script_functions, NULL };

bool fssProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff, char *buf, int bufsize) 
{
	MyFilterData* pData = (MyFilterData*)fa->filter_data;
	_snprintf(buf, bufsize, "Config(%d, %d, %d, %d, %d)", pData->targetMin, pData->targetMax, 
		pData->dynamicity, pData->sceneThreshold, pData->maxK);
	return true;
}

static struct VDXFilterDefinition myfilter_definition={
    0,0,NULL,                       // next, prev, and module (set to zero)
    "Intelligent Brightness",                    // name
    "Smoothly adjusts brightness and contrast to desired levels.",    // description
    "Infognition Co. Ltd.",         // author / maker
    NULL,                           // no private data
    sizeof(MyFilterData),           // no instance data size
    NULL,                           // no initProc
    NULL,                           // no deinitProc
    runProc,                   // runProc
	paramProc,
	configProc,
	stringProc,
	startProc,
	endProc,
	&script_obj, fssProc,
	stringProc2,0,0,0,
	0, 0,
	0,0    
};

VDXFilterDefinition *g_registeredFilterDef;

extern "C" __declspec(dllexport) int __cdecl VirtualdubFilterModuleInit2(struct VDXFilterModule *fm, const VDXFilterFunctions *ff, int& vdfd_ver, int& vdfd_compat) 
{
	/*if (vdfd_ver < 14) {
		MessageBox(NULL, "VirtualDub version 1.9.1 or higher required.", "Infognition Super Resolution", MB_ICONERROR);
		return 1;
	}*/
    g_registeredFilterDef = ff->addFilter(fm, &myfilter_definition, sizeof(VDXFilterDefinition));

    vdfd_ver        = VIRTUALDUB_FILTERDEF_VERSION; //14
    vdfd_compat     = 14;    // VD 1.9.1 or higher

    return 0;
}

extern "C" __declspec(dllexport) void __cdecl VirtualdubFilterModuleDeinit(struct VDXFilterModule *fm, const VDXFilterFunctions *ff) 
{
    ff->removeFilter(g_registeredFilterDef);
}
