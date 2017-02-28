#if defined(__cplusplus)
extern "C" {
#endif

BOOL __stdcall CreateShellLink(LPCTSTR pszShortcutFile, LPCTSTR pszFolder, LPCTSTR pszLink, LPCTSTR lpszDesc);
BOOL __stdcall AddNewGroup(LPCTSTR GroupName, LPTSTR szStartMenuPath,size_t cchStartMenuPath,BOOL fAllUsers);

#ifdef DEBUG
void __stdcall demoCreateShellLink(BOOL fInitialize);
#else
#define demoCreateShellLink(fInitialize)
#endif

#if defined(__cplusplus)
}
#endif
