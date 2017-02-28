// Hyperlinks.h
//
// Copyright 2002 Neal Stublen
// All rights reserved.
//
// http://www.awesoftware.com
//

#if defined(__cplusplus)
extern "C" {
#endif
BOOL __stdcall ConvertStaticToHyperlink(HWND hwndCtl,BOOL fEnable);
BOOL __stdcall IsEnabledStaticHyperlink(HWND hwnd);
BOOL __stdcall EnableStaticHyperlink(HWND hwnd,BOOL fEnable);
BOOL __stdcall RemoveStaticHyperlink(HWND hwnd);
#if defined(__cplusplus)
}
#endif
