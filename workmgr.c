/*
 *  [ ctwm ]
 *
 *  Copyright 1992 Claude Lecommandeur.
 *
 * Permission to use, copy, modify  and distribute this software  [ctwm] and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above  copyright notice appear  in all copies and that both that
 * copyright notice and this permission notice appear in supporting documen-
 * tation, and that the name of  Claude Lecommandeur not be used in adverti-
 * sing or  publicity  pertaining to  distribution of  the software  without
 * specific, written prior permission. Claude Lecommandeur make no represen-
 * tations  about the suitability  of this software  for any purpose.  It is
 * provided "as is" without express or implied warranty.
 *
 * Claude Lecommandeur DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL  IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS.  IN NO
 * EVENT SHALL  Claude Lecommandeur  BE LIABLE FOR ANY SPECIAL,  INDIRECT OR
 * CONSEQUENTIAL  DAMAGES OR ANY  DAMAGES WHATSOEVER  RESULTING FROM LOSS OF
 * USE, DATA  OR PROFITS,  WHETHER IN AN ACTION  OF CONTRACT,  NEGLIGENCE OR
 * OTHER  TORTIOUS ACTION,  ARISING OUT OF OR IN  CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Claude Lecommandeur [ lecom@sic.epfl.ch ][ April 1992 ]
 */

#include "ctwm.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <X11/Xatom.h>

#include "ctwm_atoms.h"
#include "util.h"
#include "animate.h"
#include "parse.h"
#include "screen.h"
#include "icons.h"
#include "decorations.h"
#include "resize.h"
#include "add_window.h"
#include "events.h"
#include "otp.h"
#include "clicktofocus.h"
#include "cursor.h"
#include "list.h"
#include "image.h"
#include "functions.h"
#include "drawing.h"
#ifdef EWMH
#  include "ewmh_atoms.h"
#endif

#include "gram.tab.h"

// Gotten via screen.h
//#include "workmgr.h"


static void DisplayWinUnchecked(VirtualScreen *vs,
                                TwmWindow *tmp_win);
static void CreateWorkSpaceManagerWindow(VirtualScreen *vs);
static void PaintWorkSpaceManagerBorder(VirtualScreen *vs);
static void ResizeWorkSpaceManager(VirtualScreen *vs, TwmWindow *win);
static void InvertColorPair(ColorPair *cp);
static void WMapRedrawWindow(Window window, int width, int height,
                             ColorPair cp, char *label);

static bool useBackgroundInfo = false;
static XContext MapWListContext = (XContext) 0;
static Cursor handCursor  = (Cursor) 0;


/*
 * Basic setup of Scr->workSpaceMgr structures.  Called (for each screen)
 * early in startup, prior to config file parsing.
 */
void InitWorkSpaceManager(void)
{
	Scr->workSpaceMgr.count         = 0;
	Scr->workSpaceMgr.workSpaceList = NULL;
	Scr->workSpaceMgr.initialstate  = WMS_buttons;
	Scr->workSpaceMgr.geometry      = NULL;
	Scr->workSpaceMgr.buttonStyle   = STYLE_NORMAL;
	Scr->workSpaceMgr.windowcp.back = Scr->White;
	Scr->workSpaceMgr.windowcp.fore = Scr->Black;
	Scr->workSpaceMgr.windowcpgiven = false;

	Scr->workSpaceMgr.occupyWindow = calloc(1, sizeof(OccupyWindow));
	Scr->workSpaceMgr.occupyWindow->name      = "Occupy Window";
	Scr->workSpaceMgr.occupyWindow->icon_name = "Occupy Window Icon";
	Scr->workSpaceMgr.occupyWindow->geometry  = NULL;
	Scr->workSpaceMgr.occupyWindow->columns   = 0;
	Scr->workSpaceMgr.occupyWindow->twm_win   = NULL;
	Scr->workSpaceMgr.occupyWindow->vspace    = Scr->WMgrVertButtonIndent;
	Scr->workSpaceMgr.occupyWindow->hspace    = Scr->WMgrHorizButtonIndent;

	Scr->workSpaceMgr.curColors.back  = Scr->Black;
	Scr->workSpaceMgr.curColors.fore  = Scr->White;
	Scr->workSpaceMgr.defColors.back  = Scr->White;
	Scr->workSpaceMgr.defColors.fore  = Scr->Black;
	Scr->workSpaceMgr.curImage        = NULL;
	Scr->workSpaceMgr.curPaint        = false;
	Scr->workSpaceMgr.defImage        = NULL;
	Scr->workSpaceMgr.vspace          = Scr->WMgrVertButtonIndent;
	Scr->workSpaceMgr.hspace          = Scr->WMgrHorizButtonIndent;
	Scr->workSpaceMgr.name            = "WorkSpaceManager";
	Scr->workSpaceMgr.icon_name       = "WorkSpaceManager Icon";

	Scr->workSpaceMgr.windowFont.basename =
	        "-adobe-courier-medium-r-normal--10-100-75-75-m-60-iso8859-1";
	/*"-adobe-courier-bold-r-normal--8-80-75-75-m-50-iso8859-1";*/

	XrmInitialize();
	if(MapWListContext == (XContext) 0) {
		MapWListContext = XUniqueContext();
	}
}


/*
 * Prep up structures for WSM windows in each VS.  Called (for each
 * screen) in startup after InitVirtualScreens() has setup the VS stuff
 * (and after config file processing).
 */
void ConfigureWorkSpaceManager(void)
{
	VirtualScreen *vs;

	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		/*
		 * Make sure this is all properly initialized to nothing.  Otherwise
		 * bad and undefined behavior can show up in certain cases (e.g.,
		 * with no Workspaces {} defined in .ctwmrc, the only defined
		 * workspace will be random memory bytes, which can causes crashes on
		 * e.g.  f.menu "TwmWindows".)
		 */
		WorkSpaceWindow *wsw = calloc(1, sizeof(WorkSpaceWindow));
		wsw->state = Scr->workSpaceMgr.initialstate;
		vs->wsw = wsw;
	}
}


/***********************************************************************
 *
 *  Procedure:
 *      CreateWorkSpaceManager - create the workspace manager window
 *              for this screen.
 *
 *  Returned Value:
 *      none
 *
 *  Inputs:
 *      none
 *
 ***********************************************************************
 */
void CreateWorkSpaceManager(void)
{
	char vsmapbuf    [1024], *vsmap;
	VirtualScreen    *vs;
	WorkSpace        *ws, *fws;
	int              vsmaplen;

	if(! Scr->workSpaceManagerActive) {
		return;
	}

	Scr->workSpaceMgr.windowFont.basename =
	        "-adobe-courier-medium-r-normal--10-100-75-75-m-60-iso8859-1";
	Scr->workSpaceMgr.buttonFont = Scr->IconManagerFont;
	Scr->workSpaceMgr.cp         = Scr->IconManagerC;
	if(!Scr->BeNiceToColormap) {
		GetShadeColors(&Scr->workSpaceMgr.cp);
	}

	NewFontCursor(&handCursor, "top_left_arrow");

	vsmaplen = sizeof(vsmapbuf);
	if(CtwmGetVScreenMap(dpy, Scr->Root, vsmapbuf, &vsmaplen)) {
		vsmap = strtok(vsmapbuf, ",");
	}
	else {
		vsmap = NULL;
	}

	/*
	 * weird things can happen if the config file is changed or the atom
	 * returned above is messed with.  Sometimes windows may disappear in
	 * that case depending on what's changed.  (depending on where they were
	 * on the actual screen.
	 */
	ws = Scr->workSpaceMgr.workSpaceList;
	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		WorkSpaceWindow *wsw = vs->wsw;
		if(vsmap) {
			fws = GetWorkspace(vsmap);
		}
		else {
			fws = NULL;
		}
		if(fws) {
			wsw->currentwspc = fws;
			vsmap = strtok(NULL, ",");
		}
		else {
			wsw->currentwspc = ws;
			ws = ws->next;
		}
		CreateWorkSpaceManagerWindow(vs);
	}
	CreateOccupyWindow();

	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		WorkSpaceWindow *wsw = vs->wsw;
		WorkSpace *ws2 = wsw->currentwspc;
		MapSubwindow *msw = wsw->mswl [ws2->number];
		if(Scr->workSpaceMgr.curImage == NULL) {
			if(Scr->workSpaceMgr.curPaint) {
				XSetWindowBackground(dpy, msw->w, Scr->workSpaceMgr.curColors.back);
			}
		}
		else {
			XSetWindowBackgroundPixmap(dpy, msw->w, Scr->workSpaceMgr.curImage->pixmap);
		}
		XSetWindowBorder(dpy, msw->w, Scr->workSpaceMgr.curBorderColor);
		XClearWindow(dpy, msw->w);

		if(useBackgroundInfo && ! Scr->DontPaintRootWindow) {
			if(ws2->image == NULL) {
				XSetWindowBackground(dpy, vs->window, ws2->backcp.back);
			}
			else {
				XSetWindowBackgroundPixmap(dpy, vs->window, ws2->image->pixmap);
			}
			XClearWindow(dpy, vs->window);
		}
	}

	{
		char *wrkSpcList;
		int  len;

		len = GetPropertyFromMask(0xFFFFFFFFu, &wrkSpcList);
		XChangeProperty(dpy, Scr->Root, XA_WM_WORKSPACESLIST, XA_STRING, 8,
		                PropModeReplace, (unsigned char *) wrkSpcList, len);
		free(wrkSpcList);
	}
}



/*
 * Various funcs for switching workspaces
 */
void GotoWorkSpaceByName(VirtualScreen *vs, char *wname)
{
	WorkSpace *ws;

	if(! Scr->workSpaceManagerActive) {
		return;
	}
	if(!vs) {
		return;
	}
	ws = GetWorkspace(wname);
	if(ws == NULL) {
		return;
	}
	GotoWorkSpace(vs, ws);
}


void GotoWorkSpaceByNumber(VirtualScreen *vs, int workspacenum)
{
	WorkSpace *ws;
	if(! Scr->workSpaceManagerActive) {
		return;
	}
	if(!vs) {
		return;
	}
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(ws->number == workspacenum) {
			break;
		}
	}
	if(ws == NULL) {
		return;
	}
	GotoWorkSpace(vs, ws);
}


