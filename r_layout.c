/*
 * Copyright notice...
 */

#include "ctwm.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "r_layout.h"
#include "r_area_list.h"
#include "r_area.h"
#include "util.h"


/**
 * Create an RLayout for a given set of monitors.
 *
 * This stashes up the list of monitors, and precalculates the
 * horizontal/vertical stripes that compose it.
 */
RLayout *
RLayoutNew(RAreaList *monitors)
{
	RLayout *layout = malloc(sizeof(RLayout));
	if(layout == NULL) {
		abort();
	}

	layout->monitors = monitors;
	layout->horiz = RAreaListHorizontalUnion(monitors);
	layout->vert = RAreaListVerticalUnion(monitors);
	layout->names = NULL;

	return layout;
}


/**
 * Create a copy of an RLayout with given amounts cropped off the sides.
 * This is used anywhere we need to pretend our display area is smaller
 * than it actually is (e.g., via the BorderBottom/Top/Left/Right config
 * params)
 */
RLayout *
RLayoutCopyCropped(RLayout *self, int left_margin, int right_margin,
                   int top_margin, int bottom_margin)
{
	RAreaList *cropped_monitors = RAreaListCopyCropped(self->monitors,
	                              left_margin, right_margin,
	                              top_margin, bottom_margin);
	if(cropped_monitors == NULL) {
		return NULL;        // nothing to crop, same layout as passed
	}

	return RLayoutNew(cropped_monitors);
}


/**
 * Clean up and free any RLayout.names there might be in an RLayout.
 */
static void
_RLayoutFreeNames(RLayout *self)
{
	if(self->names != NULL) {
		free(self->names);
		self->names = NULL;
	}
}


/**
 * Clean up and free an RLayout.
 */
void
RLayoutFree(RLayout *self)
{
	RAreaListFree(self->monitors);
	RAreaListFree(self->horiz);
	RAreaListFree(self->vert);
	_RLayoutFreeNames(self);
	free(self);
}


/**
 * Set the names for our monitors in an RLayout.  This is only used for
 * the RLayout that describes our complete monitor layout, which fills in
 * the RANDR names for each output.
 */
RLayout *
RLayoutSetMonitorsNames(RLayout *self, char **names)
{
	_RLayoutFreeNames(self);
	self->names = names;
	return self;
}


/**
 * Given an RArea that doesn't reside in any of the areas in our RLayout,
 * create an maximally-tall RArea that is and that the window could go
 * into, and figure where that would fit in our RLayout.
 *
 * This is the vertical-stripe-returning counterpart of
 * _RLayoutRecenterHorizontally().
 *
 * This will create an RArea that's always the width of far_area, moved
 * horizontally as little as possible to ensure that the left or right
 * edge is on-screen, and the full height of our RLayout.  That area is
 * then RAreaListIntersect()'ed with self, yielding the set of vertical
 * stripes in self that new position will be in.
 *
 * This is called only by _RLayoutVerticalIntersect() when given an RArea
 * that doesn't already intersect the RLayout.  Will probably not tell
 * you something useful if given a far_area that already _does_ intersect
 * self.
 *
 * \param self     Our current monitor layout
 * \param far_area The area to act on
 */
static RAreaList *
_RLayoutRecenterVertically(RLayout *self, RArea *far_area)
{
	//  |_V_|
	//  |   |
	// L|   |R
	//  |___|
	//  | V |
	RArea big = RAreaListBigArea(self->monitors), tmp;

	// Where did it wind up?
	if((far_area->x >= big.x && far_area->x <= RAreaX2(&big))
	                || (RAreaX2(far_area) >= big.x && RAreaX2(far_area) <= RAreaX2(&big))) {
		// In one of the V areas.  It's already in a horizontal position
		// that would fit, so keep that.  Come up with a new area of that
		// horizontal pos/width, which vertically covers the full height
		// of self.
		tmp = RAreaNew(far_area->x, big.y,
		               far_area->width, big.height);
	}
	else if(RAreaX2(far_area) < big.x) {
		// Off to the left side, so make an area that moves it over far
		// enough to just eek into self.
		tmp = RAreaNew(big.x - far_area->width + 1, big.y,
		               far_area->width, big.height);
	}
	else {
		// Off to the right side, so move it over just enough for the
		// left edge to be on-screen.
		tmp = RAreaNew(RAreaX2(&big), big.y,
		               far_area->width, big.height);
	}

	// Intersect that new vertical stripe with our screen
	return RAreaListIntersect(self->vert, &tmp);
}


