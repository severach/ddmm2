#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <wchar.h>
#include <tchar.h>

// http://stackoverflow.com/questions/5427673/how-to-run-a-program-automatically-as-admin-on-windows-startup
// TODO: Launch as administrator on Win7 startup via Scheduled Task or service. ddmm as user doesn't get mouse events when an elevated window is active.
// Possible bug: Why is minimize so slow when mouse management is active.
// TODO: Fix ShellLink with "Start in" path, and target with parameters
// TODO: Check 64 bit compile
// TODO: Implement or remove Hide Tray Option
// TODO: "About" in tray menu
// TODO: EmergencyRestore from ddmm, at least figure out why it's needed.
// TODO: Tray Icon animation
// TODO: Unify variable + define names
// TODO: Reclip region every second.
// TODO for TrayTool: Built in system menu copy tool

/*
  Enhancement: Pure C and Win32. No more strange DLL.
  Bug Fix: Support looked like 3 monitors but more than 2 didn't work. Now supports 16 monitors, which like 640K, ought to be enough for anybody. If not, it's easy to change.
  Enhancement: Faster reapplication of clip region after Windows takes it away.
  Enhancement: Z motion crossing.
  Enhancement: Ctrl+Alt+~ to teleport to primary monitor.
  Bug Fix: Mouse position scaled so Ctrl-~ teleports to same place on each screen.
  Enhancement: Native 64 bit.
  Enhancement: Self disables with single monitor. Reenables again when 2+ monitors appear. Hot plug monitor ready.
  Mouse control released on drag.
  Not implemented: adjustable monitor size
  Not implemented: outline preview
  Enhancement: Mouse re-clipping may be performed on every mouse motion. (see original code)
    It's rewritten in pure C now so you can enable that global hook.
  Enhancement: minimized start is by command line switch
  Enhancement: Manifest for version info.
*/

#include "resource.h"
#include "ddmm.h"
#include "ShellLink.h"
#include "Hyperlinks.h"
#include "TrayIcon.h"

// returns TRUE if the entire path fit
// Slashes will be automatically added when necessary. path parts should NOT begin with a slash unless multiple slashes are desired.
// if szPath1==NULL then the existing szDest starts the path
// "" can be specified to force a path member to end with a slash without appending anything.
BOOL __cdecl _tcscatPath(TCHAR *szDest,size_t cchDestSize,const TCHAR * restrict szPath1,...) {
  BOOL Result=FALSE;
  if (cchDestSize) {
    _TCHAR *szEnd0=szDest+cchDestSize-1; // szEnd0 is the last possible position of \0
    const _TCHAR *szNext=szPath1;
    _TCHAR *szDestPos=szDest;
    va_list ap;
    va_start(ap,szPath1);
    if (!szNext) {
      szDestPos=szDest+_tcslen(szDest);
      if (szDestPos>=szEnd0) goto done; else goto midtest;
    }
    do {
      do {
        if (szDestPos>=szEnd0) goto done;
        if (!*szNext) break;
        *szDestPos = *szNext;
        ++szDestPos;
        ++szNext;
      } while(1);
midtest:
      szNext=va_arg(ap,_TCHAR *);
      if (!szNext) break;
      if (szDestPos>szDest && szDestPos[-1]!=_T('\\')) {
        *szDestPos=_T('\\'); // the path must end in a slash
        ++szDestPos;
      }
    } while(1);
    Result=TRUE;
done:
    va_end(ap);
    *szDestPos=_T('\0');
  }
  return Result;
}

/* While writing a modeless dialog program there were many accidental occurances where doing something while stopped in the debugger
   would terminate the window with WM_CLOSE, DestroyWindow(), and WM_DESTROY but the message loop wouldn't terminate
   on PostQuitMessage() and the process would stay running which requires a manual kill from Task Manager.
   Experimenting showed that the user actions can be simulated in two simple steps:
     PostMessage(hDlg,WM_CLOSE,0,0);
     ShellExecute(hDlg,NULL, _T("http://www.google.com/"), NULL, NULL, SW_SHOW);
   That is to say, a WM_CLOSE is placed on the message queue before ShellExecute, say because the user hits close
   on a window that is suspended in the debugger then continues execution to the ShellExecute.
   Somehow the DestroyWindow() occurs before ShellExecute has completed something which blocks
   PostQuitMessage(). The message queue is definitely destroyed since a hanging SetTimer() never fires.

   There is no problem with a WM_CLOSE so long as it comes long after the ShellExecute has completed which
   is why noone ever sees this problem. They click the link to go to the website and some time later click close.

   There are many round-a-bout solutions to the problem all of which involve time. One way is to
   block and detect WM_CLOSE, then permit the WM_CLOSE to proceed after a SetTimer()>100ms. Unfortunately this
   requires code in a lot of places to support this method and there's no reason to believe that
   100ms applies to all situations. Some situations on low memory computers could be several seconds.

   A solution was discovered accidentaly with ShellExecuteURL posted on MSDN which calls ShellExecuteEx().
   The problem depended on which flags were selected.
   Figuring that ShellExecute can't be any more than a set up call to ShellExecuteEx I traced through and discovered that the only difference was in the
   choice of flags. ShellExecute chooses SEE_MASK_NOASYNC under some conditions and the sample code didn't.

   MSDN says: Also, applications that exit immediately after calling ShellExecuteEx should specify SEE_MASK_NOASYNC .

   Sounds like this is the flag for us but it seems that exiting immediately is precisely what is causing the problem
   so I had to rewrite ShellExecute to eliminate SEE_MASK_NOASYNC to eliminate the problem.

   The URL's do launch asynchronously but it does take a little longer because we must wait for ShellExecute to finish
   launching the browser asynchronously or sending the DDE message.

   This launches URL's correctly in Windows 2000, XP, and Vista; Firefox and IE, loading new or DDE into existing.

   Technical Notes: http://support.microsoft.com/kb/224816 http://blogs.msdn.com/oldnewthing/archive/2004/11/26/270710.aspx

   When we launch programs with CreateProcess, CreateProcess creates the process but does not launch the program
   until a synchronous WaitForSingleObject() or asynchronous CloseHandle() is seen. Stepping across a ShellExecute(SEE_MASK_NOASYNC)
   we see the same behavior. The call to ShellExecute() creates the process but does not launch the program
   until we return to our message loop and apparently only after the message loop is idle since the problem
   occurs no matter how late on the queue I place the WM_CLOSE. That way it appears
   to us that the launch has been asynchronous when really ShellExecute is watching our message loop so
   that it can complete the process when we are not busy. If we
   destroy our loop too soon in some cases the CloseHandle() never gets run and though our dialog is fine we get a non functional
   orphaned browser in a new window. In other cases ShellExecute() destroys the PostQuitMessage() so our window disappears but
   our message loop never terminates which leaves our process orphaned. A call to our non async ShellExecute() returns
   only when the new process is complete. This takes a small amount of time on a fast machine but it completely
   eliminates the need for ShellExecute to watch and damage our message loop to perform the process fixup.

   It is quite clear that the meaning of SEE_MASK_NOASYNC has been reversed and it specifies that the task is to be executed
   asynchronously since with that flag the single stepping shows the same results as it would with CreateProcess() and CloseHandle()
   and without the flags single stepping shows the same results as CreateProcess() and WaitForSingleObject().
   As a result MSDN should read "...applications that exit immediately after calling ShellExecuteEx should [not] specify this [SEE_MASK_NOASYNC] flag."
   Because of this mistake SEE_MASK_NOASYNC creates the very problem it's trying to avoid and ShellExecute is permanently damaged because of it because
   the improper behavior is now depended on.

   This problem only occurs with modeless dialog procs. A modal dialog proc must have extra code in EndDialog to handle this
   since this problem does not occur.

   Vista seems to have solved the problem by delaying the CreateProcess to the idle message loop or terminating the created process as our process goes down.
   All options destroy all windows and processes without any orphans.
*/

