/////////////////////////////////////////////
//                                         //
// Minimizing C++ Win32 App To System Tray //
//                                         //
// You found this at bobobobo's weblog,    //
// https://bobobobo.wordpress.com          //
//                                         //
// Creation date:  Mar 30/09               //
// Last modified:  Mar 30/09               //
// Last modified:  Apr 26/15 - Chris Severance //
//                                         //
/////////////////////////////////////////////

// GIVING CREDIT WHERE CREDIT IS DUE!!
// Thanks ubergeek!  http://www.gidforums.com/t-5815.html

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <tchar.h>

#include "ddmm.h"
#include "TrayIcon.h"

// Much of this functionality is patterned after the Borland Pascal and C++ Builder TTrayIcon component
// so that TrayIcons in C are as easy to use as they are in Object Pascal and C++. -CJS 2015
// http://delphi-orm.googlecode.com/svn/trunk/lib/uib/misc/UIBMonitor/TrayIcon.pas

// Add the icon to the system tray if it is expected to be visible.
// Use this on TaskbarCreated after an Explorer crash made your icons disappear.
void TrayIcon_TaskbarCreated(struct TrayIcon_NOTIFYICONDATA *ptnid) {
  if (ptnid->bValid && ptnid->bIconVisible) {
    Shell_NotifyIcon(NIM_ADD, &ptnid->nid);
    ptnid->bIconVisible=1;
  }
}

// Remove the icon from the system tray if it's there.
void TrayIcon_Delete(struct TrayIcon_NOTIFYICONDATA *ptnid) {
  if (ptnid->bValid && ptnid->bIconVisible) {
    Shell_NotifyIcon(NIM_DELETE, &ptnid->nid);
    ptnid->bIconVisible=0;
  }
}

// Add the icon to the system tray if not already there
void TrayIcon_Add(struct TrayIcon_NOTIFYICONDATA *ptnid) {
  if (ptnid->bValid && !ptnid->bIconVisible) {
    Shell_NotifyIcon(NIM_ADD, &ptnid->nid);
    ptnid->bIconVisible=1;
  }
}

void TrayIcon_SetActive(struct TrayIcon_NOTIFYICONDATA *ptnid,int fNewActive) {
  if (fNewActive) TrayIcon_Add(ptnid); 
  else TrayIcon_Delete(ptnid);
}

void TrayIcon_SetHideOnRestore(struct TrayIcon_NOTIFYICONDATA *ptnid,int bNewHideOnRestore) {
  if (ptnid->bValid && ptnid->bHideOnRestore != bNewHideOnRestore) {
    ptnid->bHideOnRestore = bNewHideOnRestore;
    if (ptnid->bMinimizeToTray) {
      if (IsWindowVisible(ptnid->nid.hWnd)) TrayIcon_SetActive(ptnid,!bNewHideOnRestore);
    } else {
      TrayIcon_Delete(ptnid);
    }
  }
}

// These next 2 helper functions are the STARS of this example.
// They perform the "minimization" function and "restore"
// functions for our window.  Notice how when you "minimize"
// the app, it doesn't really "minimize" at all.  Instead,
// you simply HIDE the window, so it doesn't display, and
// at the same time, stick in a little icon in the system tray,
// so the user can still access the application.
// Call this on SC_MINIMIZE, SC_CLOSE, and WM_CLOSE as desired
int TrayIcon_SC_MINIMIZE(struct TrayIcon_NOTIFYICONDATA *ptnid) {
  if (ptnid->bValid && ptnid->bMinimizeToTray /* && ptnid->nBlockMinimize==0 */) {
    TrayIcon_Add(ptnid);
    if (IsWindowVisible(ptnid->nid.hWnd) || IsIconic(ptnid->nid.hWnd)) {
      ShowWindow(ptnid->nid.hWnd, SW_HIDE); // ..and hide the main window
      return TRUE;
    } 
  }
  return FALSE;
}