void GotoPrevWorkSpace(VirtualScreen *vs)
{
	WorkSpace *ws1, *ws2;

	if(! Scr->workSpaceManagerActive) {
		return;
	}
	if(!vs) {
		return;
	}
	ws1 = Scr->workSpaceMgr.workSpaceList;
	if(ws1 == NULL) {
		return;
	}
	ws2 = ws1->next;

	while((ws2 != vs->wsw->currentwspc) && (ws2 != NULL)) {
		ws1 = ws2;
		ws2 = ws2->next;
	}
	GotoWorkSpace(vs, ws1);
}


void GotoNextWorkSpace(VirtualScreen *vs)
{
	WorkSpace *ws;
	if(! Scr->workSpaceManagerActive) {
		return;
	}
	if(!vs) {
		return;
	}

	ws = vs->wsw->currentwspc;
	ws = (ws->next != NULL) ? ws->next : Scr->workSpaceMgr.workSpaceList;
	GotoWorkSpace(vs, ws);
}


void GotoRightWorkSpace(VirtualScreen *vs)
{
	WorkSpace *ws;
	int number, columns, count;

	if(!Scr->workSpaceManagerActive) {
		return;
	}
	if(!vs) {
		return;
	}

	ws      = vs->wsw->currentwspc;
	number  = ws->number;
	columns = Scr->workSpaceMgr.columns;
	count   = Scr->workSpaceMgr.count;
	number++;
	if((number % columns) == 0) {
		number -= columns;
	}
	else if(number >= count) {
		number = (number / columns) * columns;
	}

	GotoWorkSpaceByNumber(vs, number);
}


void GotoLeftWorkSpace(VirtualScreen *vs)
{
	WorkSpace *ws;
	int number, columns, count;

	if(!Scr->workSpaceManagerActive) {
		return;
	}
	if(!vs) {
		return;
	}

	ws      = vs->wsw->currentwspc;
	number  = ws->number;
	columns = Scr->workSpaceMgr.columns;
	count   = Scr->workSpaceMgr.count;
	number += (number % columns) ? -1 : (columns - 1);
	if(number >= count) {
		number = count - 1;
	}
	GotoWorkSpaceByNumber(vs, number);
}


void GotoUpWorkSpace(VirtualScreen *vs)
{
	WorkSpace *ws;
	int number, lines, columns, count;

	if(!Scr->workSpaceManagerActive) {
		return;
	}
	if(!vs) {
		return;
	}

	ws      = vs->wsw->currentwspc;
	number  = ws->number;
	lines   = Scr->workSpaceMgr.lines;
	columns = Scr->workSpaceMgr.columns;
	count   = Scr->workSpaceMgr.count;
	number -=  columns;
	if(number < 0) {
		number += lines * columns;
		/* If the number of workspaces is not a multiple of nr of columns */
		if(number >= count) {
			number -= columns;
		}
	}
	GotoWorkSpaceByNumber(vs, number);
}


void GotoDownWorkSpace(VirtualScreen *vs)
{
	WorkSpace *ws;
	int number, columns, count;

	if(!Scr->workSpaceManagerActive) {
		return;
	}
	if(!vs) {
		return;
	}

	ws      = vs->wsw->currentwspc;
	number  = ws->number;
	columns = Scr->workSpaceMgr.columns;
	count   = Scr->workSpaceMgr.count;
	number +=  columns;
	if(number >= count) {
		number %= columns;
	}
	GotoWorkSpaceByNumber(vs, number);
}



/*
 * Show the background (by hiding all windows) or undo it.
 * f.showbackground, also can be called via EWMH bits.
 *
 * newstate is the desired showing state.
 * Pass -1 to toggle, 1 to show the background,
 * or 0 to re-show the windows.
 *
 * XXX Doesn't really belong here; more of a functions.c-ish thing
 * probably.  But left here for the moment.
 */
void ShowBackground(VirtualScreen *vs, int newstate)
{
	static int state = 0;
	TwmWindow *twmWin;

	if(newstate == state) {
		return;
	}

	if(state) {
		for(twmWin = Scr->FirstWindow; twmWin != NULL; twmWin = twmWin->next) {
			if(twmWin->savevs == vs) {
				DisplayWin(vs, twmWin);
			}
			twmWin->savevs = NULL;
		}
		state = 0;
	}
	else {
		for(twmWin = Scr->FirstWindow; twmWin != NULL; twmWin = twmWin->next) {
			if(twmWin->vs == vs
#ifdef EWMH
			                /* leave wt_Desktop and wt_Dock visible */
			                && twmWin->ewmhWindowType == wt_Normal
#endif /* EWMH */
			  ) {
				twmWin->savevs = twmWin->vs;
				Vanish(vs, twmWin);
			}
		}
		state = 1;
	}
#ifdef EWMH
	EwmhSet_NET_SHOWING_DESKTOP(state);
#endif /* EWMH */
}


/*
 * Belongs with the above GotoWorkSpace* funcs
 */
void GotoWorkSpace(VirtualScreen *vs, WorkSpace *ws)
{
	TwmWindow            *twmWin;
	WorkSpace            *oldws, *newws;
	WList                *wl, *wl1;
	WinList              winl;
	XSetWindowAttributes attr;
	XWindowAttributes    winattrs;
	unsigned long        eventMask;
	IconMgr              *iconmgr;
	Window               oldw;
	Window               neww;
	TwmWindow            *focuswindow;
	VirtualScreen        *tmpvs;

	if(! Scr->workSpaceManagerActive) {
		return;
	}
	for(tmpvs = Scr->vScreenList; tmpvs != NULL; tmpvs = tmpvs->next) {
		if(ws == tmpvs->wsw->currentwspc) {
			XBell(dpy, 0);
			return;
		}
	}
	oldws = vs->wsw->currentwspc;
	newws = ws;
	if(oldws == newws) {
		return;
	}

	attr.backing_store = NotUseful;
	attr.save_under    = False;

	if(useBackgroundInfo && ! Scr->DontPaintRootWindow) {
		if(newws->image == NULL) {
			XSetWindowBackground(dpy, vs->window, newws->backcp.back);
		}
		else {
			XSetWindowBackgroundPixmap(dpy, vs->window, newws->image->pixmap);
		}
		XClearWindow(dpy, vs->window);
	}

	/* If SaveWorkspaceFocus is on, save the focus of the last window. */
	if(Scr->SaveWorkspaceFocus) {
		oldws->save_focus = Scr->Focus;
	}

	focuswindow = NULL;
	/* For better visual effect, the order or map/unmap is important:
	   - map from top to bottom.
	   - unmap from bottom to top.
	   - unmap after mapping.
	   The guiding factor: at any point during the transition, something
	   should be visible only if it was visible before the transition or if
	   it will be visible at the end.  */
	OtpCheckConsistency();

	for(twmWin = OtpTopWin(); twmWin != NULL;
	                twmWin = OtpNextWinDown(twmWin)) {

		if(OCCUPY(twmWin, newws)) {
			if(!twmWin->vs) {
				DisplayWin(vs, twmWin);
			}
#ifdef EWMH
			if(OCCUPY(twmWin, oldws)) {
				/*
				 * If the window remains visible, re-order the workspace
				 * numbers in NET_WM_DESKTOP.
				 */
				EwmhSet_NET_WM_DESKTOP_ws(twmWin, newws);
			}
#endif
		}
	}

	for(twmWin = OtpBottomWin(); twmWin != NULL;
	                twmWin = OtpNextWinUp(twmWin)) {
		if(twmWin->vs == vs) {
			if(!OCCUPY(twmWin, newws)) {
				VirtualScreen *tvs;

				Vanish(vs, twmWin);
				/*
				 * Now that the window has Vanished from one virtual screen,
				 * check to see if it is wanted on another one.
				 * This is relatively rare, so don't bother with the
				 * top-to-bottom order here.
				 */
				if(Scr->numVscreens > 1) {
					for(tvs = Scr->vScreenList; tvs != NULL; tvs = tvs->next) {
						if(tvs == vs) { /* no, not back on the old one */
							continue;
						}
						if(OCCUPY(twmWin, tvs->wsw->currentwspc)) {
							DisplayWin(tvs, twmWin);
							break;
						}
					}
				}
			}
			else if(twmWin->hasfocusvisible) {
				focuswindow = twmWin;
				SetFocusVisualAttributes(focuswindow, false);
			}
		}
	}
	OtpCheckConsistency();

	/*
	   Reorganize icon manager window lists
	*/
	for(twmWin = Scr->FirstWindow; twmWin != NULL; twmWin = twmWin->next) {
		wl = twmWin->iconmanagerlist;
		if(wl == NULL) {
			continue;
		}
		if(OCCUPY(wl->iconmgr->twm_win, newws)) {
			continue;
		}
		wl1 = wl;
		wl  = wl->nextv;
		while(wl != NULL) {
			if(OCCUPY(wl->iconmgr->twm_win, newws)) {
				break;
			}
			wl1 = wl;
			wl  = wl->nextv;
		}
		if(wl != NULL) {
			wl1->nextv = wl->nextv;
			wl->nextv  = twmWin->iconmanagerlist;
			twmWin->iconmanagerlist = wl;
		}
	}
	wl = NULL;
	for(iconmgr = newws->iconmgr; iconmgr; iconmgr = iconmgr->next) {
		if(iconmgr->first) {
			wl = iconmgr->first;
			break;
		}
	}
	CurrentIconManagerEntry(wl);
	if(focuswindow) {
		SetFocusVisualAttributes(focuswindow, true);
	}
	vs->wsw->currentwspc = newws;
	if(Scr->ReverseCurrentWorkspace && vs->wsw->state == WMS_map) {
		MapSubwindow *msw = vs->wsw->mswl [oldws->number];
		for(winl = msw->wl; winl != NULL; winl = winl->next) {
			WMapRedrawName(vs, winl);
		}
		msw = vs->wsw->mswl [newws->number];
		for(winl = msw->wl; winl != NULL; winl = winl->next) {
			WMapRedrawName(vs, winl);
		}
	}
	else if(vs->wsw->state == WMS_buttons) {
		ButtonSubwindow *bsw = vs->wsw->bswl [oldws->number];
		PaintWsButton(WSPCWINDOW, vs, bsw->w, oldws->label, oldws->cp, off);
		bsw = vs->wsw->bswl [newws->number];
		PaintWsButton(WSPCWINDOW, vs, bsw->w, newws->label, newws->cp,  on);
	}
	oldws->iconmgr = Scr->iconmgr;
	Scr->iconmgr = newws->iconmgr;

	oldw = vs->wsw->mswl [oldws->number]->w;
	neww = vs->wsw->mswl [newws->number]->w;
	if(useBackgroundInfo) {
		if(oldws->image == NULL || Scr->NoImagesInWorkSpaceManager) {
			XSetWindowBackground(dpy, oldw, oldws->backcp.back);
		}
		else {
			XSetWindowBackgroundPixmap(dpy, oldw, oldws->image->pixmap);
		}
	}
	else {
		if(Scr->workSpaceMgr.defImage == NULL || Scr->NoImagesInWorkSpaceManager) {
			XSetWindowBackground(dpy, oldw, Scr->workSpaceMgr.defColors.back);
		}
		else {
			XSetWindowBackgroundPixmap(dpy, oldw, Scr->workSpaceMgr.defImage->pixmap);
		}
	}
	attr.border_pixel = Scr->workSpaceMgr.defBorderColor;
	XChangeWindowAttributes(dpy, oldw, CWBorderPixel, &attr);

	if(Scr->workSpaceMgr.curImage == NULL) {
		if(Scr->workSpaceMgr.curPaint) {
			XSetWindowBackground(dpy, neww, Scr->workSpaceMgr.curColors.back);
		}
	}
	else {
		XSetWindowBackgroundPixmap(dpy, neww, Scr->workSpaceMgr.curImage->pixmap);
	}
	attr.border_pixel =  Scr->workSpaceMgr.curBorderColor;
	XChangeWindowAttributes(dpy, neww, CWBorderPixel, &attr);

	XClearWindow(dpy, oldw);
	XClearWindow(dpy, neww);

	XGetWindowAttributes(dpy, Scr->Root, &winattrs);
	eventMask = winattrs.your_event_mask;
	XSelectInput(dpy, Scr->Root, eventMask & ~PropertyChangeMask);

	XChangeProperty(dpy, Scr->Root, XA_WM_CURRENTWORKSPACE, XA_STRING, 8,
	                PropModeReplace, (unsigned char *) newws->name, strlen(newws->name));
#ifdef EWMH
	{
		long number = newws->number;
		/*
		 * TODO: this should probably not use Scr->Root but ->XineramaRoot.
		 * That is the real root window if we're using virtual screens.
		 * Also, on the real root it would need values for each of the
		 * virtual roots, but that doesn't fit in the EWMH ideas.
		 */
		XChangeProperty(dpy, Scr->Root, XA__NET_CURRENT_DESKTOP,
		                XA_CARDINAL, 32,
		                PropModeReplace, (unsigned char *) &number, 1);
	}
#endif /* EWMH */
	XSelectInput(dpy, Scr->Root, eventMask);

	/*    XDestroyWindow (dpy, cachew);*/
	if(Scr->ChangeWorkspaceFunction.func != 0) {
		char *action;
		XEvent event;

		action = Scr->ChangeWorkspaceFunction.item ?
		         Scr->ChangeWorkspaceFunction.item->action : NULL;
		ExecuteFunction(Scr->ChangeWorkspaceFunction.func, action,
		                (Window) 0, NULL, &event, C_ROOT, false);
	}

	/* If SaveWorkspaceFocus is on, try to restore the focus to the last
	   window which was focused when we left this workspace. */
	if(Scr->SaveWorkspaceFocus && newws->save_focus) {
		twmWin = newws->save_focus;
		if(OCCUPY(twmWin, newws)) {     /* check should not even be needed anymore */
			WarpToWindow(twmWin, false);
		}
		else {
			newws->save_focus = NULL;
		}
	}

	/* keep track of the order of the workspaces across restarts */
	CtwmSetVScreenMap(dpy, Scr->Root, Scr->vScreenList);

	XSync(dpy, 0);
	if(Scr->ClickToFocus || Scr->SloppyFocus) {
		set_last_window(newws);
	}
	MaybeAnimate = true;
}