#ifndef SEE_MASK_NOASYNC
#define SEE_MASK_NOASYNC (SEE_MASK_FLAG_DDEWAIT)
#endif
#define SEE_MASK_UNDOCUMENTED_FLAG_x1000 (0x1000)
#define UNKNOWN_FUNCTION_CALL (0)
static HINSTANCE ShellExecuteNoAsync(HWND hwnd,LPCTSTR lpOperation,LPCTSTR lpFile,LPCTSTR lpParameters,LPCTSTR lpDirectory,INT nShowCmd) {
  SHELLEXECUTEINFO ExecuteInfo;
  ZeroMemory(&ExecuteInfo,sizeof(ExecuteInfo)); // Microsoft does this after the values have been put in which requires insane code to duplicate and saves nothing
  ExecuteInfo.fMask = 0;
  ExecuteInfo.hInstApp =0;
  ExecuteInfo.hwnd = hwnd;
  ExecuteInfo.lpVerb = lpOperation;
  ExecuteInfo.lpFile = lpFile;
  ExecuteInfo.lpParameters = lpParameters;
  ExecuteInfo.lpDirectory = lpDirectory;
  ExecuteInfo.nShow = nShowCmd;

  ExecuteInfo.cbSize = sizeof(ExecuteInfo);
  //ExecuteInfo.fMask = UNKNOWN_FUNCTION_CALL?(SEE_MASK_UNDOCUMENTED_FLAG_x1000|SEE_MASK_FLAG_NO_UI):(SEE_MASK_UNDOCUMENTED_FLAG_x1000|SEE_MASK_FLAG_NO_UI|SEE_MASK_NOASYNC);
  ExecuteInfo.fMask = SEE_MASK_UNDOCUMENTED_FLAG_x1000|SEE_MASK_FLAG_NO_UI;
  ShellExecuteEx(&ExecuteInfo); // SEE_MASK_NOCLOSEPROCESS is even worse
  return ExecuteInfo.hInstApp;
}

#define MONITORS (16)

static TCHAR g_szINIFile[MAX_PATH];

static unsigned g_nScreens;     /* The count of screens into the list. */
static RECT g_rectScreens[MONITORS]; /* The list of screens */
static int g_nActiveScreen;     /* 0=no active screen, 1..MONITORS the active screen, always subtract 1 from this */
static RECT g_rectMaxClipCursor;/* maximum extents of monitor so we can release clipping */
static unsigned g_nClipCounter; /* counter for Dialog debug item */
static unsigned g_nTimeCounter; /* counter for Dialog debug item */

static HWND g_hwndActiveScreen; /* Dialog debug item */
static HWND g_hwndScreens;      /* Dialog debug item */
static HWND g_hwndDialog;        /* The dialog for functions where it's too hard to pass the dialog */
static HWND g_hwndWasClipRegion; /* Dialog debug item */
static HWND g_hwndClipRegion;    /* Dialog debug item */
static HWND g_hwndClipCounter;   /* Dialog debug item */
static HWND g_hwndCoords;        /* Dialog debug item */
static HWND g_hwndTimerCounter;  /* Dialog debug item */

#define HUNDRED_EDGE (100)      /* We define a border around the edge of every screen in which you must come at the edge at a shallow angle within this edge */
//static int g_bHundredLockOut;     /* 1 if the angle was too steep forcing the user to go back out beyond the hundred mark to try again */
static HWND g_hwndHundredAngle;     /* Dialog debug item, shows angle fraction from hundred point to cursor */
static HWND g_hwndHundredPoint;     /* Dialog debug item, shows the hundred point, stops when hundred point is selected */
static int g_bHundredPoint;         /* 1 if we have recorded a hundred point. 0 if no point or locked out. */
static int g_nHundredFirstQuadrant; /* quadrant number of where mouse first entered hundred edge. The hundred lock is enabled if the user slides along the edge into a different quadrant. */
#define HUNDRED_QUAD_NONE   (0) /* \ T / */
#define HUNDRED_QUAD_LEFT   (1) /*  \ /  */
#define HUNDRED_QUAD_TOP    (2) /* L X R */
#define HUNDRED_QUAD_RIGHT  (3) /*  / \  */
#define HUNDRED_QUAD_BOTTOM (4) /* / B \ */
static TCHAR g_szQuadrant[]=TEXT("X\0L\0T\0R\0B");
#define HUNDRED_MINANGLE (3)
static POINT g_ptHundred;           /* This is the hundred point if we have it */

static int g_dlg_bActivateProgram =1; /* 0 if dialog says all functions disabled */
static int g_dlg_bMethod_CtrlKey  =1; /* 1 if dialog says control releases the clip region */
static int g_dlg_bMethod_CtrlTilde=1; /* 1 if dialog says control tilde jumps between regions */
static int g_dlg_bDebug           =0; /* 1 if dialog says show debug information, was UsePreview */
static int g_dlg_bMethod_Delay    =1; /* 1 if dialog says to allow border crossing after delay */
static int g_dlg_bMethod_Delay_Time=150;
static int g_dlg_bMethod_ZMotion  =0; /* 1 if dialog says to restrict border crossing to Z motion */

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
  TCHAR sz[64];
  if (g_nScreens<MONITORS) {
    memcpy(g_rectScreens+g_nScreens,lprcMonitor,sizeof(*lprcMonitor)); // memcpy is inline where possible in Pelles C
    g_nScreens++;
    wsprintf(sz,TEXT("%d: (%d,%d)-(%d,%d)"),g_nScreens,lprcMonitor->left,lprcMonitor->top,lprcMonitor->right,lprcMonitor->bottom);
    SendMessage(g_hwndScreens, LB_ADDSTRING, 0, (LPARAM) sz);
    return TRUE;
  } else {
    return FALSE;
  }
}

static void ClearAllDebug(void) {
  SetWindowText(g_hwndActiveScreen,TEXT(""));
  SetWindowText(g_hwndClipRegion,TEXT(""));
  SetWindowText(g_hwndCoords,TEXT(""));
  SetWindowText(g_hwndHundredPoint,TEXT(""));
  SetWindowText(g_hwndHundredAngle,TEXT(""));
  SetWindowText(g_hwndClipCounter,TEXT(""));
  SetWindowText(g_hwndTimerCounter,TEXT(""));
}

static void InitMEPData(int bInit) {
  if (bInit) {
    g_nScreens=0;
    g_nActiveScreen=0;
    if (g_dlg_bDebug) SetWindowText(g_hwndActiveScreen,TEXT("s0"));
    g_nClipCounter=0;
    g_nTimeCounter=0;
    g_bHundredPoint=0;
    GetClipCursor(&g_rectMaxClipCursor);
    SendMessage(g_hwndScreens, LB_RESETCONTENT, 0, 0);
    ClearAllDebug();
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
    if (g_nScreens<=1) {
      SendMessage(g_hwndScreens, LB_ADDSTRING, 0, (LPARAM)TEXT("Disabled, we require more monitors"));
    }
  } else {
    ClipCursor(&g_rectMaxClipCursor); /* The active window change when our program quits will fix this if broken */
  }
}

static UINT_PTR g_idDelayTimer;

static unsigned g_nBlockClip=0; // 0 if clipping is permitted, non zero if blocked