// Basically bring back the window (SHOW IT again)
// and remove the little icon in the system tray.
// Call this on SC_RESTORE, button clicks, or menu clicks
// returns TRUE if the message was processed and you should not call DefWindowProc().  (Hidden   -> Visible)
// returns FALSE if the message was not processed and you should call DefWindowProc(). (Maximize -> Restore)
int TrayIcon_SC_RESTORE(struct TrayIcon_NOTIFYICONDATA *ptnid) {
  if (ptnid->bValid && ptnid->bMinimizeToTray /* && ptnid->nBlockMinimize==0 */) {
    if (ptnid->bHideOnRestore) TrayIcon_Delete(ptnid);
    if (IsIconic(ptnid->nid.hWnd)) {
      ShowWindow(ptnid->nid.hWnd, SW_RESTORE);
      return TRUE;
    } else if (!IsWindowVisible(ptnid->nid.hWnd)) {
      ShowWindow(ptnid->nid.hWnd, SW_SHOW); // ..and show the window
      return TRUE;
    }
  }
  return FALSE;
}

/* 
   +4 cases Windows is restored/maximized bHideOnRestore=On/Off MinimizeToTray=On/Off
   +4 cases Windows is iconic/minimized   bHideOnRestore=On/Off MinimizeToTray=On/Off
   -2   cases bHideOnRestore doesn't do anything
   Windows is hidden as trayicon
   +1 case Window was maximized/restored
   +1 case Window was iconic/minimized
   +1 case Window was iconic, moved to tray, then tray icon is clicked -> restored
   +1 case Window was iconic, moved to tray, moved out of tray back to iconic, then taskbark icon is clicked repeatedly, should go iconic, restored
     (this fails and I don't know how to fix it. Fortunately most programs aren't going to do this very much.)
   =8 cases
*/

void TrayIcon_SetMinimizeToTray(struct TrayIcon_NOTIFYICONDATA *ptnid,int bNewMinimizeToTray) {
  if (ptnid->bValid && ptnid->bMinimizeToTray != bNewMinimizeToTray) {
    BOOL bIconic=IsIconic(ptnid->nid.hWnd);
    ptnid->bMinimizeToTray = bNewMinimizeToTray;
    if (bNewMinimizeToTray) { // transition to MinimizeToTray 
      if (bIconic) {
        TrayIcon_Add(ptnid);
        ShowWindow(ptnid->nid.hWnd, SW_HIDE);
      } else {
        if (!ptnid->bHideOnRestore) TrayIcon_Add(ptnid);
      }
    } else {// transition away from MinimizeToTray 
      if (!IsWindowVisible(ptnid->nid.hWnd)) { /* I couldn't find any sequence that makes this work right */
        //if (bIconic) ShowWindow(ptnid->nid.hWnd, SW_RESTORE);
        ShowWindow(ptnid->nid.hWnd, bIconic?SW_SHOW:SW_RESTORE); // SW_MINIMIZE doesn't work any better
        //if (bIconic) SetWindowPos(ptnid->nid.hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
        //if (bIconic) {
        //  ptnid->nBlockMinimize++;
        //  SendMessage(ptnid->nid.hWnd,WM_SYSCOMMAND,SC_MINIMIZE,0);
        //  ptnid->nBlockMinimize--;
        //}
      }
      TrayIcon_Delete(ptnid);
    }
  }
}

/* This does not cause a hidden icon to become visible */
void TrayIcon_UpdateToolTip(struct TrayIcon_NOTIFYICONDATA *ptnid,LPCTSTR pszNewTip) {
  if (pszNewTip == NULL) pszNewTip=TEXT("");
  if (ptnid->bValid && lstrcmp(pszNewTip,ptnid->nid.szTip)) {
    if (pszNewTip[0]) {
      lstrcpyn(ptnid->nid.szTip, pszNewTip,NELEM(ptnid->nid.szTip));
      ptnid->nid.szTip[NELEM(ptnid->nid.szTip)-1]='\0';
      ptnid->nid.uFlags |= NIF_TIP; // we're gonna provide a tooltip as well, son.
    } else {
      ZeroMemory(ptnid->nid.szTip,sizeof(ptnid->nid.szTip)); // I'm mean!
      ptnid->nid.uFlags &= ~NIF_TIP; // or maybe we won't!
    }
    if (ptnid->bIconVisible) {
      Shell_NotifyIcon(NIM_MODIFY, &ptnid->nid);
    }
  }
}