/*
 * Get the name of the currently active WS.  Used in Execute() for
 * sub'ing in $currentworkspace in executing commands.
 */
char *GetCurrentWorkSpaceName(VirtualScreen *vs)
{
	if(! Scr->workSpaceManagerActive) {
		return (NULL);
	}
	if(!vs) {
		vs = Scr->vScreenList;
	}
	return vs->wsw->currentwspc->name;
}


/*
 * Create a workspace.  This is what gets called when parsing
 * WorkSpaces {} config file entries.
 *
 * WorkSpaces {
 *  "One" { name background foreground backback backfore "backpix.jpg" }
 *  #         |      |           |        |       |            |
 *  #     WS name    |      Button Text   |   Map/Root FG      |
 *  #            Button BG             Map BG            Map/Root BG img
 * }
 */
void
AddWorkSpace(const char *name, const char *background, const char *foreground,
             const char *backback, const char *backfore, const char *backpix)
{
	WorkSpace *ws;

	/* XXX Shouldn't just silently return if we're already at max...  */
	if(Scr->workSpaceMgr.count == MAXWORKSPACE) {
		return;
	}

	/* Init.  Label can change over time, but starts the same as name. */
	ws = calloc(1, sizeof(WorkSpace));
	ws->name  = strdup(name);
	ws->label = strdup(name);
	ws->number = Scr->workSpaceMgr.count++;

	/* We're a new entry on the "everything" list */
	fullOccupation |= (1 << ws->number);


	/*
	 * FB/BG colors for the button state may be specified, or fallback to
	 * the icon manager's if not.
	 */
	if(background == NULL) {
		ws->cp.back = Scr->IconManagerC.back;
	}
	else {
		GetColor(Scr->Monochrome, &(ws->cp.back), background);
	}

	if(foreground == NULL) {
		ws->cp.fore = Scr->IconManagerC.fore;
	}
	else {
		GetColor(Scr->Monochrome, &(ws->cp.fore), foreground);
	}

	/* Shadows for 3d buttons derived from that */
#ifdef COLOR_BLIND_USER
	ws->cp.shadc = Scr->White;
	ws->cp.shadd = Scr->Black;
#else
	if(!Scr->BeNiceToColormap) {
		GetShadeColors(&ws->cp);
	}
#endif


	/*
	 * Map-state fb/bg color, as well as root win background.
	 */
	if(backback == NULL) {
		GetColor(Scr->Monochrome, &(ws->backcp.back), "Black");
	}
	else {
		GetColor(Scr->Monochrome, &(ws->backcp.back), backback);
		useBackgroundInfo = true;
	}

	if(backfore == NULL) {
		GetColor(Scr->Monochrome, &(ws->backcp.fore), "White");
	}
	else {
		GetColor(Scr->Monochrome, &(ws->backcp.fore), backfore);
		useBackgroundInfo = true;
	}


	/* Maybe there's an image to stick on the root as well */
	ws->image = GetImage(backpix, ws->backcp);
	if(ws->image != NULL) {
		useBackgroundInfo = true;
	}


	/* Put ourselves on the end of the workspace list */
	if(Scr->workSpaceMgr.workSpaceList == NULL) {
		Scr->workSpaceMgr.workSpaceList = ws;
	}
	else {
		WorkSpace *wstmp = Scr->workSpaceMgr.workSpaceList;
		while(wstmp->next != NULL) {
			wstmp = wstmp->next;
		}
		wstmp->next = ws;
	}

	/* There's at least one defined WS now */
	Scr->workSpaceManagerActive = true;

	return;
}


/*
 * Find workspace by name
 */
WorkSpace *
GetWorkspace(char *wname)
{
	WorkSpace *ws;

	/* Guard */
	if(!wname) {
		return (NULL);
	}

	/* Check by label */
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(strcmp(ws->label, wname) == 0) {
			return ws;
		}
	}

	/* Check by name */
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(strcmp(ws->name, wname) == 0) {
			return ws;
		}
	}

	/* Nope */
	return NULL;
}


/*
 * Move a window's frame and icon to a new VS.  This mostly happens as a
 * backend bit of the DisplayWin() process, but it does get called
 * directly for the Occupy window.  XXX Should it?
 */
void
ReparentFrameAndIcon(TwmWindow *tmp_win)
{
	VirtualScreen *vs = tmp_win->vs; /* which virtual screen we want it in */

	/* parent_vs is the current real parent of the window */
	if(vs != tmp_win->parent_vs) {
		struct Icon *icon = tmp_win->icon;

		tmp_win->parent_vs = vs;

		if(icon && icon->w) {
			ReparentWindowAndIcon(dpy, tmp_win, vs->window,
			                      tmp_win->frame_x, tmp_win->frame_y,
			                      icon->w_x, icon->w_y);
		}
		else {
			ReparentWindow(dpy, tmp_win,  WinWin, vs->window,
			               tmp_win->frame_x, tmp_win->frame_y);
		}
	}
}


/*
 * Get this window outta here.  Note that despite naming, this is
 * unrelated to f.vanish.
 */
void
Vanish(VirtualScreen *vs, TwmWindow *tmp_win)
{
	/* It's not here?  Nothing to do. */
	if(vs && tmp_win->vs && tmp_win->vs != vs) {
		return;
	}

	/* Unmap (or near-equivalent) all its bits */
	if(tmp_win->UnmapByMovingFarAway) {
		/* UnmapByMovingFarAway?  Move it off-screen */
		XMoveWindow(dpy, tmp_win->frame, Scr->rootw + 1, Scr->rooth + 1);
	}
	else if(tmp_win->mapped) {
		/* It's mapped; unmap it */
		XWindowAttributes winattrs;
		unsigned long     eventMask;

		XGetWindowAttributes(dpy, tmp_win->w, &winattrs);
		eventMask = winattrs.your_event_mask;
		XSelectInput(dpy, tmp_win->w, eventMask & ~StructureNotifyMask);
		XUnmapWindow(dpy, tmp_win->w);
		XUnmapWindow(dpy, tmp_win->frame);
		XSelectInput(dpy, tmp_win->w, eventMask);

		if(!tmp_win->DontSetInactive) {
			SetMapStateProp(tmp_win, InactiveState);
		}
	}
	else if(tmp_win->icon_on && tmp_win->icon && tmp_win->icon->w) {
		/* It's not mapped, but the icon's up; hide it away */
		XUnmapWindow(dpy, tmp_win->icon->w);
		IconDown(tmp_win);
	}

#if 0
	/*
	 * The purpose of this is in the event of a ctwm death/restart,
	 * geometries of windows that were on unmapped workspaces will show
	 * up where they belong.
	 * XXX - I doubt its usefulness, since still-mapped windows won't
	 * enjoy this "protection", making it suboptimal at best.
	 * XXX - XReparentWindow() messes up the stacking order of windows.
	 * It should be avoided as much as possible. This already affects
	 * switching away from and back to a workspace. Therefore do this only
	 * if there are at least 2 virtual screens AND the new one (firstvs)
	 * differs from where the window currently is. (Olaf Seibert).
	 */

	if(Scr->numVscreens > 1) {
		int x, y;
		unsigned int junk;
		Window junkW, w = tmp_win->frame;
		VirtualScreen *firstvs = NULL;

		for(firstvs = Scr->vScreenList; firstvs; firstvs = firstvs->next)
			if(firstvs->x == 0 && firstvs->y == 0) {
				break;
			}
		if(firstvs && firstvs != vs) {
			tmp_win->vs = firstvs;
			ReparentFrameAndIcon(tmp_win);
		}
	}
#endif

	/* Currently displayed nowhere */
	tmp_win->vs = NULL;
}