static int g_bCursorAlreadyTouchingEdge=0;
static int g_nAngleFirstTouchedEdge;
static POINT g_ptCursorFirstTouchedEdge;
static int g_bTemporaryClipRelease; /* 1 if the clip region is released to permit screen changes */
static int g_bTemporaryDragRelease; /* 1 if the clip region is released due to drags. Otherwise dragged windows can't be moved between screens */
#define CURSOR_Z_JIFFIES (150)      /* The amount of perpendicular Z motion required to cross a screen border */
#define CURSOR_CORNER_JIFFIES (150) /* We never cross screen borders this close to any corner */

#define IDT_TIMER_DELAY (10000)
static void SetDelayTimer(int bSet) {
  // processes "cursor on the edge of screen" event (unclipping)
  if (bSet) {
    if (g_idDelayTimer == 0) {
      if (g_dlg_bMethod_Delay && g_dlg_bMethod_Delay_Time>50) { // if mouse is clipped and crossing method is "after delay on the border of the screen"
        g_idDelayTimer=SetTimer(g_hwndDialog,IDT_TIMER_DELAY,g_dlg_bMethod_Delay_Time,NULL); // start unclipping timer: we will unclip if timer reaches tick
        g_nTimeCounter++;
        if (g_nTimeCounter>255) g_nTimeCounter=0;
        if (g_dlg_bDebug) {
          TCHAR sz[64];
          wsprintf(sz,TEXT("t%u"),g_nTimeCounter);
          SetWindowText(g_hwndTimerCounter,sz);
        }
      }
    }
  } else {
    if (g_idDelayTimer) {
      KillTimer(g_hwndDialog,g_idDelayTimer);
      g_idDelayTimer=0;
    }
  }
}

static void ProcDelayTimer(HWND hDlg) {
  ClipCursor(&g_rectMaxClipCursor);
  g_nActiveScreen=0;
  SetDelayTimer(0);
}

static void SetDelayTimerText(HWND hDlg) {
  TCHAR sz[64];
  wsprintf(sz,TEXT("%u"),g_dlg_bMethod_Delay_Time);
  SetWindowText(GetDlgItem(hDlg,IDC_ED_DELAY_CROSS),sz);
}

#define PT_STICKY (5) // once you touch the edge we'll consider you on the edge so long as you're within a few pixels
static inline BOOL PtOnRect(const RECT *lprc,LONG X, LONG Y, int bSticky) {
  if (bSticky) {
    return X <= lprc->left+PT_STICKY || X >= lprc->right-1-PT_STICKY || Y == lprc->top+PT_STICKY || Y == lprc->bottom-1-PT_STICKY;
  } else {
    return X == lprc->left || X == lprc->right-1 || Y == lprc->top || Y == lprc->bottom-1;
  }
}

/* This math does not correct for non square screens. I can't think of a non float way to scale the longer edge to the shorter edge. */
static unsigned GetQuadrant(LPRECT lprect,LONG X,LONG Y) {
  int dLeft  =X-lprect->left;
  int dTop   =Y-lprect->top;
  int dRight =lprect->right-X;
  int dBottom=lprect->bottom-Y;
  if (dLeft < dRight) {
    if (dTop < dLeft) return HUNDRED_QUAD_TOP;
    if (dBottom < dLeft) return HUNDRED_QUAD_BOTTOM;
    return HUNDRED_QUAD_LEFT;
  } else {
    if (dTop < dRight) return HUNDRED_QUAD_TOP;
    if (dBottom < dRight) return HUNDRED_QUAD_BOTTOM;
    return HUNDRED_QUAD_RIGHT;
  }
}