/* This does not cause a hidden icon to become visible */
void TrayIcon_UpdateIcon(struct TrayIcon_NOTIFYICONDATA *ptnid,HICON hIcon) {
  if (ptnid->bValid) {
    if (hIcon == NULL) hIcon=LoadIcon(NULL,IDI_APPLICATION); /* an icon is required or you won't be able to quit the demo */
    if (ptnid->nid.hIcon != hIcon) {
      ptnid->nid.hIcon = hIcon;
      ptnid->nid.uFlags |= NIF_ICON; // promise that the hIcon member WILL BE A VALID ICON!!
      if (ptnid->bIconVisible) {
        Shell_NotifyIcon(NIM_MODIFY, &ptnid->nid);
      }
    }
  }
}

// Initialize the NOTIFYICONDATA structure.
// See MSDN docs http://msdn.microsoft.com/en-us/library/bb773352(VS.85).aspx
// for details on the NOTIFYICONDATA structure.
/* 
   hWnd: the window or dialog that click, menu, and other messages will be sent to. This window will be shown and hidden if you use the helper functions.
   ptnid: this is our enhanced struct, not NOTIFYICONDATA
   message: the message that will be sent for all tray icons, usually #define WM_TRAYICON (WM_USER+1)
   uIDIcon: The wParam item id to distinguish this tray icon from others
   hIcon: The initial icon for your tray icon. If your icon is invalid an ugly one will be chosen for you.
   pszTip: Tip text, can be "" or NULL for no tip
   bHideOnRestore: non zero if this icon is to be shown and hidden with the helper functions.  

The following can be set directly into the struct as desired.
   hMenu: NULL or a menu if you want to let the helper function do most of the menu
*/
void TrayIcon_InitNotifyIconData(HWND hWnd,struct TrayIcon_NOTIFYICONDATA *ptnid,UINT message,UINT uIDIcon, HICON hIcon,LPCTSTR pszTip,int bHideOnRestore,int bMinimizeToTray,HMENU hMenu) {
  ZeroMemory( ptnid, sizeof( *ptnid ) ) ;
  
  ptnid->bValid=1;
  ptnid->nid.cbSize = sizeof(ptnid->nid);
  ptnid->bHideOnRestore=bHideOnRestore;
  ptnid->bMinimizeToTray=bMinimizeToTray;
  ptnid->hMenu=hMenu;
  
  /////
  // Tie the NOTIFYICONDATA struct to our
  // global HWND (that will have been initialized
  // before calling this function)
  ptnid->nid.hWnd = hWnd;
  // Now GIVE the NOTIFYICON.. the thing that
  // will sit in the system tray, an ID.
  ptnid->nid.uID = uIDIcon;
  // The COMBINATION of HWND and uID form
  // a UNIQUE identifier for EACH ITEM in the
  // system tray.  Windows knows which application
  // each icon in the system tray belongs to
  // by the HWND parameter.
  /////
  
  /////
  // Set up flags.
  ptnid->nid.uFlags = NIF_MESSAGE; // when someone clicks on the system tray icon, we want a WM_ type message to be sent to our WNDPROC
  ptnid->nid.uCallbackMessage = message; //this message must be handled in hwnd's window procedure. more info below.
  
  // Load da icon.  Be sure to include an icon "green_man.ico" .. get one
  // from the internet if you don't have an icon
  TrayIcon_UpdateIcon(ptnid,hIcon);

  // set the tooltip text.  must be LESS THAN 64 chars
  TrayIcon_UpdateToolTip(ptnid,pszTip); 
  if (!bHideOnRestore && bMinimizeToTray) TrayIcon_Add(ptnid); /* This turns on bIconVisible */
}

// I want to be notified when windows explorer
// crashes and re-launches the taskbar.  the g_TrayIcon_WM_TBC
// event will be sent to my WndProc() AUTOMATICALLY whenever
// explorer.exe starts up and fires up the taskbar again.
// So its great, because now, even if explorer crashes,
// I have a way to re-add my system tray icon in case
// the app is already in the "minimized" (hidden) state.
// if we did not do this an explorer crashed, the application
// would remain inaccessible!!
/* call this before CreateWindow() or CreateDialog() to make g_TrayIcon_WM_TBC valid in the message loop */
UINT TrayIcon_Init_WinMain(int bInit) {
  if (bInit) {
    return RegisterWindowMessageA("TaskbarCreated") ;
  } else {
    return 0;
  }
}