/**
 * Given an RArea that doesn't reside in any of the areas in our RLayout,
 * create an maximally-wide RArea that is and that the window could go
 * into, and figure where that would fit in our RLayout.
 *
 * This is the horizontal-stripe-returning counterpart of
 * _RLayoutRecenterVertically().
 *
 * This will create an RArea that's always the height of far_area, moved
 * veritcally as little as possible to ensure that the top or bottom edge
 * is on-screen, and the full width of our RLayout.  That area is then
 * RAreaListIntersect()'ed with self, yielding the set of horizontal
 * stripes in self that new position will be in.
 *
 * This is called only by _RLayoutHorizontalIntersect() and
 * RLayoutFull1() when given an RArea that doesn't already intersect the
 * RLayout.  Will probably not tell you something useful if given a
 * far_area that already _does_ intersect self.
 *
 * \param self     Our current monitor layout
 * \param far_area The area to act on
 */
static RAreaList *
_RLayoutRecenterHorizontally(RLayout *self, RArea *far_area)
{
	// ___T___
	//  |   |
	// H|   |H
	// _|___|_
	//    B
	RArea big = RAreaListBigArea(self->monitors), tmp;

	// Where is it?
	if((far_area->y >= big.y && far_area->y <= RAreaY2(&big))
	                || (RAreaY2(far_area) >= big.y && RAreaY2(far_area) <= RAreaY2(&big))) {
		// In one of the H areas.  Already in a valid place vertically,
		// so create a horizontal stripe there.
		tmp = RAreaNew(big.x, far_area->y,
		               big.width, far_area->height);
	}
	else if(RAreaY2(far_area) < big.y) {
		// Off the top (T); make a horizontal stripe low enough that the
		// bottom of far_area winds up on-screen.
		tmp = RAreaNew(big.x, big.y - far_area->height + 1,
		               big.width, far_area->height);
	}
	else {
		// Off the bottom (B); make the stripe high enough for the top of
		// far_area to peek on-screen.
		tmp = RAreaNew(big.x, RAreaY2(&big),
		               big.width, far_area->height);
	}

	// And find which horizontal stripes of the screen that falls into.
	return RAreaListIntersect(self->horiz, &tmp);
}


/**
 * Find which vertical regions of our monitor layout a given RArea (often
 * a window) is in.  If it's not in any, shift it horizontally until it'd
 * be inside in that dimension, and return the vertical stripes that
 * horizontal extent would be in.
 *
 * Note that for our purposes, it doesn't matter whether the RArea is
 * _vertically_ within the RLayout.  This function is used only by
 * RLayoutFindTopBottomEdges()
 */
static RAreaList *
_RLayoutVerticalIntersect(RLayout *self, RArea *area)
{
	RAreaList *mit = RAreaListIntersect(self->vert, area);

	if(mit->len == 0) {
		// Not in any of the areas; find the nearest horizontal shift to
		// put in in one.
		RAreaListFree(mit);
		mit = _RLayoutRecenterVertically(self, area);
	}
	return mit;
}


/**
 * Find which horizontal regions of our monitor layout a given RArea
 * (often a window) is in.  If it's not in any, shift it vertically until
 * it'd be inside in that dimension, and return the horizontal stripes
 * that vertical extent would be in.
 *
 * Note that for our purposes, it doesn't matter whether the RArea is
 * _horizontally_ within the RLayout.  This function is used only by
 * RLayoutFindLeftRightEdges()
 */