/* As a low level hook we are called before SetCursorPos() happens so we can't adjust postion inside the hook */
/* Windows disconnects the hook if we don't respond in a timely fashion, such as when we are halted in the debugger */
static void MouseMoved(WPARAM wParam, LONG X, LONG Y) { // called by mouse hook to handle mouse movements
  // Should modify the following line to happen only on visible form, reducing CPU usage
  if (g_dlg_bActivateProgram && g_nBlockClip==0){  // do something about mouse (clipping or unclipping) only if program is activated
    // activate clipping
    TCHAR sz[64];
    POINT pt_cursor = {X,Y};
    unsigned nCursorOnScreen=0; // figure out which screen the cursor is on, most likely way first
    if (GetKeyState(VK_LBUTTON) < 0 || GetKeyState(VK_RBUTTON) < 0) {
      if (g_bTemporaryDragRelease==0) {
        ClipCursor(&g_rectMaxClipCursor);
        g_bTemporaryDragRelease=1;
        if (g_dlg_bDebug) SetWindowText(g_hwndClipRegion,TEXT("ReleaseDrag"));
      }
      return;
    } else if (g_bTemporaryDragRelease) {
      g_bTemporaryDragRelease=0;
    }
    if (g_nActiveScreen && g_bTemporaryClipRelease==0) { /* if the GetClipCursor is still what we set it to then we know the cursor is on the right screen no matter which screen it may be temporairly on */
      RECT rect_OldClip;
      GetClipCursor(&rect_OldClip);
      if (memcmp(g_rectScreens+g_nActiveScreen-1,&rect_OldClip,sizeof(rect_OldClip))) {
        g_nActiveScreen=0;
        if (g_dlg_bDebug) SetWindowText(g_hwndActiveScreen,TEXT("so"));
      } else {
        nCursorOnScreen=g_nActiveScreen;
      }
    } 
    if (0 && nCursorOnScreen == 0 && g_nActiveScreen && PtInRect(g_rectScreens+g_nActiveScreen-1,pt_cursor)) {
      nCursorOnScreen=g_nActiveScreen;
    }
    if (nCursorOnScreen == 0) {
      unsigned i;
      for(i=0; i<g_nScreens; i++) { // figure out which screen the mouse cursor is on
        if (PtInRect(g_rectScreens+i,pt_cursor)) { // if cursor in one of the screens then that's where it is
          nCursorOnScreen = i+1;
          break;
        }
      }
    }
    if (0 && nCursorOnScreen == 0) nCursorOnScreen=g_nActiveScreen; // If the cursor is outside of every screen then assume it's still on the screen it was before.
    if (g_nActiveScreen == 0 || nCursorOnScreen && g_nActiveScreen != nCursorOnScreen) { // if cursor in one of the screens -> clip to that screen
      if (g_dlg_bDebug) {
        wsprintf(sz,TEXT("m(%d,%d)"),X,Y);
        SetWindowText(g_hwndCoords,sz);
      }
      g_bTemporaryClipRelease=0;
      g_bHundredPoint=0;
      if (g_dlg_bDebug) {
        SetWindowText(g_hwndHundredPoint,TEXT("NoPoint"));
        SetWindowText(g_hwndHundredAngle,TEXT("?"));
      }

      ClipCursor(g_rectScreens+nCursorOnScreen-1);  // clip the cursor

      g_nActiveScreen=nCursorOnScreen;              // flag clipping as active
      if (g_dlg_bDebug) {
        wsprintf(sz,TEXT("s%u"),nCursorOnScreen);
        SetWindowText(g_hwndActiveScreen,sz);
      }

      g_nClipCounter++;
      if (g_nClipCounter>255) g_nClipCounter=0;
      if (g_dlg_bDebug) {
        wsprintf(sz,TEXT("n%u"),g_nClipCounter);
        SetWindowText(g_hwndClipCounter,sz);

        wsprintf(sz,TEXT("c(%d,%d)-(%d,%d)"),g_rectScreens[nCursorOnScreen-1].left,g_rectScreens[nCursorOnScreen-1].top,g_rectScreens[nCursorOnScreen-1].right,g_rectScreens[nCursorOnScreen-1].bottom);
        SetWindowText(g_hwndClipRegion,sz);
      }

      //notifyIcon1.Icon = new Icon(GetType(), "ddmm_screen" + ((i == 1) ? "1" : "2") + ".ico"); // change icon (2 icons for different screens)
      // g_bCanClip = 0;                // no need to allow new clipping
    } else if (1) {
      if (!g_dlg_bMethod_ZMotion) {
        g_bHundredPoint=0;
        if (g_dlg_bDebug) {
          wsprintf(sz,TEXT("m(%d,%d)"),X,Y);
          SetWindowText(g_hwndCoords,sz);
        }
      } else {
        unsigned nQuadrant=GetQuadrant(&g_rectScreens[g_nActiveScreen-1],X,Y);
        if (g_dlg_bDebug) {
          wsprintf(sz,TEXT("m(%d,%d)%s"),X,Y,g_szQuadrant+2*nQuadrant);
          SetWindowText(g_hwndCoords,sz);
        }
        if (X > g_rectScreens[g_nActiveScreen-1].left+HUNDRED_EDGE && X < g_rectScreens[g_nActiveScreen-1].right-HUNDRED_EDGE &&
            Y > g_rectScreens[g_nActiveScreen-1].top+HUNDRED_EDGE  && Y < g_rectScreens[g_nActiveScreen-1].bottom-HUNDRED_EDGE) {
          g_bHundredPoint=1;
          g_ptHundred.x=X;
          g_ptHundred.y=Y;
          g_nHundredFirstQuadrant=nQuadrant;
          if (g_dlg_bDebug) {
            wsprintf(sz,TEXT("h(%d,%d)%s"),X,Y,g_szQuadrant+2*g_nHundredFirstQuadrant);
            SetWindowText(g_hwndHundredPoint,sz);
            SetWindowText(g_hwndHundredAngle,TEXT("."));
          }
        } else if (g_bHundredPoint) {
          if (nQuadrant != g_nHundredFirstQuadrant) {
            g_bHundredPoint=0;
            if (g_dlg_bDebug) SetWindowText(g_hwndHundredPoint,TEXT("QuadLock"));
          } else {
            int normal_angle=0,normal_perp;
            if (nQuadrant == HUNDRED_QUAD_TOP || nQuadrant == HUNDRED_QUAD_BOTTOM) {
              normal_perp=g_ptHundred.y-Y;
              normal_angle=g_ptHundred.x-X;
            } else {
              normal_perp=g_ptHundred.x-X;
              normal_angle=g_ptHundred.y-Y;
            }
            if (normal_perp<0) normal_perp=-normal_perp;
            if (normal_angle<0) normal_angle=-normal_angle;
            if (normal_angle==0) {
              normal_angle=99999;
            } else {
              normal_angle=normal_perp/normal_angle; /* normal_angle=normal_perp/normal_parallel. This angle isn't an angle, it's rise/run. */
            }
            if (g_dlg_bDebug) {
              wsprintf(sz,TEXT("a%d"),normal_angle);
              SetWindowText(g_hwndHundredAngle,sz);
            }
            if (g_bCursorAlreadyTouchingEdge==0) {
              g_nAngleFirstTouchedEdge=normal_angle;
              if (g_dlg_bDebug) {
                wsprintf(sz,TEXT("h(%d,%d)%s%d"),g_ptHundred.x,g_ptHundred.y,g_szQuadrant+2*g_nHundredFirstQuadrant,normal_angle);
                SetWindowText(g_hwndHundredPoint,sz);
              }
            }
          }
        }
      }

      if (X < g_rectScreens[g_nActiveScreen-1].left) X = g_rectScreens[g_nActiveScreen-1].left;  // The mouse can go 1 pixel outside the clip region so we consider this to be on the rect
      else if (X > g_rectScreens[g_nActiveScreen-1].right-1) X = g_rectScreens[g_nActiveScreen-1].right-1;
      if (Y < g_rectScreens[g_nActiveScreen-1].top) Y = g_rectScreens[g_nActiveScreen-1].top;
      else if (Y > g_rectScreens[g_nActiveScreen-1].bottom-1) Y = g_rectScreens[g_nActiveScreen-1].bottom-1;
      if (PtOnRect(g_rectScreens+g_nActiveScreen-1,X,Y,g_bCursorAlreadyTouchingEdge)) {
        SetDelayTimer(1);
    	  if (g_bCursorAlreadyTouchingEdge==0) {
    	    g_bCursorAlreadyTouchingEdge=1;
    	    g_ptCursorFirstTouchedEdge.x=X; 
    	    g_ptCursorFirstTouchedEdge.y=Y;
    	  } else if (g_bHundredPoint && g_nAngleFirstTouchedEdge>=HUNDRED_MINANGLE) {
    	    int jiffies; /* jiffies usually refers to the Unix/Linux tick count. Here's it's pixels. */
    	    if (Y == g_ptCursorFirstTouchedEdge.y) {
    	      if (X>g_rectScreens[g_nActiveScreen-1].left+CURSOR_CORNER_JIFFIES && X<g_rectScreens[g_nActiveScreen-1].right-CURSOR_CORNER_JIFFIES) {
    	        jiffies = g_ptCursorFirstTouchedEdge.x - X;
    	        if (jiffies < 0) jiffies=-jiffies;
    	        if (jiffies > CURSOR_Z_JIFFIES) {
    	          ClipCursor(&g_rectMaxClipCursor);
    	          g_bTemporaryClipRelease=1;
    	          if (g_dlg_bDebug) SetWindowText(g_hwndClipRegion,TEXT("Release"));
    	          //g_nActiveScreen=0; SetWindowText(g_hwndActiveScreen,"s0");
    	          //g_bCursorAlreadyTouchingEdge=0;
    	        }
    	      } else goto release;
    	    } else if (X == g_ptCursorFirstTouchedEdge.x) {
    	      if (Y>g_rectScreens[g_nActiveScreen-1].top+CURSOR_CORNER_JIFFIES && Y<g_rectScreens[g_nActiveScreen-1].bottom-CURSOR_CORNER_JIFFIES) {
    	        jiffies = g_ptCursorFirstTouchedEdge.y - Y;
    	        if (jiffies < 0) jiffies=-jiffies;
    	        if (jiffies > CURSOR_Z_JIFFIES) {
    	          ClipCursor(&g_rectMaxClipCursor);
    	          g_bTemporaryClipRelease=1;
    	          if (g_dlg_bDebug) SetWindowText(g_hwndClipRegion,TEXT("Release"));
    	          //g_nActiveScreen=0; SetWindowText(g_hwndActiveScreen,"s0");
    	          //g_bCursorAlreadyTouchingEdge=0;
    	        }
    	      } else goto release;
    	    }
        }
      } else { /* point not on rect */
        SetDelayTimer(0);
        g_bCursorAlreadyTouchingEdge=0;
release:
        if (g_bTemporaryClipRelease) {
          g_bTemporaryClipRelease=0;
          if (g_nActiveScreen) {
            if (g_dlg_bDebug) {
              wsprintf(sz,TEXT("c(%d,%d)-(%d,%d)re"),g_rectScreens[nCursorOnScreen-1].left,g_rectScreens[nCursorOnScreen-1].top,g_rectScreens[nCursorOnScreen-1].right,g_rectScreens[nCursorOnScreen-1].bottom);
              SetWindowText(g_hwndClipRegion,sz);
            }
            ClipCursor(g_rectScreens+g_nActiveScreen-1);  // clip the cursor
          }
        }
      }
    }
  }
}