/* If iCmdShow comes in as SW_MINIMIZE then the task bar icon can flash up and down. 
   This fixes iCmdShow and sends a special message that tells the window to apply 
   the tray icon immediately so we launch straight to the system tray.
   This should be tested with bMinimizeToTray on and off.
*/
int TrayIcon_Fix_WinMain(struct TrayIcon_NOTIFYICONDATA *ptnid,int iCmdShow,UINT messageSpecialHide) {
  if ((iCmdShow == SW_MINIMIZE || iCmdShow == SW_HIDE) && ptnid->bValid && ptnid->bMinimizeToTray) {
    PostMessage(ptnid->nid.hWnd,messageSpecialHide,0,0);
    iCmdShow = SW_HIDE;
  }
  return iCmdShow;
}

// the mouse button has been released.

// I'd LIKE TO do this on WM_LBUTTONDOWN, it makes
// for a more responsive-feeling app but actually
// the guy who made the original post is right.
// Most apps DO respond to WM_LBUTTONUP, so if you
// restore your window on WM_LBUTTONDOWN, then some
// other icon will scroll in under your mouse so when
// the user releases the mouse, THAT OTHER ICON will
// get the WM_LBUTTONUP command and that's quite annoying.

/*
  If you have trouble getting this message it may be because of privilege
  escalation and can be solved with ChangeWindowMessageFilter().
  https://social.msdn.microsoft.com/forums/windowsdesktop/en-US/f4370108-9cf9-4353-a086-a6f5112947ce/taskbarcreated-message
  The message is blocked by User Interface Privilege Isolation, Administrative applications that need to see it can allow it through by calling ChangeWindowMessageFilter after making sure the necessary security precautions are in place.
*/

/* return: 0=failure, 1=button clicked, #=id of menu control. SC_RESTORE automatically restores the window */
UINT TrayIcon_Message(struct TrayIcon_NOTIFYICONDATA *ptnid,LPARAM lParam) {
  if (ptnid->bValid) {
    if (lParam == WM_LBUTTONUP) {
      PostMessage(ptnid->nid.hWnd,WM_SYSCOMMAND,SC_RESTORE,0);
      return 1;
    } else if (lParam == WM_RBUTTONDOWN) { // I'm using WM_RBUTTONDOWN here because
      if (ptnid->hMenu) {
        // it gives the app a more responsive feel.  Some apps
        // DO use this trick as well.  Right clicks won't make
        // the icon disappear, so you don't get any annoying behavior
        // with this (try it out!)

        // Get current mouse position.
        POINT curPoint ;
        GetCursorPos( &curPoint ) ;

        // should SetForegroundWindow according
        // to original poster so the popup shows on top
        SetForegroundWindow(ptnid->nid.hWnd); 

        // TrackPopupMenu blocks the app until TrackPopupMenu returns
        SendMessage(ptnid->nid.hWnd, WM_INITMENUPOPUP, (WPARAM)ptnid->hMenu, MAKELPARAM(0,FALSE)); /* seems Windows doesn't send this on our own popups */
        UINT clicked = TrackPopupMenu(ptnid->hMenu,TPM_RETURNCMD | TPM_NONOTIFY, // don't send me WM_COMMAND messages about this window, instead block and return the identifier of the clicked menu item
          curPoint.x,curPoint.y,0,ptnid->nid.hWnd,NULL);
  // Original poster's line of code.  Haven't deleted it,
  // but haven't seen a need for it.
        // SendMessage(hwnd, WM_NULL, 0, 0); // send benign message to window to make sure the menu goes away.
        if (clicked == SC_RESTORE) PostMessage(ptnid->nid.hWnd,WM_SYSCOMMAND,SC_RESTORE,0);
        return clicked;
      }
    }
  }
  return 0;
}

#if 0 // 1 for demo, change your WinMain to WinMainXXX, 0 to use in your code