/*
 * Display a window in a given virtual screen.
 */
void
DisplayWin(VirtualScreen *vs, TwmWindow *tmp_win)
{
	OtpCheckConsistency();
	DisplayWinUnchecked(vs, tmp_win);
	OtpCheckConsistency();
}

static void DisplayWinUnchecked(VirtualScreen *vs, TwmWindow *tmp_win)
{
	/*
	 * A window cannot be shown in multiple virtual screens, even if
	 * it occupies both corresponding workspaces.
	 */
	if(vs && tmp_win->vs) {
		return;
	}

	/* This is where we're moving it */
	tmp_win->vs = vs;


	/* If it's unmapped, RFAI() moves the necessary bits here */
	if(!tmp_win->mapped) {
		ReparentFrameAndIcon(tmp_win);

		/* If it's got an icon that should be up, make it up here */
		if(tmp_win->isicon) {
			if(tmp_win->icon_on) {
				if(tmp_win->icon && tmp_win->icon->w) {

					IconUp(tmp_win);
					XMapWindow(dpy, tmp_win->icon->w);
				}
			}
		}

		/* All there is to do with unmapped wins */
		return;
	}


	/* If we make it this far, the window is mapped */

	if(tmp_win->UnmapByMovingFarAway) {
		/*
		 * XXX I don't believe the handling of UnmapByMovingFarAway is
		 * quite correct.
		 */
		if(vs) {
			XReparentWindow(dpy, tmp_win->frame, vs->window,
			                tmp_win->frame_x, tmp_win->frame_y);
		}
		else {
			XMoveWindow(dpy, tmp_win->frame, tmp_win->frame_x, tmp_win->frame_y);
		}
	}
	else {
		/* Map and move it here */
		if(!tmp_win->squeezed) {
			XWindowAttributes   winattrs;
			unsigned long       eventMask;

			XGetWindowAttributes(dpy, tmp_win->w, &winattrs);
			eventMask = winattrs.your_event_mask;
			XSelectInput(dpy, tmp_win->w, eventMask & ~StructureNotifyMask);
			XMapWindow(dpy, tmp_win->w);
			XSelectInput(dpy, tmp_win->w, eventMask);
		}

		ReparentFrameAndIcon(tmp_win);

		XMapWindow(dpy, tmp_win->frame);
		SetMapStateProp(tmp_win, NormalState);
	}
}


/*
 * Put together the actual window for the workspace manager.  Called as
 * part of CreateWorkSpaceManager() during startup, once per workspace
 * (since there's a separate window in each).
 */
static void CreateWorkSpaceManagerWindow(VirtualScreen *vs)
{
	int           mask;
	int           lines, vspace, hspace, count, columns;
	unsigned int  width, height, bwidth, bheight;
	char          *name, *icon_name, *geometry;
	int           i, j;
	ColorPair     cp;
	MyFont        font;
	WorkSpace     *ws;
	int           x, y, strWid, wid;
	unsigned long border;
	TwmWindow     *tmp_win;
	XSetWindowAttributes        attr;
	XWindowAttributes           wattr;
	unsigned long               attrmask;
	XSizeHints    sizehints;
	XWMHints      wmhints;
	int           gravity;
	XRectangle inc_rect;
	XRectangle logical_rect;

	name      = Scr->workSpaceMgr.name;
	icon_name = Scr->workSpaceMgr.icon_name;
	geometry  = Scr->workSpaceMgr.geometry;
	columns   = Scr->workSpaceMgr.columns;
	vspace    = Scr->workSpaceMgr.vspace;
	hspace    = Scr->workSpaceMgr.hspace;
	font      = Scr->workSpaceMgr.buttonFont;
	cp        = Scr->workSpaceMgr.cp;
	border    = Scr->workSpaceMgr.defBorderColor;

	count = 0;
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		count++;
	}
	Scr->workSpaceMgr.count = count;
	if(count == 0) {
		return;
	}

	if(columns == 0) {
		lines   = 2;
		columns = ((count - 1) / lines) + 1;
	}
	else {
		lines   = ((count - 1) / columns) + 1;
	}
	Scr->workSpaceMgr.lines   = lines;
	Scr->workSpaceMgr.columns = columns;

	strWid = 0;
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		XmbTextExtents(font.font_set, ws->label, strlen(ws->label),
		               &inc_rect, &logical_rect);
		wid = logical_rect.width;
		if(wid > strWid) {
			strWid = wid;
		}
	}
	if(geometry != NULL) {
		mask = XParseGeometry(geometry, &x, &y, &width, &height);
		bwidth  = (mask & WidthValue)  ? ((width - (columns * hspace)) / columns) :
		          strWid + 10;
		bheight = (mask & HeightValue) ? ((height - (lines  * vspace)) / lines) : 22;
		width   = columns * (bwidth  + hspace);
		height  = lines   * (bheight + vspace);

		if(!(mask & YValue)) {
			y = 0;
			mask |= YNegative;
		}
		if(mask & XValue) {
			if(mask & XNegative) {
				x += vs->w - width;
				gravity = (mask & YNegative) ? SouthEastGravity : NorthEastGravity;
			}
			else {
				gravity = (mask & YNegative) ? SouthWestGravity : NorthWestGravity;
			}
		}
		else {
			x = (vs->w - width) / 2;
			gravity = (mask & YValue) ? ((mask & YNegative) ?
			                             SouthGravity : NorthGravity) : SouthGravity;
		}
		if(mask & YNegative) {
			y += vs->h - height;
		}
	}
	else {
		bwidth  = strWid + 2 * Scr->WMgrButtonShadowDepth + 6;
		bheight = 22;
		width   = columns * (bwidth  + hspace);
		height  = lines   * (bheight + vspace);
		x       = (vs->w - width) / 2;
		y       = vs->h - height;
		gravity = NorthWestGravity;
	}

#define Dummy   1

	vs->wsw->width   = Dummy;
	vs->wsw->height  = Dummy;
	vs->wsw->bswl = calloc(Scr->workSpaceMgr.count, sizeof(ButtonSubwindow *));
	vs->wsw->mswl = calloc(Scr->workSpaceMgr.count, sizeof(MapSubwindow *));

	vs->wsw->w = XCreateSimpleWindow(dpy, Scr->Root, x, y, width, height, 0,
	                                 Scr->Black, cp.back);
	i = 0;
	j = 0;
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		Window mapsw, butsw;
		MapSubwindow *msw;
		ButtonSubwindow *bsw;

		vs->wsw->bswl [ws->number] = bsw = malloc(sizeof(ButtonSubwindow));
		vs->wsw->mswl [ws->number] = msw = malloc(sizeof(MapSubwindow));

		butsw = bsw->w =
		                XCreateSimpleWindow(dpy, vs->wsw->w,
		                                    Dummy /* x */, Dummy /* y */,
		                                    Dummy /* width */, Dummy /* height */,
		                                    0, Scr->Black, ws->cp.back);

		mapsw = msw->w =
		                XCreateSimpleWindow(dpy, vs->wsw->w,
		                                    Dummy /* x */, Dummy /* y */,
		                                    Dummy /* width */, Dummy /* height */,
		                                    1, border, ws->cp.back);

		if(vs->wsw->state == WMS_buttons) {
			XMapWindow(dpy, butsw);
		}
		else {
			XMapWindow(dpy, mapsw);
		}

		vs->wsw->mswl [ws->number]->wl = NULL;
		if(useBackgroundInfo) {
			if(ws->image == NULL || Scr->NoImagesInWorkSpaceManager) {
				XSetWindowBackground(dpy, mapsw, ws->backcp.back);
			}
			else {
				XSetWindowBackgroundPixmap(dpy, mapsw, ws->image->pixmap);
			}
		}
		else {
			if(Scr->workSpaceMgr.defImage == NULL || Scr->NoImagesInWorkSpaceManager) {
				XSetWindowBackground(dpy, mapsw, Scr->workSpaceMgr.defColors.back);
			}
			else {
				XSetWindowBackgroundPixmap(dpy, mapsw, Scr->workSpaceMgr.defImage->pixmap);
			}
		}
		XClearWindow(dpy, butsw);
		i++;
		if(i == columns) {
			i = 0;
			j++;
		};
	}

	sizehints.flags       = USPosition | PBaseSize | PMinSize | PResizeInc |
	                        PWinGravity;
	sizehints.x           = x;
	sizehints.y           = y;
	sizehints.base_width  = columns * hspace;
	sizehints.base_height = lines   * vspace;
	sizehints.width_inc   = columns;
	sizehints.height_inc  = lines;
	sizehints.min_width   = columns  * (hspace + 2);
	sizehints.min_height  = lines    * (vspace + 2);
	sizehints.win_gravity = gravity;

	wmhints.flags         = InputHint | StateHint;
	wmhints.input         = True;
	wmhints.initial_state = NormalState;

	XmbSetWMProperties(dpy, vs->wsw->w, name, icon_name, NULL, 0,
	                   &sizehints, &wmhints, NULL);

	tmp_win = AddWindow(vs->wsw->w, AWT_WORKSPACE_MANAGER,
	                    Scr->iconmgr, vs);
	if(! tmp_win) {
		fprintf(stderr, "cannot create workspace manager window, exiting...\n");
		exit(1);
	}
	tmp_win->occupation = fullOccupation;
	tmp_win->attr.width = width;
	tmp_win->attr.height = height;
	ResizeWorkSpaceManager(vs, tmp_win);

	attrmask = 0;
	attr.cursor = Scr->ButtonCursor;
	attrmask |= CWCursor;
	attr.win_gravity = gravity;
	attrmask |= CWWinGravity;
	XChangeWindowAttributes(dpy, vs->wsw->w, attrmask, &attr);

	XGetWindowAttributes(dpy, vs->wsw->w, &wattr);
	attrmask = wattr.your_event_mask | KeyPressMask | KeyReleaseMask | ExposureMask;
	XSelectInput(dpy, vs->wsw->w, attrmask);

	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		Window buttonw = vs->wsw->bswl [ws->number]->w;
		Window mapsubw = vs->wsw->mswl [ws->number]->w;
		XSelectInput(dpy, buttonw, ButtonPressMask | ButtonReleaseMask | ExposureMask);
		XSaveContext(dpy, buttonw, TwmContext, (XPointer) tmp_win);
		XSaveContext(dpy, buttonw, ScreenContext, (XPointer) Scr);

		XSelectInput(dpy, mapsubw, ButtonPressMask | ButtonReleaseMask);
		XSaveContext(dpy, mapsubw, TwmContext, (XPointer) tmp_win);
		XSaveContext(dpy, mapsubw, ScreenContext, (XPointer) Scr);
	}
	SetMapStateProp(tmp_win, WithdrawnState);
	vs->wsw->twm_win = tmp_win;

	ws = Scr->workSpaceMgr.workSpaceList;
	if(useBackgroundInfo && ! Scr->DontPaintRootWindow) {
		if(ws->image == NULL) {
			XSetWindowBackground(dpy, Scr->Root, ws->backcp.back);
		}
		else {
			XSetWindowBackgroundPixmap(dpy, Scr->Root, ws->image->pixmap);
		}
		XClearWindow(dpy, Scr->Root);
	}
	PaintWorkSpaceManager(vs);
}