static void MouseTeleport(int bToPrimary) { // called by keyboard hook to handle mouse teleport
  g_nBlockClip++;
  if (ClipCursor(&g_rectMaxClipCursor)) { // clip to original zone
    POINT pt_cursor;
    unsigned nCursorOnScreen=0,nPrimaryScreen=0; // figure out which screen the cursor is on, most likely way first
    unsigned i;
    GetCursorPos(&pt_cursor);
    for(i=0; i<g_nScreens; i++) { // figure out which screen the mouse cursor is on
      if (g_rectScreens[i].left==0 && g_rectScreens[i].top==0) nPrimaryScreen=i+1;
      if (PtInRect(g_rectScreens+i,pt_cursor)) { // if cursor in one of the screens then that's where it is
        nCursorOnScreen = i+1;
        if (!bToPrimary) break;
      }
    }
    if (nCursorOnScreen) { /* get position X,Y ranging from 0.0 to 1.0 to scale to next screen */
      float fX,fY;
      LONG newX,newY;
      unsigned nCursorNextScreen=nCursorOnScreen--;
      if (nCursorNextScreen>=g_nScreens) nCursorNextScreen=0;
      if (bToPrimary && nPrimaryScreen) nCursorNextScreen=nPrimaryScreen-1;
      if (nCursorOnScreen != nCursorNextScreen) {
        fX=pt_cursor.x-g_rectScreens[nCursorOnScreen].left; fX /= (g_rectScreens[nCursorOnScreen].right-g_rectScreens[nCursorOnScreen].left);
        fY=pt_cursor.y-g_rectScreens[nCursorOnScreen].top; fY /= (g_rectScreens[nCursorOnScreen].bottom-g_rectScreens[nCursorOnScreen].top);
        newX=fX*(g_rectScreens[nCursorNextScreen].right-g_rectScreens[nCursorNextScreen].left); newX += g_rectScreens[nCursorNextScreen].left;
        newY=fY*(g_rectScreens[nCursorNextScreen].bottom-g_rectScreens[nCursorNextScreen].top); newY += g_rectScreens[nCursorNextScreen].top;
        if (newX == g_rectScreens[nCursorNextScreen].left) newX++; /* prevent users from warping from edge to edge. That way we get at least one mouse event to start screen clipping */
        else if (newX == g_rectScreens[nCursorNextScreen].right) newX--;
        if (newY == g_rectScreens[nCursorNextScreen].top) newY++;
        else if (newY == g_rectScreens[nCursorNextScreen].bottom) newY--;
        SetCursorPos(newX,newY);
        g_nActiveScreen=0; // Let the next motion of the mouse reapply the clip region. If they teleport to the edge where the first movement is the next screen, too bad!
      }
    }
  }
  g_nBlockClip--;
}

static HHOOK g_hMouseOldHook=NULL;
/* Mouse Hooks are epic fail. wParam and lParam aren't much good if we don't have the message. */
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode>=0) {
    MOUSEHOOKSTRUCT *pMouseStruct = (MOUSEHOOKSTRUCT *)lParam;
    MouseMoved(wParam, pMouseStruct->pt.x, pMouseStruct->pt.y);
  }
  return CallNextHookEx(g_hMouseOldHook, nCode, wParam, lParam);
}

/* This init can be called multiple times */
static void InitMouseHook(int bInit) {
  if (bInit) {
    if (g_hMouseOldHook == NULL) {
      g_hMouseOldHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(NULL), 0);
    }
  } else {
    if (g_hMouseOldHook != NULL) {
      UnhookWindowsHookEx(g_hMouseOldHook);
      g_hMouseOldHook=NULL;
    }
  }
}

static int g_fCtrlKeyPressed=0;
static void CtrlKeyPressed(int bPressed) {
  if (bPressed) {
    if (g_dlg_bMethod_CtrlKey && !g_fCtrlKeyPressed) {
      ClipCursor(&g_rectMaxClipCursor);
      g_nActiveScreen=0;
      g_nBlockClip++;
      //notifyIcon1.Icon = new Icon(GetType(), "ddmm_normal.ico");
      g_fCtrlKeyPressed=bPressed;
    }
  } else {
    if (g_fCtrlKeyPressed) { /* If the users holds control, clicks g_dlg_bMethod_CtrlKey off, and releases control, we must still do this part */
      g_nBlockClip--;
    }
    g_fCtrlKeyPressed=bPressed;
  }
}

static HHOOK g_hKeyOldHook=NULL;
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode >= 0) {
    KBDLLHOOKSTRUCT *phook=(KBDLLHOOKSTRUCT *)lParam;
    switch(phook->vkCode) {
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_CONTROL:
      if (wParam == WM_KEYDOWN) CtrlKeyPressed(1);
      else if (wParam == WM_KEYUP) CtrlKeyPressed(0);
      break;
      case VK_D:
      if (wParam == WM_KEYDOWN && GetAsyncKeyState(VK_CONTROL)<0 && GetAsyncKeyState(VK_MENU)<0) { /* VK_MENU == Alt Key */
        MouseTeleport(1); // Place cursor on primary screen
        //EmergencyRestore(); // Ctrl+Alt+D: emergency restore ways of exiting mouse clipping
      }
      break;
    case VK_OEM_3: /* The tilde (~) key */
      if (g_dlg_bMethod_CtrlTilde && wParam == WM_KEYDOWN && GetAsyncKeyState(VK_CONTROL)<0) {
        MouseTeleport(GetAsyncKeyState(VK_MENU)<0); // Ctrl + ~ : teleports mouse to the next screen, Ctrl+Alt teleports to the primary screen.
      }
      break;
    default:
      break;
    }
  }
  return CallNextHookEx(g_hKeyOldHook, nCode, wParam, lParam);
}

/* This init can be called multiple times */
static void InitKeyHook(int bInit) {
  if (bInit) {
    if (g_hKeyOldHook == NULL) {
      g_hKeyOldHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    }
  } else {
    if (g_hKeyOldHook != NULL) {
      UnhookWindowsHookEx(g_hKeyOldHook);
      g_hKeyOldHook=NULL;
    }
  }
}

static TCHAR szStartWithWindowsRegPath[]=TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
static void SetAutoStart(int bInit) {
  HKEY hKey;
  if (bInit) {
    if (ERROR_SUCCESS==RegOpenKeyEx(HKEY_LOCAL_MACHINE,szStartWithWindowsRegPath,0,KEY_SET_VALUE,&hKey)) {
      TCHAR szModuleName[MAX_PATH];
      DWORD cchSize=GetModuleFileName( NULL, szModuleName, NELEM(szModuleName));
      if (cchSize>0 && cchSize<NELEM(szModuleName)) {
        TCHAR szRunPathParm[MAX_PATH*2];
        wsprintf(szRunPathParm,TEXT("\"%s\" /minimize"),szModuleName);
        RegSetValueEx(hKey,TEXT("ddmm"),0,REG_SZ,(BYTE *)szRunPathParm,lstrlen(szRunPathParm));
      }
      RegCloseKey(hKey);
    }
  } else {
    if (ERROR_SUCCESS==RegOpenKeyEx(HKEY_LOCAL_MACHINE,szStartWithWindowsRegPath,0,KEY_SET_VALUE,&hKey)) {
      RegDeleteValue(hKey,TEXT("ddmm"));
      RegCloseKey(hKey);
    }
  }
}