static RAreaList *
_RLayoutHorizontalIntersect(RLayout *self, RArea *area)
{
	RAreaList *mit = RAreaListIntersect(self->horiz, area);

	if(mit->len == 0) {
		RAreaListFree(mit);

		// Out of screen, recenter the window
		mit = _RLayoutRecenterHorizontally(self, area);
	}

	return mit;
}


/**
 * Figure the position (or nearest practical position) of an area in our
 * screen layout, and return into about the bottom/top stripes it fits
 * into.
 *
 * Note that the return values (params) are slightly counterintuitive;
 * top tells you where the top of the lowest stripe that area intersects
 * with is, and bottom tells you the bottom of the highest.  This is used
 * as a backend piece of various calculations trying to be sure something
 * winds up on-screen.
 *
 * \param[in]  self   The monitor layout to work from
 * \param[in]  area   The area to be fit into the monitors
 * \param[out] top    The top of the lowest stripe area fits into.
 * \param[out] bottom The bottom of the highest stripe area fits into.
 */
void
RLayoutFindTopBottomEdges(RLayout *self, RArea *area, int *top,
                          int *bottom)
{
	RAreaList *mit = _RLayoutVerticalIntersect(self, area);

	if(top != NULL) {
		*top = RAreaListMaxY(mit);
	}

	if(bottom != NULL) {
		*bottom = RAreaListMinY2(mit);
	}

	RAreaListFree(mit);
}


/**
 * Find the bottom of the top stripe of self that area fits into.  A
 * shortcut to get only the second return value of
 * RLayoutFindTopBottomEdges().
 */
int
RLayoutFindBottomEdge(RLayout *self, RArea *area)
{
	int min_y2;
	RLayoutFindTopBottomEdges(self, area, NULL, &min_y2);
	return min_y2;
}


/**
 * Find the top of the bottom stripe of self that area fits into.  A
 * shortcut to get only the first return value of
 * RLayoutFindTopBottomEdges().
 */
int
RLayoutFindTopEdge(RLayout *self, RArea *area)
{
	int max_y;
	RLayoutFindTopBottomEdges(self, area, &max_y, NULL);
	return max_y;
}


/**
 * Figure the position (or nearest practical position) of an area in our
 * screen layout, and return into about the left/rightmost stripes it fits
 * into.
 *
 * As with RLayoutFindTopBottomEdges(), the return values (params) are
 * slightly counterintuitive.  left tells you where the left-side of the
 * right-most stripe that area intersects with is, and right tells you
 * the right side of the left-most.
 *
 * This is used as a backend piece of
 * various calculations trying to be sure something winds up on-screen.
 *
 * \param[in]  self   The monitor layout to work from
 * \param[in]  area   The area to be fit into the monitors
 * \param[out] left   The left edge of the right-most stripe area fits into.
 * \param[out] right  The right edge of the left-most stripe area fits into.
 */
void
RLayoutFindLeftRightEdges(RLayout *self, RArea *area, int *left,
                          int *right)
{
	RAreaList *mit = _RLayoutHorizontalIntersect(self, area);

	if(left != NULL) {
		*left = RAreaListMaxX(mit);
	}

	if(right != NULL) {
		*right = RAreaListMinX2(mit);
	}

	RAreaListFree(mit);
}


/**
 * Find the left edge of the right-most stripe of self that area fits
 * into.  A shortcut to get only the first return value of
 * RLayoutFindLeftRightEdges().
 */
int
RLayoutFindLeftEdge(RLayout *self, RArea *area)
{
	int max_x;
	RLayoutFindLeftRightEdges(self, area, &max_x, NULL);
	return max_x;
}


/**
 * Find the right edge of the left-most stripe of self that area fits
 * into.  A shortcut to get only the second return value of
 * RLayoutFindLeftRightEdges().
 */