/*
 * Draw a workspace manager window on expose
 */
void WMgrHandleExposeEvent(VirtualScreen *vs, XEvent *event)
{
	WorkSpace *ws;
	Window buttonw;

	if(vs->wsw->state == WMS_buttons) {
		for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
			buttonw = vs->wsw->bswl [ws->number]->w;
			if(event->xexpose.window == buttonw) {
				break;
			}
		}
		if(ws == NULL) {
			PaintWorkSpaceManagerBorder(vs);
		}
		else if(ws == vs->wsw->currentwspc) {
			PaintWsButton(WSPCWINDOW, vs, buttonw, ws->label, ws->cp, on);
		}
		else {
			PaintWsButton(WSPCWINDOW, vs, buttonw, ws->label, ws->cp, off);
		}
	}
	else {
		WinList   wl;

		if(XFindContext(dpy, event->xexpose.window, MapWListContext,
		                (XPointer *) &wl) == XCNOENT) {
			return;
		}
		if(wl && wl->twm_win && wl->twm_win->mapped) {
			WMapRedrawName(vs, wl);
		}
	}
}


/*
 * Draw up the pieces of a WSM window.  This is subtly different from the
 * expose handler because XXX ???
 */
void PaintWorkSpaceManager(VirtualScreen *vs)
{
	WorkSpace *ws;

	PaintWorkSpaceManagerBorder(vs);
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		Window buttonw = vs->wsw->bswl [ws->number]->w;
		if(ws == vs->wsw->currentwspc) {
			PaintWsButton(WSPCWINDOW, vs, buttonw, ws->label, ws->cp, on);
		}
		else {
			PaintWsButton(WSPCWINDOW, vs, buttonw, ws->label, ws->cp, off);
		}
	}
}


/*
 * Border around the WSM
 */
static void PaintWorkSpaceManagerBorder(VirtualScreen *vs)
{
	int width, height;

	width  = vs->wsw->width;
	height = vs->wsw->height;
	Draw3DBorder(vs->wsw->w, 0, 0, width, height, 2, Scr->workSpaceMgr.cp, off,
	             true, false);
}


/*
 * Moving the WSM between button and map state
 */
void WMapToggleState(VirtualScreen *vs)
{
	if(vs->wsw->state == WMS_buttons) {
		WMapSetMapState(vs);
	}
	else {
		WMapSetButtonsState(vs);
	}
}

void WMapSetMapState(VirtualScreen *vs)
{
	WorkSpace     *ws;

	if(vs->wsw->state == WMS_map) {
		return;
	}
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		XUnmapWindow(dpy, vs->wsw->bswl [ws->number]->w);
		XMapWindow(dpy, vs->wsw->mswl [ws->number]->w);
	}
	vs->wsw->state = WMS_map;
	MaybeAnimate = true;
}

void WMapSetButtonsState(VirtualScreen *vs)
{
	WorkSpace     *ws;

	if(vs->wsw->state == WMS_buttons) {
		return;
	}
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		XUnmapWindow(dpy, vs->wsw->mswl [ws->number]->w);
		XMapWindow(dpy, vs->wsw->bswl [ws->number]->w);
	}
	vs->wsw->state = WMS_buttons;
}


/*
 * Verify if a window may be added to the workspace map.
 * This is not allowed for
 * - icon managers
 * - the occupy window
 * - workspace manager windows
 * - or, optionally, windows with full occupation.
 */
bool
WMapWindowMayBeAdded(TwmWindow *win)
{
	if(win->isiconmgr) {
		return false;
	}
	if(win == Scr->workSpaceMgr.occupyWindow->twm_win) {
		return false;
	}
	if(win->iswspmgr) {
		return false;
	}
	if(Scr->workSpaceMgr.noshowoccupyall &&
	                win->occupation == fullOccupation) {
		return false;
	}
	return true;
}


/*
 * Add a window into any appropriate WSM's [data structure].  Called
 * during AddWindow().
 */
void WMapAddWindow(TwmWindow *win)
{
	WorkSpace     *ws;

	if(!WMapWindowMayBeAdded(win)) {
		return;
	}

	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(OCCUPY(win, ws)) {
			WMapAddToList(win, ws);
		}
	}
}


/*
 * Remove a window from any WSM's [data structures].  Called during
 * window destruction process.
 */
void WMapDestroyWindow(TwmWindow *win)
{
	WorkSpace *ws;

	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(OCCUPY(win, ws)) {
			WMapRemoveFromList(win, ws);
		}
	}
	/* XXX Better belongs inline in caller or separate func? */
	if(win == occupyWin) {
		OccupyWindow *occwin = Scr->workSpaceMgr.occupyWindow;
		XUnmapWindow(dpy, occwin->twm_win->frame);
		occwin->twm_win->mapped = false;
		occwin->twm_win->occupation = 0;
		occupyWin = NULL;
	}
}


/*
 * Map up a window's subwindow in the map-mode WSM.  Happens when a
 * window is de-iconified or otherwise mapped.  Specifically, when we get
 * (or fake) a Map request event.  x-ref comment on WMapDeIconify() for
 * some subtle distinctions between the two...
 */
void WMapMapWindow(TwmWindow *win)
{
	VirtualScreen *vs;
	WorkSpace *ws;
	WinList   wl;

	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
			for(wl = vs->wsw->mswl [ws->number]->wl; wl != NULL; wl = wl->next) {
				if(wl->twm_win == win) {
					XMapWindow(dpy, wl->w);
					WMapRedrawName(vs, wl);
					break;
				}
			}
		}
	}
}


/*
 * Position a window in the WSM.  Happens as a result of moving things.
 */
void WMapSetupWindow(TwmWindow *win, int x, int y, int w, int h)
{
	VirtualScreen *vs;
	WorkSpace     *ws;
	WinList       wl;

	if(win->isiconmgr) {
		return;
	}
	if(!win->vs) {
		return;
	}

	if(win->iswspmgr) {
		if(w == -1) {
			return;
		}
		ResizeWorkSpaceManager(win->vs, win);
		return;
	}
	if(win == Scr->workSpaceMgr.occupyWindow->twm_win) {
		if(w == -1) {
			return;
		}
		ResizeOccupyWindow(win);
		return;
	}
	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		WorkSpaceWindow *wsw = vs->wsw;
		float wf = (float)(wsw->wwidth  - 2) / (float) vs->w;
		float hf = (float)(wsw->wheight - 2) / (float) vs->h;

		for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
			for(wl = wsw->mswl [ws->number]->wl; wl != NULL; wl = wl->next) {
				if(win == wl->twm_win) {
					wl->x = (int)(x * wf);
					wl->y = (int)(y * hf);
					if(w == -1) {
						XMoveWindow(dpy, wl->w, wl->x, wl->y);
					}
					else {
						wl->width  = (unsigned int)((w * wf) + 0.5);
						wl->height = (unsigned int)((h * hf) + 0.5);
						if(!Scr->use3Dwmap) {
							wl->width  -= 2;
							wl->height -= 2;
						}
						if(wl->width  < 1) {
							wl->width  = 1;
						}
						if(wl->height < 1) {
							wl->height = 1;
						}
						XMoveResizeWindow(dpy, wl->w, wl->x, wl->y, wl->width, wl->height);
					}
					break;
				}
			}
		}
	}
}


/*
 * Hide away a window in the WSM map.  Happens when win is iconified;
 * different from destruction.
 */
void WMapIconify(TwmWindow *win)
{
	VirtualScreen *vs;
	WorkSpace *ws;
	WinList    wl;

	if(!win->vs) {
		return;
	}

	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
			for(wl = vs->wsw->mswl [ws->number]->wl; wl != NULL; wl = wl->next) {
				if(win == wl->twm_win) {
					XUnmapWindow(dpy, wl->w);
					break;
				}
			}
		}
	}
}


/*
 * De-iconify a window in the WSM map.  The opposite of WMapIconify(),
 * and different from WMapMapWindow() in complicated ways.  WMMW() gets
 * called at the end of HandleMapRequest().  A little earlier in HMR(),
 * DeIconify() is (sometimes) called, which calls this function.  So,
 * anything that de-iconifies invokes this, but only when it happens via
 * a map event does WMMW() get called as well.
 *
 * XXX Does it make sense that they're separate?  They seem do be doing a
 * lot of the same stuff.  In fact, the only difference is apparently
 * that this auto-raises on !(NoRaiseDeIcon)?  This requires some further
 * investigation...  at the least, they should probably be collapsed
 * somehow with a conditional for that trivial difference.
 */