static int IsAutoStartEnabled(void) {
  int rv=0;
  HKEY hKey;
  if (ERROR_SUCCESS==RegOpenKeyEx(HKEY_LOCAL_MACHINE,szStartWithWindowsRegPath,0,KEY_READ,&hKey)) {
    TCHAR szModuleName[MAX_PATH],szFileName2[MAX_PATH];
    DWORD cchSize=GetModuleFileName( NULL, szModuleName, NELEM(szModuleName));
    if (cchSize>0 && cchSize<NELEM(szModuleName)) {
      DWORD cbData=sizeof(szModuleName);
      DWORD dwType=REG_SZ;
      if (ERROR_SUCCESS==RegGetValue(hKey,TEXT(""),TEXT("ddmm"),RRF_RT_REG_SZ,&dwType,(BYTE *)szFileName2,&cbData)) {
        if (lstrcmp(szModuleName,szFileName2)==0) rv=1;
      }
    }
    RegCloseKey(hKey);
  }
  return rv;
}

static int GetStartMenuShortcutPath(HWND hwnd,TCHAR *szName,TCHAR *szPath,unsigned cchPath) {
  int rv=0;
  TCHAR szStartMenu[MAX_PATH];
  if (S_OK==SHGetFolderPath(hwnd,CSIDL_PROGRAMS,NULL,SHGFP_TYPE_CURRENT,szStartMenu)) { 
    rv=_tcscatPath(szPath,cchPath,szStartMenu,szName,NULL);
  }
  return rv;
}

// http://www.geekpedia.com/tutorial125_Create-shortcuts-with-a-.NET-application.html
static void SetStartMenuShortcut(int bInit) {
  if (bInit) {
    TCHAR szModuleName[MAX_PATH];
    DWORD cchSize=GetModuleFileName( NULL, szModuleName, NELEM(szModuleName));
    if (cchSize>0 && cchSize<NELEM(szModuleName)) {
      TCHAR szStartMenuPath[MAX_PATH];
      if (AddNewGroup(_T(""), szStartMenuPath,NELEM(szStartMenuPath),FALSE)) {
        CreateShellLink(szModuleName, szStartMenuPath, TEXT("ddmm.lnk"), TEXT("Multi Display Mouse Manager"));
      }
    }
  } else {
    TCHAR szLink[MAX_PATH];
    if (GetStartMenuShortcutPath(NULL,TEXT("ddmm.lnk"),szLink,NELEM(szLink))) {
      DeleteFile(szLink);
    }
  }
}

static int IsStartMenuShortcutPresent(void) {
  TCHAR szLink[MAX_PATH];
  if (GetStartMenuShortcutPath(NULL,TEXT("ddmm.lnk"),szLink,NELEM(szLink))) {
    return PathFileExists(szLink)!=FALSE;
  }
  return 0;
}

static void InitAll(int bInit) {
  InitMEPData(bInit);
  if (g_nScreens<=1) {
    bInit=0;
    InitMEPData(bInit);
  }
  InitMouseHook(bInit); /* this causes a slow close and minimize. Disabling MouseMoved() doesn't fix it */
  InitKeyHook(bInit); 
}

static HICON g_hIcon_Open;
static HICON g_hIcon_TopClose;
static HICON g_hIcon_BotClose;

static void SetIcon(HWND hDlg,HICON hi) {
  if (hi) {
    SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hi);
    SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hi);
    // DestroyIcon(hi); // http://stackoverflow.com/questions/15556817/is-a-memory-leak-possible-with-loadicon
  }
}

static BOOL WritePrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, int nValue, LPCTSTR lpFileName) {
  TCHAR sz[64];
  wsprintf(sz,TEXT("%d"),nValue);
  return WritePrivateProfileString(lpAppName,lpKeyName,sz,lpFileName);  
}

/* Instead of just accepting the default value, we write the default out so the INI file is complete */
static UINT WINAPI MyGetPrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, INT nDefault, LPCTSTR lpFileName) {
  TCHAR st[64];
  DWORD cchSt=GetPrivateProfileString(lpAppName,lpKeyName,TEXT("\x01"),st,NELEM(st),lpFileName);
  if (cchSt==0 || cchSt>=NELEM(st)-1 || st[0]=='\x01') {
    WritePrivateProfileInt(lpAppName,lpKeyName,nDefault,lpFileName);
    return nDefault;
  } else {
    return _tcstoul(st,NULL,10);
  }
}

/* idOption only applies on Save, 0 to save all */
static void LoadSaveOptions(int bSave,unsigned idOption) {
  if (g_szINIFile[0]) {
    if (bSave) {
      if (idOption==0 || idOption==IDC_CK_ACTMOUSEMGT) WritePrivateProfileInt(TEXT("ddmm"),TEXT("ActivateProgram"),g_dlg_bActivateProgram,g_szINIFile);
      if (idOption==0 || idOption==IDC_CK_CTRL_CROSS)  WritePrivateProfileInt(TEXT("ddmm"),TEXT("Method_CtrlKey"),g_dlg_bMethod_CtrlKey,g_szINIFile);       // Hide tray icon
      if (idOption==0 || idOption==IDC_CK_DELAY_CROSS) WritePrivateProfileInt(TEXT("ddmm"),TEXT("Method_Delay"),g_dlg_bMethod_Delay,g_szINIFile);
      if (idOption==0 || idOption==IDC_ED_DELAY_CROSS) WritePrivateProfileInt(TEXT("ddmm"),TEXT("Delay_UnClip"),g_dlg_bMethod_Delay_Time,g_szINIFile);
      //?=MyGetPrivateProfileInt(TEXT("ddmm"),"Window_Minimized",0,g_szINIFile);
      if (idOption==0 || idOption==IDC_CK_MOUSEJUMP)   WritePrivateProfileInt(TEXT("ddmm"),TEXT("UseMouseJump"),g_dlg_bMethod_CtrlTilde,g_szINIFile);
      if (idOption==0 || idOption==IDC_CK_ZMOTION)     WritePrivateProfileInt(TEXT("ddmm"),TEXT("UseZMotion"),g_dlg_bMethod_ZMotion,g_szINIFile);
    } else {
      g_dlg_bActivateProgram  =MyGetPrivateProfileInt(TEXT("ddmm"),TEXT("ActivateProgram"),1,g_szINIFile);
      //?=MyGetPrivateProfileInt(TEXT("ddmm"),TEXT("HideTrayIcon"),0,g_szINIFile);
      g_dlg_bMethod_CtrlKey   =MyGetPrivateProfileInt(TEXT("ddmm"),TEXT("Method_CtrlKey"),0,g_szINIFile);
      g_dlg_bMethod_Delay     =MyGetPrivateProfileInt(TEXT("ddmm"),TEXT("Method_Delay"),1,g_szINIFile);
      g_dlg_bMethod_Delay_Time=MyGetPrivateProfileInt(TEXT("ddmm"),TEXT("Delay_UnClip"),150,g_szINIFile);
      if (g_dlg_bMethod_Delay_Time<55) g_dlg_bMethod_Delay_Time=55;
      //?=MyGetPrivateProfileInt(TEXT("ddmm"),TEXT("Window_Minimized"),0,g_szINIFile);
      g_dlg_bMethod_CtrlTilde =MyGetPrivateProfileInt(TEXT("ddmm"),TEXT("UseMouseJump"),1,g_szINIFile);
      g_dlg_bMethod_ZMotion   =MyGetPrivateProfileInt(TEXT("ddmm"),TEXT("UseZMotion"),0,g_szINIFile);
    }
  }
}

#define ID_TRAY_APP                     (5000)

static UINT g_TrayIcon_WM_TBC;
static int g_TrayIcon_bWM_CLOSE; /* non zero if WM_CLOSE is supposed to close the program, 0 if WM_CLOSE minimizes */
static struct TrayIcon_NOTIFYICONDATA g_nidTRAY_APP;