int
RLayoutFindRightEdge(RLayout *self, RArea *area)
{
	int min_x2;
	RLayoutFindLeftRightEdges(self, area, NULL, &min_x2);
	return min_x2;
}



/*
 * Lookups to find areas in an RLayout by various means.
 */

/// Internal structure for callback in RLayoutGetAreaAtXY().
struct monitor_finder_xy {
	RArea *area;
	int x, y;
};

/**
 * Callback util for RLayoutGetAreaAtXY().
 */
static int
_findMonitorByXY(RArea *cur, void *vdata)
{
	struct monitor_finder_xy *data = (struct monitor_finder_xy *)vdata;

	if(RAreaContainsXY(cur, data->x, data->y)) {
		data->area = cur;
		return 1;
	}
	return 0;
}

/**
 * Find the RArea in a RLayout that a given coordinate falls into.  In
 * practice, the RArea's in self are the monitors of the desktop, so this
 * answers "Which monitor is this position on?"
 */
RArea
RLayoutGetAreaAtXY(RLayout *self, int x, int y)
{
	struct monitor_finder_xy data = { NULL, x, y };

	RAreaListForeach(self->monitors, _findMonitorByXY, &data);

	return data.area == NULL ? self->monitors->areas[0] : *data.area;
}


/**
 * Return the index'th RArea in an RLayout, or the last one if index
 * overflows.
 *
 * \todo XXX This is questionable.  This function is called in only one
 * place, and that place calls it with index 0, so (a) it could only fail
 * to find index if there weren't any RArea's in the RLayout, and (2) if
 * there weren't any, it would return areas[-1] which is scary garbage...
 */
RArea RLayoutGetAreaIndex(RLayout *self, int index)
{
	if(index >= self->monitors->len) {
		index = self->monitors->len - 1;
	}

	return self->monitors->areas[index];
}


/**
 * Return the RArea in self with the name given by the string of length
 * len at name.  This is only used in RLayoutXParseGeometry() to parse a
 * fragment of a larger string, hence the need for len.  It's used to
 * find the monitor with a given name (RANDR output name).
 */
RArea
RLayoutGetAreaByName(RLayout *self, const char *name, int len)
{
	if(self->names != NULL) {
		int index;

		if(len < 0) {
			len = strlen(name);
		}

		for(index = 0; index < self->monitors->len
		                && self->names[index] != NULL; index++) {
			if(strncmp(self->names[index], name, len) == 0) {
				return self->monitors->areas[index];
			}
		}
	}

	return RAreaInvalid();
}


/**
 * Generate maximal spanning RArea.
 *
 * This is a trivial wrapper of RAreaListBigArea() to hide knowledge of
 * RLayout internals.  Currently only used once; maybe should just be
 * deref'd there...
 */
RArea
RLayoutBigArea(RLayout *self)
{
	return RAreaListBigArea(self->monitors);
}



/*
 * Now some utils for finding various edges
 */

/// Internal struct for use in FindMonitor*Edge() callbacks.
struct monitor_edge_finder {
	RArea *area;
	union {
		int max_x;
		int max_y;
		int min_x2;
		int min_y2;
	} u;
	int found;
};


/**
 * Callback util for RLayoutFindMonitorBottomEdge()
 */
static int
_findMonitorBottomEdge(RArea *cur, void *vdata)
{
	struct monitor_edge_finder *data = (struct monitor_edge_finder *)vdata;

	// Does the area we're looking for intersect this piece of the
	// RLayout, is the bottom of the area shown on it, and is the bottom
	// of this piece the highest we've yet found that satisfies those
	// conditions?
	if(RAreaIsIntersect(cur, data->area)
	                && RAreaY2(cur) > RAreaY2(data->area)
	                && (!data->found || RAreaY2(cur) < data->u.min_y2)) {
		data->u.min_y2 = RAreaY2(cur);
		data->found = 1;
	}
	return 0;
}

