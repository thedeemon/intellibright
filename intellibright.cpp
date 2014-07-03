// intellibright.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <vd2/plugin/vdvideofilt.h>
#include <vector>

struct MyFilterData {
	std::vector<int> lastColors;
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
	BYTE tbl[256];
	bool work = false;
	if (maxBr > minBr && (maxBr - minBr) < 255) {
		k = 255.0 / (maxBr - minBr);
		work = true;
		for(int i=0;i<256;i++) {
			int v = (i - minBr) * k + 0.5;
			tbl[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
		}
	} 

    dst = fa->dst.data;
    src = (const uint32 *)fa->src.data;

	if (work) {
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
    return 0;
}

int startProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff)
{
	return 0;
}

int endProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff)
{
	return 0;
}

int configProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff, VDXHWND hwndParent) 
{
	return 0;
}

static void stringProc2(const VDXFilterActivation *fa, const VDXFilterFunctions *ff, char *buf, int maxlen) 
{
	MyFilterData* pData = (MyFilterData*)fa->filter_data;

	//_snprintf(buf, maxlen, " (%dx%d, %d)", pData->dx, pData->dy, pData->GetQuality() );
}

void stringProc(const VDXFilterActivation *fa, const VDXFilterFunctions *ff, char *buf)
{
	stringProc2(fa, ff, buf, 80);
}

void configScriptFunc(IVDXScriptInterpreter *isi, void *lpVoid, VDXScriptValue *argv, int argc) 
{
}

VDXScriptFunctionDef script_functions[] = {
    { (VDXScriptFunctionPtr)configScriptFunc, "Config", "0iii" },
    { NULL, NULL, NULL },
};

VDXScriptObject script_obj = { NULL, script_functions, NULL };

bool fssProc(VDXFilterActivation *fa, const VDXFilterFunctions *ff, char *buf, int bufsize) 
{
	return false;
}

static struct VDXFilterDefinition myfilter_definition={
    0,0,NULL,                       // next, prev, and module (set to zero)
    "Intelligent Brightness & Contrast",                    // name
    "Smoothly adjusts brightness & contrast to desired levels.",    // description
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