/* Unlike the other TrayIcon functions, this is one you copy into your program. It's named TrayIcon
   so that you can find this with all the other the places TrayIcon functions are supposed to go. */
static void TrayIcon_InitAll(HWND hwnd,int bInit,HICON hIcon) {
  if (bInit) {
    HMENU hMenu = GetSubMenu(LoadMenu(NULL, MAKEINTRESOURCE(IDM_TRAYMENU)),0);
    g_TrayIcon_bWM_CLOSE=0;
    TrayIcon_InitNotifyIconData(hwnd,&g_nidTRAY_APP,WM_TRAYICON,ID_TRAY_APP,hIcon,TEXT("Multi Monitor Mouse Manager"),0,1,hMenu);
  } else {
    TrayIcon_Delete(&g_nidTRAY_APP);
  }
}

static INT_PTR WINAPI DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  TCHAR sz1[64],sz2[64];
  if (msg==g_TrayIcon_WM_TBC) { // this is a variable so can't go in a case statement
    TrayIcon_TaskbarCreated(&g_nidTRAY_APP);
  } else switch (msg) {
  case WM_INITDIALOG:
    LoadSaveOptions(0,0);
    g_hIcon_Open=LoadIcon(GetModuleHandle(0),MAKEINTRESOURCE(IDI_ICON_OPEN));
    SetIcon(hDlg,g_hIcon_Open);
    TrayIcon_InitAll(hDlg,1,g_hIcon_Open);
    g_hIcon_TopClose=LoadIcon(GetModuleHandle(0),MAKEINTRESOURCE(IDI_ICON_TOPCLOSE));
    g_hIcon_BotClose=LoadIcon(GetModuleHandle(0),MAKEINTRESOURCE(IDI_ICON_BOTCLOSE));

    g_hwndDialog=hDlg;
    g_hwndCoords=GetDlgItem(hDlg,IDC_ST_COORDS);
    g_hwndClipRegion=GetDlgItem(hDlg,IDC_ST_CLIPRGN);
    g_hwndWasClipRegion=GetDlgItem(hDlg,IDC_ST_WASCLIPRGN);
    g_hwndClipCounter=GetDlgItem(hDlg,IDC_ST_CLIPCTR);
    g_hwndScreens=GetDlgItem(hDlg,IDC_LB_SCREENS);
    g_hwndActiveScreen=GetDlgItem(hDlg,IDC_ST_ACTSCREEN);
    g_hwndHundredAngle=GetDlgItem(hDlg,IDC_ST_HUNDRANGLE);
    g_hwndHundredPoint=GetDlgItem(hDlg,IDC_ST_HUNDRPOINT);
    g_hwndTimerCounter=GetDlgItem(hDlg,IDC_ST_TIMCOUNTER);
    CheckDlgButton(hDlg,IDC_CK_ACTMOUSEMGT,g_dlg_bActivateProgram);
    CheckDlgButton(hDlg,IDC_CK_STARTWIN,IsAutoStartEnabled());
    CheckDlgButton(hDlg,IDC_CK_SHORTCUT,IsStartMenuShortcutPresent());
    CheckDlgButton(hDlg,IDC_CK_CTRL_CROSS,g_dlg_bMethod_CtrlKey);
    CheckDlgButton(hDlg,IDC_CK_DELAY_CROSS,g_dlg_bMethod_Delay); g_idDelayTimer=0; SetDelayTimerText(hDlg);
    CheckDlgButton(hDlg,IDC_CK_MOUSEJUMP,g_dlg_bMethod_CtrlTilde);
    CheckDlgButton(hDlg,IDC_CK_ZMOTION,g_dlg_bMethod_ZMotion);
    CheckDlgButton(hDlg,IDC_CK_DEBUG,g_dlg_bDebug);
    InitAll(g_dlg_bActivateProgram);
    ConvertStaticToHyperlink(GetDlgItem(hDlg,IDC_ST_HYPERLINK),TRUE);
    return TRUE;
  case WM_CLOSE: 
    // if (g_TrayIcon_bWM_CLOSE==0 && TrayIcon_SC_MINIMIZE(&g_nidTRAY_APP)) return 0;
    SetDelayTimer(0);
    InitAll(0); 
    TrayIcon_InitAll(hDlg,0,NULL);
    RemoveStaticHyperlink(GetDlgItem(hDlg,IDC_ST_HYPERLINK)); 
    DestroyWindow(hDlg); // https://www-user.tu-chemnitz.de/~heha/petzold/ch11b.htm
    return FALSE;
  case WM_DESTROY: 
    PostQuitMessage(0);
    return TRUE;
  case WM_TRAYICON_HIDE:
    TrayIcon_SC_MINIMIZE(&g_nidTRAY_APP);
    return 0;
  case WM_SYSCOMMAND:
    switch( wParam & 0xfff0 ) {  // (filter out reserved lower 4 bits:  see msdn remarks http://msdn.microsoft.com/en-us/library/ms646360(VS.85).aspx)
    //case SC_CLOSE: goto close;   // Alt+F4 or close is selected in the corner box. It isn't necessary to process this message since it turns into a WM_CLOSE unless you want the corner box to behave differently
    case SC_RESTORE: 
      if (TrayIcon_SC_RESTORE(&g_nidTRAY_APP)) return TRUE;
      break; // DefWindowProc() so Maximize -> Restore works properly.
    case SC_MINIMIZE: 
      if (TrayIcon_SC_MINIMIZE(&g_nidTRAY_APP)) return TRUE;
      break;
    }
    break;
  case WM_TRAYICON:
    switch(wParam) {
    case ID_TRAY_APP:
      switch (TrayIcon_Message(&g_nidTRAY_APP,lParam)) {
      case IDM_TRAYMENU_RESTORE: 
        PostMessage(hDlg,WM_SYSCOMMAND,SC_RESTORE,0);
        return TRUE;
      case IDM_TRAYMENU_EXIT: 
        PostMessage(hDlg,WM_CLOSE,0,0);
        return TRUE;
      }
      break;
    }
    break;
  case WM_DISPLAYCHANGE: InitAll(1); break;
  case WM_TIMER: 
    switch (wParam) { 
    case IDT_TIMER_DELAY: 
      ProcDelayTimer(hDlg); 
      return TRUE;
    }
    break; /* WM_TIMER */
  case WM_COMMAND: {
      switch(GET_WM_COMMAND_ID(wParam,lParam)) {
      case IDC_CK_ACTMOUSEMGT:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == BN_CLICKED) {
          g_dlg_bActivateProgram=IsDlgButtonChecked(hDlg,IDC_CK_ACTMOUSEMGT);
          LoadSaveOptions(1,IDC_CK_ACTMOUSEMGT); 
          InitAll(g_dlg_bActivateProgram);
        }
        return TRUE;
      case IDC_CK_STARTWIN:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == BN_CLICKED) {
          SetAutoStart(IsDlgButtonChecked(hDlg,IDC_CK_STARTWIN));
        }
        return TRUE;
      case IDC_CK_SHORTCUT:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == BN_CLICKED) {
          SetStartMenuShortcut(IsDlgButtonChecked(hDlg,IDC_CK_SHORTCUT));
        }
        return TRUE;
      case IDC_CK_CTRL_CROSS:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == BN_CLICKED) {
          g_dlg_bMethod_CtrlKey=IsDlgButtonChecked(hDlg,IDC_CK_CTRL_CROSS);
          LoadSaveOptions(1,IDC_CK_CTRL_CROSS); 
        }
        return TRUE;
      case IDC_CK_DELAY_CROSS:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == BN_CLICKED) {
          g_dlg_bMethod_Delay=IsDlgButtonChecked(hDlg,IDC_CK_DELAY_CROSS);
          LoadSaveOptions(1,IDC_CK_DELAY_CROSS); 
        }
        return TRUE;
      case IDC_CK_MOUSEJUMP:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == BN_CLICKED) {
          g_dlg_bMethod_CtrlTilde=IsDlgButtonChecked(hDlg,IDC_CK_MOUSEJUMP);
          LoadSaveOptions(1,IDC_CK_MOUSEJUMP); 
        }
        return TRUE;
      case IDC_CK_ZMOTION:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == BN_CLICKED) {
          g_dlg_bMethod_ZMotion=IsDlgButtonChecked(hDlg,IDC_CK_ZMOTION);
          LoadSaveOptions(1,IDC_CK_ZMOTION); 
          ClearAllDebug();
        }
        return TRUE;
      case IDC_CK_DEBUG:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == BN_CLICKED) {
          g_dlg_bDebug=IsDlgButtonChecked(hDlg,IDC_CK_DEBUG);
          ClearAllDebug();
        }
        return TRUE;
      case IDC_ED_DELAY_CROSS:
        if (GET_WM_COMMAND_CMD(wParam,lParam) == EN_UPDATE) {
          if (GetWindowText((HWND)lParam,sz1,NELEM(sz1))) {
            int valEdit=_tcstoul(sz1,NULL,10);
            if (valEdit<0) valEdit=0;
            wsprintf(sz2,TEXT("%d"),valEdit);
            if (lstrcmp(sz1,sz2)) SetWindowText((HWND)lParam,sz2); // prevent infinite loop and have windows close us silently
            if (valEdit>=55) {
              g_dlg_bMethod_Delay_Time=valEdit;
              LoadSaveOptions(1,IDC_ED_DELAY_CROSS); 
            }
          }
        }
        return TRUE;
      }
    case IDC_ST_HYPERLINK: 
      if (GET_WM_COMMAND_CMD(wParam,lParam)==STN_CLICKED) {
        if (GetWindowText((HWND)lParam,sz1,NELEM(sz1))) {
          ShellExecuteNoAsync(hDlg,NULL, sz1, NULL, NULL, SW_SHOW);
        }
      }
      break; // seems that MSDN-2009 for STN_CLICKED hasn't been updated since Win16: hwndStatic = (HWND) LOWORD(lParam)
    } break; // WM_COMMAND
  }
  return FALSE;
}