/**
 * Find the bottom edge of the top-most monitor that contains the most of
 * a given RArea.  Generally, the area would be a window.
 *
 * That is, we find the monitor whose bottom is the highest up, but that
 * still shows the bottom edge of the window, and return that monitor's
 * bottom.  If the bottom of the window is off all the monitors, that's
 * just the highest-up monitor that contains the window.
 */
int
RLayoutFindMonitorBottomEdge(RLayout *self, RArea *area)
{
	struct monitor_edge_finder data = { area };

	RAreaListForeach(self->monitors, _findMonitorBottomEdge, &data);

	return data.found ? data.u.min_y2 : RLayoutFindBottomEdge(self, area);
}


/**
 * Callback util for RLayoutFindMonitorTopEdge()
 */
static int
_findMonitorTopEdge(RArea *cur, void *vdata)
{
	struct monitor_edge_finder *data = (struct monitor_edge_finder *)vdata;

	// Does the area we're looking for intersect this piece of the
	// RLayout, is the top of the area shown on it, and is the top
	// of this piece the lowest we've yet found that satisfies those
	// conditions?
	if(RAreaIsIntersect(cur, data->area)
	                && cur->y < data->area->y
	                && (!data->found || cur->y > data->u.max_y)) {
		data->u.max_y = cur->y;
		data->found = 1;
	}
	return 0;
}

/**
 * Find the top edge of the bottom-most monitor that contains the most of
 * a given RArea.  Generally, the area would be a window.
 *
 * That is, we find the monitor whose top is the lowest down, but that
 * still shows the top edge of the window, and return that monitor's top.
 * If the top of the window is off all the monitors, that's just the
 * lowest-down monitor that contains part of the window.
 */
int
RLayoutFindMonitorTopEdge(RLayout *self, RArea *area)
{
	struct monitor_edge_finder data = { area };

	RAreaListForeach(self->monitors, _findMonitorTopEdge, &data);

	return data.found ? data.u.max_y : RLayoutFindTopEdge(self, area);
}


/**
 * Callback util for RLayoutFindMonitorLeftEdge()
 */
static int
_findMonitorLeftEdge(RArea *cur, void *vdata)
{
	struct monitor_edge_finder *data = (struct monitor_edge_finder *)vdata;

	// Does the area we're looking for intersect this piece of the
	// RLayout, is the left of the area shown on it, and is the left of
	// this piece the right-most we've yet found that satisfies those
	// conditions?
	if(RAreaIsIntersect(cur, data->area)
	                && cur->x < data->area->x
	                && (!data->found || cur->x > data->u.max_x)) {
		data->u.max_x = cur->x;
		data->found = 1;
	}
	return 0;
}

/**
 * Find the left edge of the right-most monitor that contains the most of
 * a given RArea.  Generally, the area would be a window.
 *
 * That is, we find the monitor whose left is the furthest right, but
 * that still shows the left edge of the window, and return that
 * monitor's left.  If the left edge of the window is off all the
 * monitors, that's just the right-most monitor that contains the window.
 */
int
RLayoutFindMonitorLeftEdge(RLayout *self, RArea *area)
{
	struct monitor_edge_finder data = { area };

	RAreaListForeach(self->monitors, _findMonitorLeftEdge, &data);

	return data.found ? data.u.max_x : RLayoutFindLeftEdge(self, area);
}


/**
 * Callback util for RLayoutFindMonitorRightEdge()
 */
static int
_findMonitorRightEdge(RArea *cur, void *vdata)
{
	struct monitor_edge_finder *data = (struct monitor_edge_finder *)vdata;

	// Does the area we're looking for intersect this piece of the
	// RLayout, is the right of the area shown on it, and is the right of
	// this piece the left-most we've yet found that satisfies those
	// conditions?
	if(RAreaIsIntersect(cur, data->area)
	                && RAreaX2(cur) > RAreaX2(data->area)
	                && (!data->found || RAreaX2(cur) < data->u.min_x2)) {
		data->u.min_x2 = RAreaX2(cur);
		data->found = 1;
	}
	return 0;
}