#define ID_TRAY_LOWEST (3000)
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  (3000)
#define ID_TRAY_CHANGE_ICON_CONTEXT_MENU_ITEM (3001)
#define ID_TRAY_CHANGE_TIP_CONTEXT_MENU_ITEM (3002)
#define ID_TRAY_CHANGE_TOGGLE_HIDE_CONTEXT_MENU_ITEM (3003)
#define ID_TRAY_CHANGE_TOGGLE_MINTRAY_CONTEXT_MENU_ITEM (3004)
#define ID_TRAY_HIGHEST (3004)

void DualMenu(HMENU hMenu,HMENU hMSys,UINT_PTR uIDNewItem,LPCTSTR lpNewItem) {
  AppendMenu(hMenu, MF_STRING, uIDNewItem,  lpNewItem);
  InsertMenu(hMSys,SC_CLOSE,MF_BYCOMMAND|MF_STRING|MF_ENABLED|MF_UNCHECKED,uIDNewItem,lpNewItem);
}

static HMENU g_hMSys;

/* Copy from here down to SetChecks into your program */
/* Then case insensitive search for TrayIcon_ and add the necessary parts to your WinMain, DialogProc or WndProc */

#define ID_TRAY_APP                     (5000)

static UINT g_TrayIcon_WM_TBC;
static int g_TrayIcon_bWM_CLOSE; /* non zero if WM_CLOSE is supposed to close the program, 0 if WM_CLOSE minimizes */
static struct TrayIcon_NOTIFYICONDATA g_nidTRAY_APP;

/* Unlike the other TrayIcon functions, this is one you copy into your program. It's named TrayIcon
   so that you can find this with all the other the places TrayIcon functions are supposed to go. */
static void TrayIcon_InitAll(HWND hwnd,int bInit) {
  if (bInit) {
    HMENU hMenu;
    g_TrayIcon_bWM_CLOSE=0;
    // create the menu once.
    // oddly, you don't seem to have to explicitly attach
    // the menu to the HWND at all.  This seems so ODD.
    hMenu=CreatePopupMenu();
    g_hMSys=GetSystemMenu(hwnd,FALSE);
    DualMenu(hMenu, g_hMSys, SC_RESTORE,  TEXT( "Restore" ) );
    DualMenu(hMenu, g_hMSys, ID_TRAY_CHANGE_TOGGLE_MINTRAY_CONTEXT_MENU_ITEM,  TEXT( "Toggle Min to tray") );
    DualMenu(hMenu, g_hMSys, ID_TRAY_CHANGE_TOGGLE_HIDE_CONTEXT_MENU_ITEM,  TEXT( "Toggle Hide Icon" ) );
    DualMenu(hMenu, g_hMSys, ID_TRAY_CHANGE_ICON_CONTEXT_MENU_ITEM,  TEXT( "Change Icon" ) );
    DualMenu(hMenu, g_hMSys, ID_TRAY_CHANGE_TIP_CONTEXT_MENU_ITEM,  TEXT( "Change Tool Tip" ) );
    DualMenu(hMenu, g_hMSys, ID_TRAY_EXIT_CONTEXT_MENU_ITEM,  TEXT( "Exit" ) );
    g_hMSys=NULL;
    // Initialize the NOTIFYICONDATA structure
    TrayIcon_InitNotifyIconData(hwnd,&g_nidTRAY_APP,WM_TRAYICON,ID_TRAY_APP,LoadImage( NULL, TEXT("ddmm_normal.ico"), IMAGE_ICON, 0, 0, LR_LOADFROMFILE  ),TEXT("Green man.. here's looking at ya!"),1,1,hMenu);
  } else {
    TrayIcon_Delete(&g_nidTRAY_APP);
  }
}

