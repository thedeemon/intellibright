// intellibright.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <vd2/plugin/vdvideofilt.h>
#include <vector>
#include "resource.h"
#include <Commctrl.h>
#include <shellapi.h>
#include <Windowsx.h>

struct MyFilterData {
	int targetMin, targetMax; //0 .. 255
	int dynamicity, sceneThreshold; // 1 .. 100
	int	curveEnd; // 1 .. 255; 
	int alpha; // 1..250; /50
	int p_mode; //0,1,2 - max, average, median
	int curveStart;
	int beta;

	std::vector<int> lastColors, curve;
	double oldK;
	int oldMin;
	HBITMAP hbmp;
	bool drawn;
	IVDXFilterPreview* ifp;
	int lastP; // P value of last processed frame

	void init() {
		targetMin = 0; targetMax = 255; dynamicity = 10; sceneThreshold = 20; oldMin = 0; oldK = 1.0; 		
		curveEnd = 26;	alpha = 50; p_mode = 1; lastP = 255;
		curveStart = 0; beta = 50;
		lastColors.resize(96);
		memset(&lastColors[0], 0, 96*sizeof(int));
		curve.resize(256);
		hbmp = NULL;  
		drawn = false;
		//ifp = NULL;
	}

	void createBmp() {
		if (hbmp==NULL)
			hbmp = CreateCompatibleBitmap(GetDC(NULL), 256, 256);
	}

	void calcCurve() {
		for(int i=0;i<curveStart;i++) 
			curve[i] = targetMin + i;
		/*
		f(x) = ax^3 + bx^2 + cx
		f'(0) = c = alpha
		f'(1) = 3a + 2b + c = beta
		  => 3a + 2b = beta - alpha
		  => b = (beta - alpha - 3a)/2
		f(1) = a+b+c = 1
		  => a + beta/2 - alpha/2 - 3/2a + alpha = 1
		  => beta + alpha  - 2 =  a

		*/
		for(int i=curveStart;i<curveEnd;i++) {
			double x = (double)(i - curveStart) / (curveEnd - curveStart);
			double al = alpha / 50.0;
			double be = beta / 50.0;
			double c = al;
			double a = al + be - 2;
			double b = (be - al - 3*a)/2;
			double h = targetMax - targetMin - curveStart;
			int t = targetMin + curveStart + (a*x*x*x + b*x*x + c*x) * h;
			if (t > targetMax) t = targetMax;
			if (t < targetMin) t = targetMin;
			curve[i] = t;
		}
		for(int i=curveEnd;i<256;i++)
			curve[i] = targetMax;
		drawn = false;
	}

	void drawCurve() {
		if (hbmp==NULL) return;
		int* data = new int[256*256];
		int bgClr = 0x40, dataClr = 0x40c0c0, P_Clr = 0xFF0000, P_Clr1 = 0x00FF00;;
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

		int yeqx = targetMin + lastP;
		if (yeqx > curve[lastP]) yeqx = curve[lastP];
		for(int y=targetMin; y < yeqx; y++)
			data[(255-y)*256+lastP] = P_Clr1;
		for(int y=yeqx; y <= curve[lastP]; y++)
			data[(255-y)*256+lastP] = P_Clr;

		SetBitmapBits(hbmp, 256*256*4, data);
		delete[] data;
		drawn = true;
	}