/**
 * Find the right edge of the left-most monitor that contains the most of
 * a given RArea.  Generally, the area would be a window.
 *
 * That is, we find the monitor whose right is the furthest left, but
 * that still shows the right edge of the window, and return that
 * monitor's right.  If the right edge of the window is off all the
 * monitors, that's just the left-most monitor that contains the window.
 */
int
RLayoutFindMonitorRightEdge(RLayout *self, RArea *area)
{
	struct monitor_edge_finder data = { area };

	RAreaListForeach(self->monitors, _findMonitorRightEdge, &data);

	return data.found ? data.u.min_x2 : RLayoutFindRightEdge(self, area);
}



/**
 * Figure the best way to stretch an area across the full horizontal
 * width of an RLayout.  This is the backend for the f.xhorizoom ctwm
 * function, zooming a window to the full width of all monitors.
 */
RArea
RLayoutFullHoriz(RLayout *self, RArea *area)
{
	int max_x, min_x2;

	RLayoutFindLeftRightEdges(self, area, &max_x, &min_x2);

	return RAreaNew(max_x, area->y, min_x2 - max_x + 1, area->height);

	/**
	 * This yields an area:
	 * ~~~
	 * TL   W
	 *   *-----*
	 *   |     |
	 *  H|     |
	 *   |     |
	 *   *-----*
	 * ~~~
	 *
	 * The precise construction of the area can be tricky.
	 *
	 * In the simplest case, the area is entirely in one horizontal
	 * stripe to start with.  In that case, max_x is the left side of
	 * that box, min_x2 is the right side, so the resulting area starts
	 * at (left margin, area y), with the height of y and the width of
	 * the whole stripe.  Easy.
	 *
	 * When it spans multiple, it's more convoluted.  Let's consider an
	 * example layout to make it a little clearer:
	 *
	 * ~~~
	 * *--------------------------*
	 * |             |......2.....|
	 * |                          |  <-----.
	 * |             1 =========  |         .
	 * *-------------*-=========--+-*        >-- 2 horiz stripes
	 *               | =========    |       '
	 *               |  /           |  <---'
	 *       area  --+-'            |
	 *               *--------------*
	 * ~~~
	 *
	 * So in this case, we're trying to stretch area out as far
	 * horizontal as it can go, crossing monitors.
	 *
	 * So, the top-left corner of our box (TL) has the X coordinate of
	 * the right-most strip we started with (the lower, and the Y
	 * coordinate of the top of the area, yielding point (1) above.
	 *
	 * The width W is the difference between the right of the left-most
	 * (in this case, the top) stripe, and the left of the right-most
	 * (the bottom) (plus 1 because math).  That's the width of the
	 * intersecting horizontal area (2) above.
	 *
	 * And the height H is just the height of the original area.  And so,
	 * our resulting area is the height of that original area (in ='s),
	 * and stretched to the left and right until it runs into one or the
	 * other monitor edge (1 space to the left, 2 to the right, in our
	 * diagram).
	 */
}


/**
 * Figure the best way to stretch an area across the full vertical height
 * of an RLayout.  This is the backend for the f.xzoom ctwm function,
 * zooming a window to the full height of all monitors.
 */
RArea
RLayoutFullVert(RLayout *self, RArea *area)
{
	int max_y, min_y2;

	RLayoutFindTopBottomEdges(self, area, &max_y, &min_y2);

	return RAreaNew(area->x, max_y, area->width, min_y2 - max_y + 1);

	// X-ref long comment above in RLayoutFullHoriz() for worked example.
	// This is just rotated 90 degrees, but the logic works out about the
	// same.
}