static void SetChecks(HMENU hMenu) {
  CheckMenuItem(hMenu,ID_TRAY_CHANGE_TOGGLE_MINTRAY_CONTEXT_MENU_ITEM,MF_BYCOMMAND|(g_nidTRAY_APP.bMinimizeToTray?MF_CHECKED:MF_UNCHECKED));
  CheckMenuItem(hMenu,ID_TRAY_CHANGE_TOGGLE_HIDE_CONTEXT_MENU_ITEM,MF_BYCOMMAND|(g_nidTRAY_APP.bHideOnRestore?MF_CHECKED:MF_UNCHECKED));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  UINT traymessage;
  if (message==g_TrayIcon_WM_TBC) { // this is a variable so can't go in a case statement
    TrayIcon_TaskbarCreated(&g_nidTRAY_APP);
  } else switch (message) {
  case WM_CREATE:
    TrayIcon_InitAll(hwnd,1);
    break;
  
  case WM_DESTROY:
    printf( "DESTROY!!\n" ) ;
    // Once you get the quit message, before exiting the app,
    // clean up and remove the tray icon
    TrayIcon_InitAll(hwnd,0);
    PostQuitMessage(0);
    break;

  case WM_TRAYICON_HIDE:
    TrayIcon_SC_MINIMIZE(&g_nidTRAY_APP);
    return 0; // return TRUE for dialogs
  case WM_SYSCOMMAND:
    switch( wParam & 0xfff0 ) {  // (filter out reserved lower 4 bits:  see msdn remarks http://msdn.microsoft.com/en-us/library/ms646360(VS.85).aspx)
    //case SC_CLOSE: goto close;   // Alt+F4 or close is selected in the corner box. It isn't necessary to process this message since it turns into a WM_CLOSE unless you want the corner box to behave differently
    case SC_RESTORE: 
      if (TrayIcon_SC_RESTORE(&g_nidTRAY_APP)) return 0; // return TRUE for dialogs
      break; // DefWindowProc() so Maximize -> Restore works properly.
    case SC_MINIMIZE: 
      if (TrayIcon_SC_MINIMIZE(&g_nidTRAY_APP)) return 0; // return TRUE for dialogs
      break;
    default: 
      if (wParam >= ID_TRAY_LOWEST && wParam <= ID_TRAY_HIGHEST) {
        traymessage=wParam; 
        goto trayicon;
      }
      break;
    }
    break;
  case WM_CLOSE:
//close:
    printf( "Intercept the WM_CLOSE message to do the same as minimize!\n" ) ;
    if (g_TrayIcon_bWM_CLOSE==0 && TrayIcon_SC_MINIMIZE(&g_nidTRAY_APP)) return 0;
    break;
  // Our user defined WM_TRAYICON message.
  // We made this message up, and we told
  // the tray tool to message us when clicked.
  case WM_TRAYICON:
    printf( "Tray icon notification, from %d\n", wParam ) ;
   
    switch(wParam) {
    case ID_TRAY_APP:
      printf( "Its the ID_TRAY_APP.. one app can have several tray icons, ya know..\n" ) ;
      /* handle standard click and menu. Copy the function if you want more advanced actions. */
      printf("calling track or Let's get contextual.  I'm showing you my context menu.\n" ) ;
      traymessage=TrayIcon_Message(&g_nidTRAY_APP,lParam);
trayicon:
      switch (traymessage) {
      case 1: 
        printf( "You have restored me!\n" ); 
        break;
      case ID_TRAY_CHANGE_ICON_CONTEXT_MENU_ITEM: {
          HICON hIcon=LoadIcon(NULL,IDI_HAND);
          if (hIcon == g_nidTRAY_APP.nid.hIcon) hIcon=LoadIcon(NULL,IDI_EXCLAMATION);
          TrayIcon_UpdateIcon(&g_nidTRAY_APP,hIcon);
        }
        break;
      case ID_TRAY_CHANGE_TIP_CONTEXT_MENU_ITEM:
        TrayIcon_UpdateToolTip(&g_nidTRAY_APP,TEXT("Enhanced by Chris Severance 2015"));
        break;
      case ID_TRAY_CHANGE_TOGGLE_HIDE_CONTEXT_MENU_ITEM:
        TrayIcon_SetHideOnRestore(&g_nidTRAY_APP,!g_nidTRAY_APP.bHideOnRestore);
        break;
      case ID_TRAY_CHANGE_TOGGLE_MINTRAY_CONTEXT_MENU_ITEM:
        TrayIcon_SetMinimizeToTray(&g_nidTRAY_APP,!g_nidTRAY_APP.bMinimizeToTray);
        break;
      case ID_TRAY_EXIT_CONTEXT_MENU_ITEM:
        printf("I have posted the close message, biatch\n");
        g_TrayIcon_bWM_CLOSE=1; // Block our special WM_CLOSE handler so that the Window closes properly. PostQuitMessage(0) here prevents WM_DESTROY.
        PostMessage(hwnd,WM_CLOSE,0,0);
        break;
      }
      break;
    }
    break; /* WM_TRAYICON */
  case WM_INITMENU:
    if (g_hMSys==NULL && -1!=CheckMenuItem((HMENU)wParam,ID_TRAY_CHANGE_TOGGLE_MINTRAY_CONTEXT_MENU_ITEM,MF_BYCOMMAND|MF_UNCHECKED)) {
      g_hMSys=(HMENU)wParam; // detect modified System Menu since Windows gives me a different one than I had before
    }
    if ((HMENU)wParam == g_hMSys) {
      SetChecks((HMENU)wParam);
    }
    break;
  case WM_INITMENUPOPUP:
    if ((HMENU)wParam == g_nidTRAY_APP.hMenu) {
      SetChecks((HMENU)wParam);
    }
    break;
  // intercept the hittest message.. making full body of
  // window draggable. Just a cutsie, nothing to do with tray icons.
  case WM_NCHITTEST: {
// http://www.catch22.net/tuts/tips
// this tests if you're on the non client area hit test
    UINT uHitTest = DefWindowProc(hwnd, WM_NCHITTEST, wParam, lParam);
    if (uHitTest == HTCLIENT) uHitTest=HTCAPTION;
#if 1 /* for a window */
      return uHitTest;
#else /* for a dialog */
      SetWindowLongPtr(hwnd,DWL_MSGRESULT,uHitTest);
      return TRUE;
#endif
    }
  }
  return DefWindowProc( hwnd, message, wParam, lParam ) ;
}

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR args, int iCmdShow ) {
  static TCHAR className[] = TEXT( "tray icon class" );
  WNDCLASSEX wnd;
  ZeroMemory(&wnd,sizeof(wnd));

  wnd.hInstance = hInstance;
  wnd.lpszClassName = className;
  wnd.lpfnWndProc = WndProc;
  wnd.style = CS_HREDRAW | CS_VREDRAW ;
  wnd.cbSize = sizeof (WNDCLASSEX);

  wnd.hIcon = LoadIcon (NULL, IDI_APPLICATION);
  wnd.hIconSm = LoadIcon (NULL, IDI_APPLICATION);
  wnd.hCursor = LoadCursor (NULL, IDC_ARROW);
  wnd.hbrBackground = (HBRUSH)COLOR_APPWORKSPACE ;
  
  // add a console, because I love consoles.
  // A console lets you close the program when
  // the normal code is malfunctioning.
  // To avoid the console, just comment out
  // the next 3 lines of code. 
#if 0
  AllocConsole();
  AttachConsole( GetCurrentProcessId() ) ;
  freopen( "CON", "w", stdout ) ;
#endif

  if (!RegisterClassEx(&wnd)) {
    FatalAppExit( 0, TEXT("Couldn't register window class!") );
    return 255; /* FatalAppExit never returns so this will never happen but the compiler doesn't know that. This placates the compiler */
  } else {
    MSG msg ;
    HWND hwnd;
    
    g_TrayIcon_WM_TBC = TrayIcon_Init_WinMain(1); // goes before CreateWindow

    hwnd = CreateWindowEx(0, className,TEXT( "Using the system tray" ),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT, CW_USEDEFAULT, 400, 400, NULL, NULL, hInstance, NULL);

    // Add the label with instruction text
    CreateWindow( TEXT("static"), TEXT("right click the system tray icon to close"), WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 400, 400, hwnd, 0, hInstance, NULL ) ;

    //iCmdShow = SW_MINIMIZE;
    ShowWindow (hwnd, TrayIcon_Fix_WinMain(&g_nidTRAY_APP,iCmdShow,WM_TRAYICON_HIDE));

    while(GetMessage (&msg, NULL, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    TrayIcon_Init_WinMain(0);

    return msg.wParam;
  }
}
#endif
