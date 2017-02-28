// Hyperlinks.cpp
//
// Copyright 2002 Neal Stublen
// All rights reserved.
//
// http://www.awesoftware.com
//

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include "Hyperlinks.h"

//#define recallocfreesafe(bf,ct,ti,f4,f5) LocalReAlloc(bf,ct,LMEM_ZEROINIT)
//#define freesafe(bf,f5) LocalFree(bf)

#if defined(__cplusplus)
extern "C" {
#endif

// Enhancements:
// Remove Hyperlink at any time and not just when the control is destroyed
// Removing all hyperlinks will unsubclass the parent
// Protection for when there is no parent since the parent is required to color the text blue
// Enable/Disable which is less costly than remove/create
// protection against multiple or improper enable/disable/remove/create
// handling keyboard and focus is too much trouble. That sort of thing is why I have a big Hyperlink custom control that does everything.
// control temporairly disables it's hyperlink status when there is no text

struct _TAGHYPERLINKPARENT {
  WNDPROC pfnOrigProc;
  unsigned uRefCounter;
};

struct _TAGHYPERLINKCHILD {
  WNDPROC pfnOrigProc;
  unsigned uRefCounter;
  HFONT fntOriginal;
  HFONT fntUnderline;
  BOOL fEnabled;
  BOOL fUserEnabled;
  BOOL fHaveText; // TRUE if the control has some text in it; Enhancement: The control should check to see that the text is not all spaces.
};

// sometimes RemoveProp() returns bogus results so we never use its return value
#define PROP_DATA  TEXT("_Hyperlink_Data_")
#define GetParentPropData(hwnd) ((struct _TAGHYPERLINKPARENT *)GetProp((hwnd),PROP_DATA))
#define GetChildPropData(hwnd) ((struct _TAGHYPERLINKCHILD *)GetProp((hwnd),PROP_DATA))
#define SetParentPropData(hwnd,data) (SetProp((hwnd),PROP_DATA,(HANDLE)(struct _TAGHYPERLINKPARENT *)(data)))
#define SetChildPropData(hwnd,data) (SetProp((hwnd),PROP_DATA,(HANDLE)(struct _TAGHYPERLINKCHILD *)(data)))
#define RemoveParentPropData(hwnd) (RemoveProp((hwnd),PROP_DATA))
#define RemoveChildPropData(hwnd) (RemoveProp((hwnd),PROP_DATA))
#define GET_PARENT_PROP(hwnd) struct _TAGHYPERLINKPARENT *pparent=(struct _TAGHYPERLINKPARENT *)GetParentPropData(hwnd)
#define GET_CHILD_PROP(hwnd) struct _TAGHYPERLINKCHILD *pchild=(struct _TAGHYPERLINKCHILD *)GetChildPropData(hwnd)

static INT_PTR CALLBACK _HyperlinkParentProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

// iRef can be anything but will be treated as:
// -1: decrement counter but not below 0. delete property when the counter hits 0
//  0: set counter to 0 and delete.
// +1: increment counter, creating property if necessary
// returns the new counter value
static unsigned __stdcall AdjustRefCounter(HWND hwndParent,int iBy) {
  unsigned uRefCounter=0;
	GET_PARENT_PROP(hwndParent);
  if (hwndParent) {
    unsigned uParentRefCounter=pparent?pparent->uRefCounter:0;
    if (iBy<=0) {
      uRefCounter=uParentRefCounter;
      if (uRefCounter) --uRefCounter;
    } else {
      uRefCounter=uParentRefCounter+1;
    }
    if (uRefCounter) {
      if (uParentRefCounter==0) {
        if (!pparent) {
          pparent=(struct _TAGHYPERLINKPARENT *)LocalAlloc(LMEM_ZEROINIT,sizeof(*pparent));
          SetParentPropData(hwndParent,pparent);
        }
#undef SubclassWindow
#define     SubclassWindow(hwnd, lpfn)       \
              ((WNDPROC)SetWindowLongPtr((hwnd), GWLP_WNDPROC, (LPARAM)(WNDPROC)(lpfn)))

        pparent->pfnOrigProc=SubclassWindow(hwndParent,_HyperlinkParentProc);
      }
      pparent->uRefCounter=uRefCounter;
    } else {
      if (uParentRefCounter!=0) {
        pparent->uRefCounter=uRefCounter;
        SubclassWindow(hwndParent,pparent->pfnOrigProc); // uRefCounter can start at any value
        pparent->pfnOrigProc=NULL;
        LocalFree(pparent);
        RemoveParentPropData(hwndParent);
      }
    }
  }
  return uRefCounter;
}

static INT_PTR CALLBACK _HyperlinkParentProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	GET_PARENT_PROP(hwnd);
  WNDPROC pfnOrigProc=pparent->pfnOrigProc; // WM_DESTROY: AdjustRefCounter destroys this so we need to save it for one last use
	switch(message) {
	case WM_CTLCOLORSTATIC: {
    	GET_CHILD_PROP((HWND)lParam);
  		if (pchild && pchild->fEnabled && pchild->fHaveText) {
  			INT_PTR lr = CallWindowProc(pfnOrigProc, hwnd, message, wParam, lParam);
  			SetTextColor((HDC) wParam, RGB(0, 0, 192));
  			return lr;
  		}
    }
		break;
	case WM_DESTROY:
    AdjustRefCounter(hwnd, 0);
		break;
	}
	return CallWindowProc(pfnOrigProc, hwnd, message, wParam, lParam);
}

