#ifndef WM_TRAYICON
#define WM_TRAYICON ( WM_USER + 1 )
#endif
#ifndef WM_TRAYICON_HIDE
#define WM_TRAYICON_HIDE ( WM_USER + 2 )
#endif

struct TrayIcon_NOTIFYICONDATA {
  int bValid;         // non zero if this has been set to something */
  HMENU hMenu;        // This is expected to be set by the user. If NULL then the internal popup menu is disabled and you need to manage your own popup menu.

  int bIconVisible;   // Do not modify or the helper functions won't show and hide the icon at the right time.
  int bHideOnRestore; // Do not modify. A setter is provided.
  int bMinimizeToTray;// Do not modify. A setter is provided.
  // I thought about adding a bCloseToTray but it would be more complicated than just doing it yourself in WM_CLOSE and SC_CLOSE
  //int nBlockMinimize; // Do not modify. This helps correct a problem with moving from the tray to the minimized position.
  NOTIFYICONDATA nid; // hIcon Do not modify, a setter is provided.
                      // ToolTip Do not modify, a setter is provided.
};

void TrayIcon_TaskbarCreated(struct TrayIcon_NOTIFYICONDATA *ptnid);
void TrayIcon_Delete(struct TrayIcon_NOTIFYICONDATA *ptnid);
void TrayIcon_Add(struct TrayIcon_NOTIFYICONDATA *ptnid);
void TrayIcon_SetActive(struct TrayIcon_NOTIFYICONDATA *ptnid,int fNewActive);
void TrayIcon_SetHideOnRestore(struct TrayIcon_NOTIFYICONDATA *ptnid,int bNewHideOnRestore);
int TrayIcon_SC_MINIMIZE(struct TrayIcon_NOTIFYICONDATA *ptnid);
int TrayIcon_SC_RESTORE(struct TrayIcon_NOTIFYICONDATA *ptnid);
void TrayIcon_SetMinimizeToTray(struct TrayIcon_NOTIFYICONDATA *ptnid,int bNewMinimizeToTray);
void TrayIcon_UpdateToolTip(struct TrayIcon_NOTIFYICONDATA *ptnid,LPCTSTR pszNewTip);
void TrayIcon_UpdateIcon(struct TrayIcon_NOTIFYICONDATA *ptnid,HICON hIcon);
void TrayIcon_InitNotifyIconData(HWND hWnd,struct TrayIcon_NOTIFYICONDATA *ptnid,UINT message,UINT uIDIcon, HICON hIcon,LPCTSTR pszTip,int bHideOnRestore,int bMinimizeToTray,HMENU hMenu);
UINT TrayIcon_Init_WinMain(int bInit);
int TrayIcon_Fix_WinMain(struct TrayIcon_NOTIFYICONDATA *ptnid,int iCmdShow,UINT messageSpecialHide);
UINT TrayIcon_Message(struct TrayIcon_NOTIFYICONDATA *ptnid,LPARAM lParam);