void WMapDeIconify(TwmWindow *win)
{
	VirtualScreen *vs;
	WorkSpace *ws;
	WinList    wl;

	if(!win->vs) {
		return;
	}

	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
			for(wl = vs->wsw->mswl [ws->number]->wl; wl != NULL; wl = wl->next) {
				if(win == wl->twm_win) {
					if(Scr->NoRaiseDeicon) {
						XMapWindow(dpy, wl->w);
					}
					else {
						XMapRaised(dpy, wl->w);
					}
					WMapRedrawName(win->vs, wl);
					break;
				}
			}
		}
	}
}


/*
 * Frontends for changing the stacking of windows in the WSM.
 *
 * XXX If these implementations really _should_ be identical, they should
 * be collapsed...
 */
void WMapRaiseLower(TwmWindow *win)
{
	WorkSpace *ws;

	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(OCCUPY(win, ws)) {
			WMapRestack(ws);
		}
	}
}

void WMapLower(TwmWindow *win)
{
	WorkSpace *ws;

	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(OCCUPY(win, ws)) {
			WMapRestack(ws);
		}
	}
}

void WMapRaise(TwmWindow *win)
{
	WorkSpace *ws;

	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(OCCUPY(win, ws)) {
			WMapRestack(ws);
		}
	}
}


/*
 * Backend for redoing the stacking of a window in the WSM.
 *
 * XXX Since this tends to get called iteratively, there's probably
 * something better we can do than doing all this relatively expensive
 * stuff over and over...
 */
void WMapRestack(WorkSpace *ws)
{
	VirtualScreen *vs;
	TwmWindow   *win;
	WinList     wl;
	Window      root;
	Window      parent;
	Window      *children, *smallws;
	unsigned int number;
	int         i, j;

	number = 0;
	XQueryTree(dpy, Scr->Root, &root, &parent, &children, &number);
	smallws = calloc(number, sizeof(Window));

	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		j = 0;
		for(i = number - 1; i >= 0; i--) {
			if(!(win = GetTwmWindow(children [i]))) {
				continue;
			}
			if(win->frame != children [i]) {
				continue;        /* skip icons */
			}
			if(! OCCUPY(win, ws)) {
				continue;
			}
			if(tracefile) {
				fprintf(tracefile, "WMapRestack : w = %lx, win = %p\n", children [i],
				        (void *)win);
				fflush(tracefile);
			}
			for(wl = vs->wsw->mswl [ws->number]->wl; wl != NULL; wl = wl->next) {
				if(tracefile) {
					fprintf(tracefile, "WMapRestack : wl = %p, twm_win = %p\n", (void *)wl,
					        (void *)wl->twm_win);
					fflush(tracefile);
				}
				if(win == wl->twm_win) {
					smallws [j++] = wl->w;
					break;
				}
			}
		}
		XRestackWindows(dpy, smallws, j);
	}
	XFree(children);
	free(smallws);
	return;
}


/*
 * Update stuff in the WSM when win's icon name changes
 */
void WMapUpdateIconName(TwmWindow *win)
{
	VirtualScreen *vs;
	WorkSpace *ws;
	WinList   wl;

	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
			for(wl = vs->wsw->mswl [ws->number]->wl; wl != NULL; wl = wl->next) {
				if(win == wl->twm_win) {
					WMapRedrawName(vs, wl);
					break;
				}
			}
		}
	}
}


/*
 * Key press/release events in the WSM.  A major use (and only for
 * release) is the Ctrl-key switching between map and button state.  The
 * other use is on-the-fly renaming of workspaces by typing in the
 * button-state WSM.
 */
void WMgrHandleKeyReleaseEvent(VirtualScreen *vs, XEvent *event)
{
	KeySym      keysym;

	keysym  = XLookupKeysym((XKeyEvent *) event, 0);
	if(! keysym) {
		return;
	}
	if(keysym == XK_Control_L || keysym == XK_Control_R) {
		/* DontToggleWorkSpaceManagerState added 20040607 by dl*/
		if(!Scr->DontToggleWorkspaceManagerState) {
			WMapToggleState(vs);
		}
		return;
	}
}

void WMgrHandleKeyPressEvent(VirtualScreen *vs, XEvent *event)
{
	WorkSpace *ws;
	int       len, i, lname;
	char      key [16];
	unsigned char k;
	char      name [128];
	KeySym    keysym;

	keysym  = XLookupKeysym((XKeyEvent *) event, 0);
	if(! keysym) {
		return;
	}
	if(keysym == XK_Control_L || keysym == XK_Control_R) {
		/* DontToggleWorkSpaceManagerState added 20040607 by dl*/
		if(!Scr->DontToggleWorkspaceManagerState) {
			WMapToggleState(vs);
		}
		return;
	}
	if(vs->wsw->state == WMS_map) {
		return;
	}


	/*
	 * If we're typing in a button-state WSM, and the mouse is on one of
	 * the buttons, that means we're changing the name, so do that dance.
	 */
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(vs->wsw->bswl [ws->number]->w == event->xkey.subwindow) {
			break;
		}
	}
	if(ws == NULL) {
		return;
	}

	strcpy(name, ws->label);
	lname = strlen(name);
	len   = XLookupString(&(event->xkey), key, 16, NULL, NULL);
	for(i = 0; i < len; i++) {
		k = key [i];
		if(isprint(k)) {
			name [lname++] = k;
		}
		else if((k == 127) || (k == 8)) {
			if(lname != 0) {
				lname--;
			}
		}
		else {
			break;
		}
	}
	name [lname] = '\0';
	ws->label = realloc(ws->label, (strlen(name) + 1));
	strcpy(ws->label, name);
	if(ws == vs->wsw->currentwspc) {
		PaintWsButton(WSPCWINDOW, vs, vs->wsw->bswl [ws->number]->w, ws->label, ws->cp,
		              on);
	}
	else {
		PaintWsButton(WSPCWINDOW, vs, vs->wsw->bswl [ws->number]->w, ws->label, ws->cp,
		              off);
	}
}


/*
 * Mouse clicking in WSM.  In the simple case, that's just switching
 * workspaces.  In the more complex, it's changing window occupation in
 * various different ways.
 */