/* Each dialog with a special class gets a CLASS designation in the RC file. 
   This creates the class so the dialog can come into existance */
static void RegisterDialogClass(LPCTSTR szNewName) {
  WNDCLASSEX wc;
  wc.cbSize = sizeof(wc); 
  GetClassInfoEx(0, _T("#32770"), &wc); // Get the class structure for the system dialog class
  wc.style &= ~CS_GLOBALCLASS;  // Make sure our new class does not conflict
  wc.lpszClassName = szNewName; // Register an identical dialog class, but with a new name!
  if (!wc.cbClsExtra) wc.cbClsExtra = 2*sizeof(INT_PTR)+1; // It's hard to find a window that has class bytes to test the class dialog. Since we are already hacking the dialog class let's make some fake ones unless in the future Microsoft decides to add class bytes to dialogs. We assume that every sane user of cbClsExtra will align them to sizeof(INT_PTR). What does that make us?
  RegisterClassEx(&wc);
}

// http://www.codeproject.com/KB/cpp/avoidmultinstance.aspx?fid=695&df=90&mpp=25&noise=3&prof=False&sort=Position&view=Quick&fr=76#xx0xx
static BOOL InitInstance(HINSTANCE hInstance,LPCTSTR lpClass) {
  HANDLE hMutexOneInstance = CreateMutex( NULL, TRUE, lpClass );
  int bAlreadyRunning=bAlreadyRunning = (GetLastError() == ERROR_ALREADY_EXISTS);
  if (hMutexOneInstance != NULL) {
    ReleaseMutex(hMutexOneInstance);
  }

  if (bAlreadyRunning) {
    HWND hOther = FindWindow(lpClass, NULL);
    if (hOther != NULL) {
      SetForegroundWindow(hOther);
      if (!IsWindowVisible(hOther)) {
        ShowWindow(hOther, SW_SHOW);
      } else if (IsIconic(hOther)) {
        ShowWindow(hOther, SW_RESTORE);
      }
    }
    return FALSE; // terminates the creation
  }
  return TRUE;
}

#define RC_DIALOG_CLASS _T("ddmm-MultiDisplayMouseManager")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
  if (InitInstance(hInstance,RC_DIALOG_CLASS)) {
    HWND   hwndMain;
    int nArgc;
    LPWSTR *szArgv=CommandLineToArgvW(GetCommandLineW(), &nArgc);
    //HACCEL hAccelTable;
    DWORD cchSize=GetModuleFileName( hInstance, g_szINIFile, NELEM(g_szINIFile));
    if (!(cchSize>0 && cchSize<NELEM(g_szINIFile))) {
      g_szINIFile[0]='\0';
    } else {
      int len=lstrlen(g_szINIFile);
      if (len>4 && 0==lstrcmpi(g_szINIFile+len-4,TEXT(".EXE"))) {
        _tcscpy(g_szINIFile+len-4,TEXT(".INI"));
      } else if (len>4 && _tcscatPath(g_szINIFile,NELEM(g_szINIFile),NULL,TEXT("INI"))) {
        len=lstrlen(g_szINIFile);
        g_szINIFile[len-4]='.';
      } else g_szINIFile[0]='\0';
    }

    InitCommonControls();//Ex(&ice);

    g_TrayIcon_WM_TBC = TrayIcon_Init_WinMain(1); // goes before CreateWindow

    RegisterDialogClass(RC_DIALOG_CLASS); /* this allows dialogs in the RC files with a CLASS to be created */
    hwndMain = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DialogProc, 0);

    if (hwndMain) {
      MSG    msg;
      // UPDATED (fix for Matrox CenterPOPUP feature :)
      //SetWindowPos(hwndMain, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW); // If we use ShowWindow, then my Matrox card automatically centers WinSpy on the current monitor (even if we restored WinSpy to it's position last time we ran). Therefore we use SetWindowPos to display the dialog, as Matrox don't seem to hook this in their display driver..
      if (nArgc>1 && lstrcmpiW(szArgv[1],L"/MINIMIZE")==0) nShowCmd=SW_MINIMIZE;
      ShowWindow (hwndMain, TrayIcon_Fix_WinMain(&g_nidTRAY_APP,nShowCmd,WM_TRAYICON_HIDE));
      while(GetMessage(&msg,NULL,0,0)) {
        //if (!TranslateAccelerator(hwndMain, hAccelTable, &msg)) {
          if (!IsDialogMessage(hwndMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
          }
        //}
      }
      DestroyWindow(hwndMain);
      TrayIcon_Init_WinMain(0);
      //InitHyperlink(FALSE,hInstance);
    }
  }
  return 0;
}

#ifdef __POCC__
void _cdecl *realloc(void *ptr, size_t size) {
  return ptr?LocalReAlloc(ptr,size,LMEM_FIXED):LocalAlloc(LMEM_FIXED,size);
}

void _cdecl *calloc(size_t num, size_t size) {
  return LocalAlloc(LPTR,size*num);
}

void _cdecl *malloc(size_t size) {
  return LocalAlloc(LMEM_FIXED,size);
}

void _cdecl free(void *ptr) {
  LocalFree(ptr);
}
#endif