#ifndef SetWindowStyle
#define SetWindowStyle(hwnd,style)  ((DWORD_PTR)SetWindowLongPtr(hwnd,GWL_STYLE,(style)))
#endif

static BOOL __stdcall EnableStaticHyperlinkControl(HWND hwnd) {
  BOOL fResult=FALSE;
	GET_CHILD_PROP(hwnd);
	if (pchild) {
    BOOL fEnable=(pchild->fUserEnabled && pchild->fHaveText)?TRUE:FALSE;
    if (fEnable != pchild->fEnabled) {
      DWORD dwStyle=GetWindowStyle(hwnd);
      pchild->fEnabled=fEnable;
      if (fEnable) dwStyle |=SS_NOTIFY; else dwStyle &=~SS_NOTIFY;
      SetWindowStyle(hwnd,dwStyle);
      SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); // this forces the mouse cursor to update
      SetWindowFont(hwnd,fEnable?pchild->fntUnderline:pchild->fntOriginal,TRUE);
      //InvalidateRect(hwnd,NULL,FALSE); // the Font change does this
    }
  }
  return fResult;
}

static INT_PTR CALLBACK _HyperlinkProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	GET_CHILD_PROP(hwnd);
  WNDPROC pfnOrigProc=pchild->pfnOrigProc; // RemoveStaticHyperlink destroys pchild so we must save this
	switch (message) {
	case WM_DESTROY:
    RemoveStaticHyperlink(hwnd);
		break;
  case WM_SETTEXT:
    if (lParam && ((LPCTSTR)lParam)[0]) { // no var storage here
      if (!pchild->fHaveText) {
        pchild->fHaveText=TRUE;
        EnableStaticHyperlinkControl(hwnd);
      }
    } else {
      if (pchild->fHaveText) {
        pchild->fHaveText=FALSE;
        EnableStaticHyperlinkControl(hwnd);
      }
    }
    break;
	case WM_SETCURSOR: {
			// Since IDC_HAND is not available on all operating systems,
			// we will load the arrow cursor if IDC_HAND is not present.
			HCURSOR hCursor = LoadCursor(NULL, IDC_HAND);
			SetCursor(hCursor?hCursor:LoadCursor(NULL, IDC_ARROW));
			return TRUE;
		}
	}
	return CallWindowProc(pfnOrigProc, hwnd, message, wParam, lParam);
}

// if it is enabled but disabled because of no text it will still report enabled
BOOL __stdcall IsEnabledStaticHyperlink(HWND hwnd) {
	GET_CHILD_PROP(hwnd);
  return pchild && pchild->fUserEnabled;
}

// returns TRUE if the fEnable occured
BOOL __stdcall EnableStaticHyperlink(HWND hwnd,BOOL fUserEnable) {
	GET_CHILD_PROP(hwnd);
  if (pchild) {
    if (fUserEnable) fUserEnable=TRUE;
    if (fUserEnable != pchild->fUserEnabled) {
      pchild->fUserEnabled=fUserEnable;
      EnableStaticHyperlinkControl(hwnd);
      return TRUE;
    }
  }
  return FALSE;
}

BOOL __stdcall RemoveStaticHyperlink(HWND hwnd) {
	GET_CHILD_PROP(hwnd);
  if (pchild) {
    SubclassWindow(hwnd,pchild->pfnOrigProc); pchild->pfnOrigProc=NULL; // PROP_ORIGINAL_PROC must stay available so EnableStaticHyperlink() will work
  	EnableStaticHyperlink(hwnd,FALSE); // this uses PROP_ORIGINAL_FONT
  	DeleteObject(pchild->fntUnderline); pchild->fntUnderline=NULL;
    AdjustRefCounter(GetParent(hwnd), -1);
  	RemoveChildPropData(hwnd);
    LocalFree(pchild);
  }
  return pchild!=NULL;
}

BOOL __stdcall ConvertStaticToHyperlink(HWND hwndCtl,BOOL fEnable) {
  BOOL fResult=FALSE;
  GET_CHILD_PROP(hwndCtl);
  if (!pchild) {
  	HWND hwndParent = GetParent(hwndCtl);
  	if (NULL != hwndParent) {
      pchild=(struct _TAGHYPERLINKCHILD *)LocalAlloc(LMEM_ZEROINIT,sizeof(*pchild));
      if (pchild) {
      	LOGFONT lf;
        SetChildPropData(hwndCtl,pchild);
        pchild->pfnOrigProc =SubclassWindow(hwndCtl,_HyperlinkProc);
        pchild->fntOriginal=(HFONT) SendMessage(hwndCtl, WM_GETFONT, 0, 0); // Create an updated font by adding an underline.
        pchild->fHaveText=GetWindowTextLength(hwndCtl)?TRUE:FALSE;

      	GetObject(pchild->fntOriginal, sizeof(lf), &lf);
      	lf.lfUnderline = TRUE;

        pchild->fntUnderline=CreateFontIndirect(&lf);

        AdjustRefCounter(hwndParent, +1);
        fResult=TRUE;
    	}
    }
  }
  EnableStaticHyperlink(hwndCtl,fEnable);
	return fResult;
}
#if defined(__cplusplus)
}
#endif