/**
 * Figure the best way to stretch an area across the largest horizontal
 * and vertical space it can from its current position.  Essentially,
 * stretch it in all directions until it hits the edge of our available
 * space.
 *
 * This is the backend for the f.xfullzoom function.
 */
RArea
RLayoutFull(RLayout *self, RArea *area)
{
	RArea full_horiz, full_vert, full1, full2;

	// Get the boxes for full horizontal and vertical zooms, using the
	// above functions.
	full_horiz = RLayoutFullHoriz(self, area);
	full_vert = RLayoutFullVert(self, area);

	// Now stretch each of those in the other direction...
	full1 = RLayoutFullVert(self, &full_horiz);
	full2 = RLayoutFullHoriz(self, &full_vert);

	// And return whichever was bigger.
	return RAreaArea(&full1) > RAreaArea(&full2) ? full1 : full2;
}



/**
 * Figure the best way to stretch an area horizontally without crossing
 * monitors.
 *
 * This is the backend for the f.horizoom ctwm function.
 */
RArea
RLayoutFullHoriz1(RLayout *self, RArea *area)
{
	// Cheat by using RLayoutFull1() to find the RArea for the monitor
	// it's most on.
	RArea target = RLayoutFull1(self, area);
	int max_y, min_y2;

	// We're stretching horizontally, so the x and width of target (that
	// monitor) are already right.  But we have to figure the y and
	// height...

	// Generally, the y is the window's original y, unless we had to move
	// it down to get onto the target monitor.  XXX Wait, what if we
	// moved it _up_?
	max_y = max(area->y, target.y);
	target.y = max_y;

	// The bottom would be the bottom of the area, clipped to the bottom
	// of the monitor.  So the height is the diff.
	min_y2 = min(RAreaY2(area), RAreaY2(&target));
	target.height = min_y2 - max_y + 1;

	return target;
}


/**
 * Figure the best way to stretch an area vertically without crossing
 * monitors.
 *
 * This is the backend for the f.zoom ctwm function.
 */
RArea
RLayoutFullVert1(RLayout *self, RArea *area)
{
	// Let RLayoutFull1() find the right monitor.
	RArea target = RLayoutFull1(self, area);
	int max_x, min_x2;

	// Stretching vertically, so the y/height of the monitor are already
	// right.

	// x is where the window was, unless we had to move it right to get
	// onto the monitor.  XXX What if we moved it left?
	max_x = max(area->x, target.x);
	target.x = max_x;

	// Right side is where it was, unless we have to clip to the monitor.
	min_x2 = min(RAreaX2(area), RAreaX2(&target));
	target.width = min_x2 - max_x + 1;

	return target;
}


/**
 * Figure the best way to resize an area to fill one monitor.
 *
 * This is the backend for the f.fullzoom ctwm function.
 *
 * \param self  Monitor layout
 * \param area  Area (window) to zoom out
 */
RArea
RLayoutFull1(RLayout *self, RArea *area)
{
	RArea target;
	RAreaList *mit = RAreaListIntersect(self->monitors, area);
	// Start with a list of all the monitors the window is on now.

	if(mit->len == 0) {
		// Not on any screens.  Find the "nearest" place it would wind
		// up.
		RAreaListFree(mit);
		mit = _RLayoutRecenterHorizontally(self, area);
	}

	// Of the monitors it's on, find the one that it's "most" on, and
	// return the RArea of it.
	target = RAreaListBestTarget(mit, area);
	RAreaListFree(mit);
	return target;
}



/**
 * Pretty-print an RLayout.
 *
 * Used for dev/debug.
 */
void
RLayoutPrint(RLayout *self)
{
	fprintf(stderr, "[monitors=");
	RAreaListPrint(self->monitors);
	fprintf(stderr, "\n horiz=");
	RAreaListPrint(self->horiz);
	fprintf(stderr, "\n vert=");
	RAreaListPrint(self->vert);
	fprintf(stderr, "]\n");
}