void WMgrHandleButtonEvent(VirtualScreen *vs, XEvent *event)
{
	WorkSpaceWindow     *mw;
	WorkSpace           *ws, *oldws, *newws, *cws;
	WinList             wl;
	TwmWindow           *win;
	int                 occupation;
	unsigned int        W0, H0, bw;
	bool                cont;
	XEvent              ev;
	Window              w = 0, sw, parent;
	int                 X0, Y0, X1, Y1, XW, YW, XSW, YSW;
	Position            newX = 0, newY = 0, winX = 0, winY = 0;
	Window              junkW;
	unsigned int        junk;
	unsigned int        button;
	unsigned int        modifier;
	XSetWindowAttributes attrs;
	float               wf, hf;
	bool                alreadyvivible, realmovemode, startincurrent;
	Time                etime;

	parent   = event->xbutton.window;
	sw       = event->xbutton.subwindow;        /* mini-window in ws manager */
	mw       = vs->wsw;
	button   = event->xbutton.button;
	modifier = event->xbutton.state;
	etime    = event->xbutton.time;

	if(vs->wsw->state == WMS_buttons) {
		for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
			if(vs->wsw->bswl [ws->number]->w == parent) {
				break;
			}
		}
		if(ws == NULL) {
			return;
		}
		GotoWorkSpace(vs, ws);
		return;
	}

	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		if(vs->wsw->mswl [ws->number]->w == parent) {
			break;
		}
	}
	if(ws == NULL) {
		return;
	}
	if(sw == (Window) 0) {
		/*
		 * If clicked in the workspace but outside a window,
		 * only switch workspaces.
		 */
		GotoWorkSpace(vs, ws);
		return;
	}
	oldws = ws;

	if(XFindContext(dpy, sw, MapWListContext, (XPointer *) &wl) == XCNOENT) {
		return;
	}
	win = wl->twm_win;
	if((! Scr->TransientHasOccupation) && win->istransient) {
		return;
	}

	XTranslateCoordinates(dpy, Scr->Root, sw, event->xbutton.x_root,
	                      event->xbutton.y_root,
	                      &XW, &YW, &junkW);
	realmovemode = (Scr->ReallyMoveInWorkspaceManager && !(modifier & ShiftMask)) ||
	               (!Scr->ReallyMoveInWorkspaceManager && (modifier & ShiftMask));
	startincurrent = (oldws == vs->wsw->currentwspc);
	if(win->OpaqueMove) {
		int sw2, ss;

		sw2 = win->frame_width * win->frame_height;
		ss = vs->w * vs->h;
		if(sw2 > ((ss * Scr->OpaqueMoveThreshold) / 100)) {
			Scr->OpaqueMove = false;
		}
		else {
			Scr->OpaqueMove = true;
		}
	}
	else {
		Scr->OpaqueMove = false;
	}
	/*
	 * Buttons inside the workspace manager, when clicking on the
	 * representation of a window:
	 * 1: drag window to a different workspace
	 * 2: drag a copy of the window to a different workspace
	 *    (show it in both workspaces)
	 * 3: remove the window from this workspace (if it isn't the last).
	 */
	switch(button) {
		case 1 :
			XUnmapWindow(dpy, sw);

		case 2 :
			XGetGeometry(dpy, sw, &junkW, &X0, &Y0, &W0, &H0, &bw, &junk);
			XTranslateCoordinates(dpy, vs->wsw->mswl [oldws->number]->w,
			                      mw->w, X0, Y0, &X1, &Y1, &junkW);

			attrs.event_mask       = ExposureMask;
			attrs.background_pixel = wl->cp.back;
			attrs.border_pixel     = wl->cp.back;
			/* Create a draggable mini-window */
			w = XCreateWindow(dpy, mw->w, X1, Y1, W0, H0, bw,
			                  CopyFromParent,
			                  CopyFromParent,
			                  CopyFromParent,
			                  CWEventMask | CWBackPixel | CWBorderPixel, &attrs);

			XMapRaised(dpy, w);
			WMapRedrawWindow(w, W0, H0, wl->cp, wl->twm_win->icon_name);
			if(realmovemode && Scr->ShowWinWhenMovingInWmgr) {
				if(Scr->OpaqueMove) {
					DisplayWin(vs, win);
				}
				else {
					MoveOutline(Scr->Root,
					            win->frame_x - win->frame_bw,
					            win->frame_y - win->frame_bw,
					            win->frame_width  + 2 * win->frame_bw,
					            win->frame_height + 2 * win->frame_bw,
					            win->frame_bw,
					            win->title_height + win->frame_bw3D);
				}
			}
			break;

		case 3 :
			occupation = win->occupation & ~(1 << oldws->number);
			if(occupation != 0) {
				ChangeOccupation(win, occupation);
			}
			return;
		default :
			return;
	}

	/*
	 * Keep dragging the representation of the window
	 */
	wf = (float)(mw->wwidth  - 1) / (float) vs->w;
	hf = (float)(mw->wheight - 1) / (float) vs->h;
	XGrabPointer(dpy, mw->w, False,
	             ButtonPressMask | ButtonMotionMask | ButtonReleaseMask,
	             GrabModeAsync, GrabModeAsync, mw->w, Scr->MoveCursor, CurrentTime);

	alreadyvivible = false;
	cont = true;
	while(cont) {
		MapSubwindow *msw;
		XMaskEvent(dpy, ButtonPressMask | ButtonMotionMask |
		           ButtonReleaseMask | ExposureMask, &ev);
		switch(ev.xany.type) {
			case ButtonPress :
			case ButtonRelease :
				if(ev.xbutton.button != button) {
					break;
				}
				cont = false;
				newX = ev.xbutton.x;
				newY = ev.xbutton.y;

			case MotionNotify :
				if(cont) {
					newX = ev.xmotion.x;
					newY = ev.xmotion.y;
				}
				if(realmovemode) {
					for(cws = Scr->workSpaceMgr.workSpaceList;
					                cws != NULL;
					                cws = cws->next) {
						msw = vs->wsw->mswl [cws->number];
						if((newX >= msw->x) && (newX <  msw->x + mw->wwidth) &&
						                (newY >= msw->y) && (newY <  msw->y + mw->wheight)) {
							break;
						}
					}
					if(!cws) {
						break;
					}
					winX = newX - XW;
					winY = newY - YW;
					msw = vs->wsw->mswl [cws->number];
					XTranslateCoordinates(dpy, mw->w, msw->w,
					                      winX, winY, &XSW, &YSW, &junkW);
					winX = (int)(XSW / wf);
					winY = (int)(YSW / hf);
					if(Scr->DontMoveOff) {
						int width = win->frame_width;
						int height = win->frame_height;

						if((winX < Scr->BorderLeft) && ((Scr->MoveOffResistance < 0) ||
						                                (winX > Scr->BorderLeft - Scr->MoveOffResistance))) {
							winX = Scr->BorderLeft;
							newX = msw->x + XW + Scr->BorderLeft * mw->wwidth / vs->w;
						}
						if(((winX + width) > vs->w - Scr->BorderRight) &&
						                ((Scr->MoveOffResistance < 0) ||
						                 ((winX + width) < vs->w - Scr->BorderRight + Scr->MoveOffResistance))) {
							winX = vs->w - Scr->BorderRight - width;
							newX = msw->x + mw->wwidth *
							       (1 - Scr->BorderRight / (double) vs->w) - wl->width + XW - 2;
						}
						if((winY < Scr->BorderTop) && ((Scr->MoveOffResistance < 0) ||
						                               (winY > Scr->BorderTop - Scr->MoveOffResistance))) {
							winY = Scr->BorderTop;
							newY = msw->y + YW + Scr->BorderTop * mw->height / vs->h;
						}
						if(((winY + height) > vs->h - Scr->BorderBottom) &&
						                ((Scr->MoveOffResistance < 0) ||
						                 ((winY + height) < vs->h - Scr->BorderBottom + Scr->MoveOffResistance))) {
							winY = vs->h - Scr->BorderBottom - height;
							newY = msw->y + mw->wheight *
							       (1 - Scr->BorderBottom / (double) vs->h) - wl->height + YW - 2;
						}
					}
					WMapSetupWindow(win, winX, winY, -1, -1);
					if(Scr->ShowWinWhenMovingInWmgr) {
						goto movewin;
					}
					if(cws == vs->wsw->currentwspc) {
						if(alreadyvivible) {
							goto movewin;
						}
						if(Scr->OpaqueMove) {
							XMoveWindow(dpy, win->frame, winX, winY);
							DisplayWin(vs, win);
						}
						else {
							MoveOutline(Scr->Root,
							            winX - win->frame_bw, winY - win->frame_bw,
							            win->frame_width  + 2 * win->frame_bw,
							            win->frame_height + 2 * win->frame_bw,
							            win->frame_bw,
							            win->title_height + win->frame_bw3D);
						}
						alreadyvivible = true;
						goto move;
					}
					if(!alreadyvivible) {
						goto move;
					}
					if(!OCCUPY(win, vs->wsw->currentwspc) ||
					                (startincurrent && (button == 1))) {
						if(Scr->OpaqueMove) {
							Vanish(vs, win);
							XMoveWindow(dpy, win->frame, winX, winY);
						}
						else {
							MoveOutline(Scr->Root, 0, 0, 0, 0, 0, 0);
						}
						alreadyvivible = false;
						goto move;
					}
movewin:
					if(Scr->OpaqueMove) {
						XMoveWindow(dpy, win->frame, winX, winY);
					}
					else {
						MoveOutline(Scr->Root,
						            winX - win->frame_bw, winY - win->frame_bw,
						            win->frame_width  + 2 * win->frame_bw,
						            win->frame_height + 2 * win->frame_bw,
						            win->frame_bw,
						            win->title_height + win->frame_bw3D);
					}
				}
move:
				XMoveWindow(dpy, w, newX - XW, newY - YW);
				break;
			case Expose :
				if(ev.xexpose.window == w) {
					WMapRedrawWindow(w, W0, H0, wl->cp, wl->twm_win->icon_name);
					break;
				}
				Event = ev;
				DispatchEvent();
				break;
		}
	}
	/*
	 * Finished with dragging (button released).
	 */
	if(realmovemode) {
		if(Scr->ShowWinWhenMovingInWmgr || alreadyvivible) {
			if(Scr->OpaqueMove && !OCCUPY(win, vs->wsw->currentwspc)) {
				Vanish(vs, win);
			}
			if(!Scr->OpaqueMove) {
				MoveOutline(Scr->Root, 0, 0, 0, 0, 0, 0);
				WMapRedrawName(vs, wl);
			}
		}
		SetupWindow(win, winX, winY, win->frame_width, win->frame_height, -1);
	}
	ev.xbutton.subwindow = (Window) 0;
	ev.xbutton.window = parent;
	XPutBackEvent(dpy, &ev);
	XUngrabPointer(dpy, CurrentTime);

	if((ev.xbutton.time - etime) < 250) {
		/* Just a quick click or drag */
		KeyCode control_L_code, control_R_code;
		KeySym  control_L_sym,  control_R_sym;
		char keys [32];

		XMapWindow(dpy, sw);
		XDestroyWindow(dpy, w);
		GotoWorkSpace(vs, ws);
		if(!Scr->DontWarpCursorInWMap) {
			WarpToWindow(win, Scr->RaiseOnWarp);
		}
		control_L_sym  = XStringToKeysym("Control_L");
		control_R_sym  = XStringToKeysym("Control_R");
		control_L_code = XKeysymToKeycode(dpy, control_L_sym);
		control_R_code = XKeysymToKeycode(dpy, control_R_sym);
		XQueryKeymap(dpy, keys);
		if((keys [control_L_code / 8] & ((char) 0x80 >> (control_L_code % 8))) ||
		                keys [control_R_code / 8] & ((char) 0x80 >> (control_R_code % 8))) {
			WMapToggleState(vs);
		}
		return;
	}

	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		MapSubwindow *msw = vs->wsw->mswl [ws->number];
		if((newX >= msw->x) && (newX < msw->x + mw->wwidth) &&
		                (newY >= msw->y) && (newY < msw->y + mw->wheight)) {
			break;
		}
	}
	newws = ws;
	switch(button) {
		case 1 : /* moving to another workspace */
			if((newws == NULL) || (newws == oldws) ||
			                OCCUPY(wl->twm_win, newws)) {
				XMapWindow(dpy, sw);
				break;
			}
			occupation = (win->occupation | (1 << newws->number)) & ~(1 << oldws->number);
			ChangeOccupation(win, occupation);
			if(newws == vs->wsw->currentwspc) {
				OtpRaise(win, WinWin);
				WMapRaise(win);
			}
			else {
				WMapRestack(newws);
			}
			break;

		case 2 : /* putting in extra workspace */
			if((newws == NULL) || (newws == oldws) ||
			                OCCUPY(wl->twm_win, newws)) {
				break;
			}

			occupation = win->occupation | (1 << newws->number);
			ChangeOccupation(win, occupation);
			if(newws == vs->wsw->currentwspc) {
				OtpRaise(win, WinWin);
				WMapRaise(win);
			}
			else {
				WMapRestack(newws);
			}
			break;

		default :
			return;
	}
	XDestroyWindow(dpy, w);
}


/*
 * This is really more util.c fodder, but leaving it here for now because
 * it's only used once in the below func.  If we start finding external
 * uses for it, it should be moved.
 */
static void
InvertColorPair(ColorPair *cp)
{
	Pixel save;

	save = cp->fore;
	cp->fore = cp->back;
	cp->back = save;
	save = cp->shadc;
	cp->shadc = cp->shadd;
	cp->shadd = save;
}


/*
 * Draw a window name into the window's representation in the map-state
 * WSM.
 */
void WMapRedrawName(VirtualScreen *vs, WinList wl)
{
	int       w = wl->width;
	int       h = wl->height;
	ColorPair cp;
	char      *label;

	label  = wl->twm_win->icon_name;
	cp     = wl->cp;

	if(Scr->ReverseCurrentWorkspace && wl->wlist == vs->wsw->currentwspc) {
		InvertColorPair(&cp);
	}
	WMapRedrawWindow(wl->w, w, h, cp, label);
}


/*
 * Draw up a window's representation in the map-state WSM, with the
 * window name.
 *
 * The drawing of the window name could probably be done a bit better.
 * The font size is based on a tiny fraction of the window's height, so
 * is probably usually too small to be useful, and often appears just as
 * some odd colored pixels at the top of the window.
 */
