#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <tchar.h>
#include "ShellLink.H"
#include "ddmm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/***********************************************
//pulled from msdn and debugged by Fallenhobit
//debugged as in, their code would not compile
//I got it too after alot of cleanup....and a few
//changes....and now...com the hard way
***********************************************/

// DDEML looks harder than this
// http://www.experts-exchange.com/Programming/System/Windows__Programming/Q_20327893.html

//creates a shell link...in this example a Start Group item
// pszFolder comes from AddNewGroup()
// .lnk will be added if it isn't there already
BOOL __stdcall CreateShellLink(LPCTSTR pszShortcutFile, LPCTSTR pszFolder, LPCTSTR pszLink, LPCTSTR lpszDesc) {
  IShellLink* psl;
#if defined(__BORLANDC__) || defined(__MINGW32__) || (defined(_MSC_VER) && defined(_WIN64) && !defined(__POCC__))
#define AMPER
#define VTABLE(p,f) (p)->f(
#define VTABLEV(p,f) (p)->f()
#else
#define AMPER &
#define VTABLE(p,f) (p)->lpVtbl->f((p),
#define VTABLEV(p,f) (p)->lpVtbl->f((p))
#endif
  HRESULT hres = CoCreateInstance(AMPER CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, AMPER IID_IShellLink, (LPVOID *)&psl); // Get a pointer to the IShellLink interface.
  if (SUCCEEDED(hres)) {
    TCHAR szLinkPath[MAX_PATH];
    size_t cchLink=_tcslen(pszLink);
    const TCHAR *szLnkExt=(cchLink<=4 || lstrcmpi(pszLink+cchLink-4,_T(".lnk")))?_T("lnk"):NULL;
    if (_tcscatPath(szLinkPath,NELEM(szLinkPath),pszFolder,pszLink,szLnkExt,NULL)) {
      IPersistFile* ppf;
      if (szLnkExt) szLinkPath[_tcslen(szLinkPath)-4]=_T('.');
      hres = VTABLE(psl,QueryInterface) AMPER IID_IPersistFile, (LPVOID *) &ppf); // Query IShellLink for the IPersistFile interface for saving the shortcut in persistent storage.
      if (SUCCEEDED(hres)) {
  #ifdef UNICODE
  #define szLinkPathW szLinkPath
  #else
        WCHAR szLinkPathW[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, szLinkPath, -1, szLinkPathW, NELEM(szLinkPathW)); // Ensure that the string is UNICODE.
  #endif
        VTABLE(psl,SetPath) pszShortcutFile); // Set the path to the shortcut target, and add the description.
        VTABLE(psl,SetDescription) lpszDesc);
        hres = VTABLE(ppf,Save) szLinkPathW, TRUE); // Save the link by calling IPersistFile::Save.
        VTABLEV(ppf,Release);
      }
      VTABLEV(psl,Release);
    }
  }
  return SUCCEEDED(hres);
}

//adds a program group to the start menu, specify NULL or "" for no program group
BOOL __stdcall AddNewGroup(LPCTSTR GroupName, LPTSTR szStartMenuPath,size_t cchStartMenuPath,BOOL fAllUsers) {
  BOOL fResult=FALSE;
  if (cchStartMenuPath>=MAX_PATH) {
    LPITEMIDLIST pidlStartMenu;
    fResult=
    (SHGetSpecialFolderLocation( HWND_DESKTOP, fAllUsers?CSIDL_COMMON_PROGRAMS:CSIDL_PROGRAMS, &pidlStartMenu)==NOERROR) && // get the pidl for the start menu
     SHGetPathFromIDList( pidlStartMenu, szStartMenuPath);
    if (fResult && GroupName && GroupName[0]) {
      //_tcscat(szStartMenuPath, _T("\\"));
      //_tcscat(szStartMenuPath, GroupName);
      fResult=_tcscatPath(szStartMenuPath,cchStartMenuPath,NULL,GroupName,NULL);
      if (fResult) {
        fResult=CreateDirectory(szStartMenuPath, NULL);
        if (fResult) SHChangeNotify( SHCNE_MKDIR, SHCNF_FLUSH | SHCNF_PATH, szStartMenuPath, NULL); // notify the shell that you made a change
        if (!fResult && ERROR_ALREADY_EXISTS==GetLastError()) fResult=TRUE;
      }
    }
  }
  return fResult;
}

#ifdef DEBUG
/***********************************************
//Author: Fallenhobit
//Date: 08/12/03
//Purpose: Generates a Start Menu Group and
//Group Item
***********************************************/
void __stdcall demoCreateShellLink(BOOL fInitialize) {
	TCHAR szStartMenuPath[MAX_PATH];
	if (fInitialize) CoInitialize(NULL);
  if(!AddNewGroup(_T("Whiz Bang"), szStartMenuPath,NELEM(szStartMenuPath),TRUE)) {
		MessageBox(NULL,_T("ERROR"),_T("ERROR"),MB_OK);
		return;
	}
  if (!CreateShellLink(_T("C:\\Program Files\\Project1\\Whizbang.exe"), szStartMenuPath, _T("\\Whiz Bang Deluxe.lnk"), _T("My Whiz Bang Program"))) {
		MessageBox(NULL,_T("Error Creating Program Link"),_T("ERROR Beeeyatch"),MB_OK);
		return;
	}
	if (fInitialize) CoUninitialize();
}
#endif

#if defined(__cplusplus)
}
#endif