	void redoFrame() {
		for(int i=0;i<lastColors.size();i++) 
			lastColors[i] = 0;
		if (ifp) ifp->RedoFrame();
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

	int nr[256]={}, ng[256]={}, nb[256]={}, nrgb[256]={}, cls[96]={}, nmx[256]={};
	long nVals = w * h * 3, nPix = w * h;

	//collect histos
    for(int y=0; y<h; ++y) {
        for(int x=0; x<w; ++x) {
			const BYTE* pixel = (const BYTE*)&src[x];
			nr[ pixel[0] ]++;
			ng[ pixel[1] ]++;
			nb[ pixel[2] ]++;
			const int mx = max(pixel[0], max(pixel[1], pixel[2]));
			nmx[mx]++;
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

	//determine target range by using the curve
	int P = 0;
	switch(pData->p_mode) {
	case 0://max
		P = maxBr;		
		break;
	case 1://avg
		{
			long sumBr = 0;
			for(int i=0;i<256;i++) {
				long q = (long)nmx[i] * i;
				sumBr += q;
			}
			P = sumBr / nPix;
		}
		break;
	case 2://median
		{
			long sumPix = 0;
			long p = 0;			
			auto half = nPix / 2;
			while(p < 256 && sumPix + nmx[p] < half) {
				sumPix += nmx[p];
				p++;
			}
			P = p;
		}
		break;
	}
	if (P != pData->lastP) {
		pData->lastP = P;
		pData->drawn = false;
	}

	int trgMin = pData->targetMin;
	int trgMax = pData->curve[P];
	double maxAllowedK = maxBr > minBr ? (double)(pData->targetMax - trgMin) / (maxBr - minBr) : 1;
	if (maxBr > minBr && (minBr != trgMin || maxBr != trgMax)) {
		if (pData->p_mode==0)
			k = (double)(trgMax - trgMin) / (maxBr - minBr);
		else
			k = min(P > 0 ? (double)(trgMax - trgMin) / P : 1, maxAllowedK);
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
			int v = (i - minBr) * k + trgMin + 0.5;
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
	static MyFilterData orgData;
	TCHAR str[64];
	RECT imgRect;
	imgRect.left = 240; imgRect.top = 320;
	imgRect.right = imgRect.left + 256; imgRect.bottom = imgRect.top + 256;
	HWND combo;
	const int timerID = 526;
    switch(msg) {
        case WM_INITDIALOG:
			SetWindowLongPtr(hdlg, DWLP_USER, lParam);
			pData = (MyFilterData*)lParam;
			if (pData->dynamicity==0) pData->init();
			orgData = *pData;
			editing = true;
			SetDlgItemInt(hdlg, IDC_MINBR, pData->targetMin, FALSE);
			SetDlgItemInt(hdlg, IDC_MAXBR, pData->targetMax, FALSE);
			SetDlgItemInt(hdlg, IDC_DYNA, pData->dynamicity, FALSE);
			SetDlgItemInt(hdlg, IDC_SCENE, pData->sceneThreshold, FALSE);
			//_snwprintf(str, 64, L"%d", pData->curveEnd);
			//SetDlgItemText(hdlg, IDC_MAXK, str);
			SetDlgItemInt(hdlg, IDC_CSTART, pData->curveStart, FALSE);
			SetDlgItemInt(hdlg, IDC_CEND, pData->curveEnd, FALSE);
			_snwprintf(str, 64, L"%.2lf", pData->alpha / 50.0);
			SetDlgItemText(hdlg, IDC_ALPHA1, str);
			_snwprintf(str, 64, L"%.2lf", pData->beta / 50.0);
			SetDlgItemText(hdlg, IDC_BETA, str);

			SendMessage(GetDlgItem(hdlg, IDC_SLMINBR), TBM_SETRANGE, 0, MAKELONG(0, 255)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLMINBR), TBM_SETPOS, 1, pData->targetMin); 
			SendMessage(GetDlgItem(hdlg, IDC_SLMAXBR), TBM_SETRANGE, 0, MAKELONG(0, 255)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLMAXBR), TBM_SETPOS, 1, pData->targetMax); 

			SendMessage(GetDlgItem(hdlg, IDC_SLDYNA), TBM_SETRANGE, 0, MAKELONG(1, 100)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLDYNA), TBM_SETPOS, 1, pData->dynamicity); 
			SendMessage(GetDlgItem(hdlg, IDC_SLSCENE), TBM_SETRANGE, 0, MAKELONG(1, 100)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLSCENE), TBM_SETPOS, 1, pData->sceneThreshold); 
			SendMessage(GetDlgItem(hdlg, IDC_SLCSTART), TBM_SETRANGE, 0, MAKELONG(0, 255)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLCSTART), TBM_SETPOS, 1, pData->curveStart); 
			SendMessage(GetDlgItem(hdlg, IDC_SLCEND), TBM_SETRANGE, 0, MAKELONG(0, 255)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLCEND), TBM_SETPOS, 1, pData->curveEnd); 
			SendMessage(GetDlgItem(hdlg, IDC_SLALPHA1), TBM_SETRANGE, 0, MAKELONG(1, 250)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLALPHA1), TBM_SETPOS, 1, pData->alpha); 
			SendMessage(GetDlgItem(hdlg, IDC_SLBETA), TBM_SETRANGE, 0, MAKELONG(1, 250)); 
			SendMessage(GetDlgItem(hdlg, IDC_SLBETA), TBM_SETPOS, 1, pData->beta); 

			combo = GetDlgItem(hdlg, IDC_PCOMBO);
			ComboBox_AddString(combo, L"Source Max");
			ComboBox_AddString(combo, L"Source Average");
			ComboBox_AddString(combo, L"Source Median");
			ComboBox_SetCurSel(combo, pData->p_mode);

			if (pData->ifp)
				pData->ifp->InitButton((VDXHWND)GetDlgItem(hdlg, IDC_BTNPREVIEW));
			editing = false;
			pData->createBmp();
			pData->calcCurve();			
			return TRUE;
		case WM_COMMAND:
			if (editing) return FALSE;
			switch(LOWORD(wParam)) {
				case IDOK:
					//*pData = tempData;		
					pData->ifp = NULL;
					KillTimer(hdlg, timerID);
					EndDialog(hdlg, TRUE);
					return TRUE;
				case IDCANCEL:
					*pData = orgData;
					pData->ifp = NULL;
					KillTimer(hdlg, timerID);
					EndDialog(hdlg, FALSE);
					return TRUE;
				case IDC_BTNWWW:
					ShellExecute(NULL, NULL, L"http://www.infognition.com/", NULL, NULL, SW_SHOW);
					break;
				case IDC_PCOMBO:
					if (HIWORD(wParam) == CBN_SELCHANGE) {
						pData->p_mode = ComboBox_GetCurSel(GetDlgItem(hdlg, IDC_PCOMBO));
						pData->redoFrame();
						InvalidateRect(hdlg, &imgRect, FALSE);
					}
					break;
				case IDC_BTNPREVIEW:
					if (pData->ifp) { 
						pData->ifp->Toggle((VDXHWND)hdlg);
						SetTimer(hdlg, timerID, 100, NULL);
					}
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
				pData->targetMin = x;
				SetDlgItemInt(hdlg, IDC_MINBR, x, FALSE);
				y = SendMessage(GetDlgItem(hdlg, IDC_SLMAXBR), TBM_GETPOS, 0, 0); 
				if (y < x) {
					y = x < 255 ? x + 1 : 255;
					SendMessage(GetDlgItem(hdlg, IDC_SLMAXBR), TBM_SETPOS, 1, y); 
					SetDlgItemInt(hdlg, IDC_MAXBR, y, FALSE);
				}
				pData->calcCurve();
				pData->redoFrame();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			case IDC_SLMAXBR:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				pData->targetMax = x;
				SetDlgItemInt(hdlg, IDC_MAXBR, x, FALSE);
				y = SendMessage(GetDlgItem(hdlg, IDC_SLMINBR), TBM_GETPOS, 0, 0); 
				if (y > x) {
					y = x > 0 ? x - 1 : 0;
					SendMessage(GetDlgItem(hdlg, IDC_SLMINBR), TBM_SETPOS, 1, y); 
					SetDlgItemInt(hdlg, IDC_MINBR, y, FALSE);
				}
				pData->calcCurve();
				pData->redoFrame();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			case IDC_SLDYNA:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				pData->dynamicity = x;
				SetDlgItemInt(hdlg, IDC_DYNA, x, FALSE);
				break;
			case IDC_SLSCENE:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				pData->sceneThreshold = x;
				SetDlgItemInt(hdlg, IDC_SCENE, x, FALSE);
				break;
			case IDC_SLCSTART:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				pData->curveStart = x;
				SetDlgItemInt(hdlg, IDC_CSTART, x, FALSE);
				y = SendMessage(GetDlgItem(hdlg, IDC_SLCEND), TBM_GETPOS, 0, 0); 
				if (y < x) {
					y = x < 255 ? x + 1 : 255;
					SendMessage(GetDlgItem(hdlg, IDC_SLCEND), TBM_SETPOS, 1, y); 
					SetDlgItemInt(hdlg, IDC_CEND, y, FALSE);
					pData->curveEnd = y;
				}
				pData->calcCurve();
				pData->redoFrame();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			case IDC_SLCEND:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				pData->curveEnd = x;
				SetDlgItemInt(hdlg, IDC_CEND, x, FALSE);
				y = SendMessage(GetDlgItem(hdlg, IDC_SLCSTART), TBM_GETPOS, 0, 0); 
				if (y > x) {
					y = x > 0 ? x - 1 : 0;
					SendMessage(GetDlgItem(hdlg, IDC_SLCSTART), TBM_SETPOS, 1, y); 
					SetDlgItemInt(hdlg, IDC_CSTART, y, FALSE);
					pData->curveStart = y;
				}
				pData->calcCurve();
				pData->redoFrame();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			case IDC_SLALPHA1:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				pData->alpha = x;
				_snwprintf(str, 64, L"%.2lf", x / 50.0);
				SetDlgItemText(hdlg, IDC_ALPHA1, str);
				pData->calcCurve();
				pData->redoFrame();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			case IDC_SLBETA:
				x = SendMessage(h, TBM_GETPOS, 0, 0); 
				pData->beta = x;
				_snwprintf(str, 64, L"%.2lf", x / 50.0);
				SetDlgItemText(hdlg, IDC_BETA, str);
				pData->calcCurve();
				pData->redoFrame();
				InvalidateRect(hdlg, &imgRect, FALSE);
				break;
			}
			SetWindowLong(hdlg, DWLP_MSGRESULT, 0);
			editing = false;
			return TRUE;
			}
		case WM_PAINT:
			{
				if (pData->hbmp==NULL) return 0;
				if (!pData->drawn)
					pData->drawCurve();
				PAINTSTRUCT ps; 
				HDC hdc; 
				hdc = BeginPaint(hdlg, &ps); 
				HDC memDC = CreateCompatibleDC(hdc);
				SelectObject(memDC, pData->hbmp);
				BitBlt(hdc, imgRect.left, imgRect.top, 256,256, memDC, 0,0, SRCCOPY);
				EndPaint(hdlg, &ps); 
				DeleteObject(memDC);
			}
			break;
		case WM_TIMER:
			InvalidateRect(hdlg, &imgRect, FALSE);
			break;
	}//switch msg
	return 0;
}

int configProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff, VDXHWND hwndParent) 
{
    MyFilterData* pData = (MyFilterData*)fa->filter_data;
	pData->ifp = fa->ifp;
    auto res = !DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_SETTINGS), (HWND)hwndParent, SettingsDlgProc, (LPARAM)pData);
	//pData->ifp = NULL;
    return res;
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
	if (argc>=7) {
		pData->targetMin = argv[0].asInt(); pData->targetMax = argv[1].asInt();
		pData->dynamicity = argv[2].asInt(); pData->sceneThreshold = argv[3].asInt(); 
		pData->curveEnd = argv[4].asInt(); pData->alpha = argv[5].asInt();
		pData->p_mode = argv[6].asInt();
		if (argc==9) {
			pData->curveStart = argv[7].asInt();
			pData->beta = argv[8].asInt();
		} else {
			pData->curveStart = 0; pData->beta = 0;
		}
		pData->calcCurve();
	}
}

VDXScriptFunctionDef script_functions[] = {
    { (VDXScriptFunctionPtr)configScriptFunc, "Config", "0iiiiiii" },
    { NULL, NULL, NULL },
};

VDXScriptObject script_obj = { NULL, script_functions, NULL };

bool fssProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff, char *buf, int bufsize) 
{
	MyFilterData* pData = (MyFilterData*)fa->filter_data;
	_snprintf(buf, bufsize, "Config(%d, %d, %d, %d, %d, %d, %d, %d ,%d)", pData->targetMin, pData->targetMax, 
		pData->dynamicity, pData->sceneThreshold, pData->curveEnd, pData->alpha, pData->p_mode, pData->curveStart, pData->beta);
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