static void WMapRedrawWindow(Window window, int width, int height,
                             ColorPair cp, char *label)
{
	int         x, y, strhei, strwid;
	MyFont      font;
	XRectangle inc_rect;
	XRectangle logical_rect;
	XFontStruct **xfonts;
	char **font_names;
	int i;
	int descent;
	int fnum;

	XClearWindow(dpy, window);
	font = Scr->workSpaceMgr.windowFont;

	XmbTextExtents(font.font_set, label, strlen(label),
	               &inc_rect, &logical_rect);
	strwid = logical_rect.width;
	strhei = logical_rect.height;
	if(strhei > height) {
		return;
	}

	x = (width  - strwid) / 2;
	if(x < 1) {
		x = 1;
	}

	fnum = XFontsOfFontSet(font.font_set, &xfonts, &font_names);
	for(i = 0, descent = 0; i < fnum; i++) {
		/* xf = xfonts[i]; */
		descent = ((descent < (xfonts[i]->max_bounds.descent)) ?
		           (xfonts[i]->max_bounds.descent) : descent);
	}

	y = ((height + strhei) / 2) - descent;

	if(Scr->use3Dwmap) {
		Draw3DBorder(window, 0, 0, width, height, 1, cp, off, true, false);
		FB(cp.fore, cp.back);
	}
	else {
		FB(cp.back, cp.fore);
		XFillRectangle(dpy, window, Scr->NormalGC, 0, 0, width, height);
		FB(cp.fore, cp.back);
	}
	if(Scr->Monochrome != COLOR) {
		XmbDrawImageString(dpy, window, font.font_set, Scr->NormalGC, x, y, label,
		                   strlen(label));
	}
	else {
		XmbDrawString(dpy, window, font.font_set, Scr->NormalGC, x, y, label,
		              strlen(label));
	}
}


/*
 * Create WSM representation of a given in a given WS.  Called when
 * windows get added to a workspace.
 */
void
WMapAddToList(TwmWindow *win, WorkSpace *ws)
{
	VirtualScreen *vs;
	WinList wl;
	float   wf, hf;
	ColorPair cp;
	XSetWindowAttributes attr;
	unsigned long attrmask;
	unsigned int bw;

	cp.back = win->title.back;
	cp.fore = win->title.fore;
	if(Scr->workSpaceMgr.windowcpgiven) {
		cp.back = Scr->workSpaceMgr.windowcp.back;
		GetColorFromList(Scr->workSpaceMgr.windowBackgroundL,
		                 win->full_name, &win->class, &cp.back);
		cp.fore = Scr->workSpaceMgr.windowcp.fore;
		GetColorFromList(Scr->workSpaceMgr.windowForegroundL,
		                 win->full_name, &win->class, &cp.fore);
	}
	if(Scr->use3Dwmap && !Scr->BeNiceToColormap) {
		GetShadeColors(&cp);
	}
	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		wf = (float)(vs->wsw->wwidth  - 2) / (float) vs->w;
		hf = (float)(vs->wsw->wheight - 2) / (float) vs->h;
		wl = malloc(sizeof(struct winList));
		wl->wlist  = ws;
		wl->x      = (int)(win->frame_x * wf);
		wl->y      = (int)(win->frame_y * hf);
		wl->width  = (unsigned int)((win->frame_width  * wf) + 0.5);
		wl->height = (unsigned int)((win->frame_height * hf) + 0.5);
		bw = 0;
		if(!Scr->use3Dwmap) {
			bw = 1;
			wl->width  -= 2;
			wl->height -= 2;
		}
		if(wl->width  < 1) {
			wl->width  = 1;
		}
		if(wl->height < 1) {
			wl->height = 1;
		}
		wl->w = XCreateSimpleWindow(dpy, vs->wsw->mswl [ws->number]->w, wl->x, wl->y,
		                            wl->width, wl->height, bw, Scr->Black, cp.back);
		attrmask = 0;
		if(Scr->BackingStore) {
			attr.backing_store = WhenMapped;
			attrmask |= CWBackingStore;
		}
		attr.cursor = handCursor;
		attrmask |= CWCursor;
		XChangeWindowAttributes(dpy, wl->w, attrmask, &attr);
		XSelectInput(dpy, wl->w, ExposureMask);
		XSaveContext(dpy, wl->w, TwmContext, (XPointer) vs->wsw->twm_win);
		XSaveContext(dpy, wl->w, ScreenContext, (XPointer) Scr);
		XSaveContext(dpy, wl->w, MapWListContext, (XPointer) wl);
		wl->twm_win = win;
		wl->cp      = cp;
		wl->next    = vs->wsw->mswl [ws->number]->wl;
		vs->wsw->mswl [ws->number]->wl = wl;
		if(win->mapped) {
			XMapWindow(dpy, wl->w);
		}
	}
}


/*
 * Remove window's WSM representation
 */
void
WMapRemoveFromList(TwmWindow *win, WorkSpace *ws)
{
	VirtualScreen *vs;
	WinList wl, *prev;

	for(vs = Scr->vScreenList; vs != NULL; vs = vs->next) {
		prev = &vs->wsw->mswl [ws->number]->wl;
		wl = *prev;
		while(wl != NULL) {
			if(win == wl->twm_win) {
				*prev = wl->next;
				XDeleteContext(dpy, wl->w, TwmContext);
				XDeleteContext(dpy, wl->w, ScreenContext);
				XDeleteContext(dpy, wl->w, MapWListContext);
				XDestroyWindow(dpy, wl->w);
				free(wl);
				break;
			}
			prev = &wl->next;
			wl   = *prev;
		}
	}
}


/*
 * Size and layout a WSM.  Mostly an internal bit in the process of
 * setting it up.
 */
static void ResizeWorkSpaceManager(VirtualScreen *vs, TwmWindow *win)
{
	int           bwidth, bheight;
	int           wwidth, wheight;
	int           hspace, vspace;
	int           lines, columns;
	int           neww, newh;
	WorkSpace     *ws;
	TwmWindow     *tmp_win;
	WinList       wl;
	int           i, j;
	float         wf, hf;

	neww = win->attr.width;
	newh = win->attr.height;
	if(neww == vs->wsw->width && newh == vs->wsw->height) {
		return;
	}

	hspace  = Scr->workSpaceMgr.hspace;
	vspace  = Scr->workSpaceMgr.vspace;
	lines   = Scr->workSpaceMgr.lines;
	columns = Scr->workSpaceMgr.columns;
	bwidth  = (neww - (columns * hspace)) / columns;
	bheight = (newh - (lines   * vspace)) / lines;
	wwidth  = neww / columns;
	wheight = newh / lines;
	wf = (float)(wwidth  - 2) / (float) vs->w;
	hf = (float)(wheight - 2) / (float) vs->h;

	i = 0;
	j = 0;
	for(ws = Scr->workSpaceMgr.workSpaceList; ws != NULL; ws = ws->next) {
		MapSubwindow *msw = vs->wsw->mswl [ws->number];
		XMoveResizeWindow(dpy, vs->wsw->bswl [ws->number]->w,
		                  i * (bwidth  + hspace) + (hspace / 2),
		                  j * (bheight + vspace) + (vspace / 2),
		                  bwidth, bheight);
		msw->x = i * wwidth;
		msw->y = j * wheight;
		XMoveResizeWindow(dpy, msw->w, msw->x, msw->y, wwidth - 2, wheight - 2);
		for(wl = msw->wl; wl != NULL; wl = wl->next) {
			tmp_win    = wl->twm_win;
			wl->x      = (int)(tmp_win->frame_x * wf);
			wl->y      = (int)(tmp_win->frame_y * hf);
			wl->width  = (unsigned int)((tmp_win->frame_width  * wf) + 0.5);
			wl->height = (unsigned int)((tmp_win->frame_height * hf) + 0.5);
			XMoveResizeWindow(dpy, wl->w, wl->x, wl->y, wl->width, wl->height);
		}
		i++;
		if(i == columns) {
			i = 0;
			j++;
		};
	}
	vs->wsw->bwidth    = bwidth;
	vs->wsw->bheight   = bheight;
	vs->wsw->width     = neww;
	vs->wsw->height    = newh;
	vs->wsw->wwidth     = wwidth;
	vs->wsw->wheight    = wheight;
	PaintWorkSpaceManager(vs);
}


/*
 * MapWindowCurrentWorkSpace {} parsing
 */
void WMapCreateCurrentBackGround(char *border,
                                 char *background, char *foreground,
                                 char *pixmap)
{
	Image *image;
	WorkSpaceMgr *ws = &Scr->workSpaceMgr;

	ws->curBorderColor = Scr->Black;
	ws->curColors.back = Scr->White;
	ws->curColors.fore = Scr->Black;
	ws->curImage       = NULL;

	if(border == NULL) {
		return;
	}
	GetColor(Scr->Monochrome, &ws->curBorderColor, border);
	if(background == NULL) {
		return;
	}
	ws->curPaint = true;
	GetColor(Scr->Monochrome, &ws->curColors.back, background);
	if(foreground == NULL) {
		return;
	}
	GetColor(Scr->Monochrome, &ws->curColors.fore, foreground);

	if(pixmap == NULL) {
		return;
	}
	if((image = GetImage(pixmap, Scr->workSpaceMgr.curColors)) == NULL) {
		fprintf(stderr, "Can't find pixmap %s\n", pixmap);
		return;
	}
	ws->curImage = image;
}


/*
 * MapWindowDefaultWorkSpace {} parsing
 */
void WMapCreateDefaultBackGround(char *border,
                                 char *background, char *foreground,
                                 char *pixmap)
{
	Image *image;
	WorkSpaceMgr *ws = &Scr->workSpaceMgr;

	ws->defBorderColor = Scr->Black;
	ws->defColors.back = Scr->White;
	ws->defColors.fore = Scr->Black;
	ws->defImage       = NULL;

	if(border == NULL) {
		return;
	}
	GetColor(Scr->Monochrome, &ws->defBorderColor, border);
	if(background == NULL) {
		return;
	}
	GetColor(Scr->Monochrome, &ws->defColors.back, background);
	if(foreground == NULL) {
		return;
	}
	GetColor(Scr->Monochrome, &ws->defColors.fore, foreground);
	if(pixmap == NULL) {
		return;
	}
	if((image = GetImage(pixmap, ws->defColors)) == NULL) {
		return;
	}
	ws->defImage = image;
}
