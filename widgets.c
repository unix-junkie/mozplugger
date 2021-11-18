/**
 * This file is part of mozplugger a fork of plugger, for list of developers
 * see the README file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sysexits.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <X11/X.h>
#include <X11/Xutil.h>

#include "debug.h"
#include "widgets.h"

#define MAX_CONTROLS_WIDTH 300
#define MAX_CONTROLS_HEIGHT 100

#define DEFAULT_CONTROLS_WIDTH 60
#define DEFAULT_CONTROLS_HEIGHT 20

#define MIN_CONTROLS_WIDTH 48
#define MIN_CONTROLS_HEIGHT 16

#define WINDOW_BORDER_WIDTH 1

#define BUTTON_DEPTH 2

/**
 * Given v when size is 16 scale to v if size was size,
 * make sure we round up or down correctly.
 *
 * @param[in] v The value to scale
 * @param[in] size
 *
 * @return The scaled value
 */
static int scale(int v, int size)
{
     return (v * size +8) / 16;
}

/**
 * Given origin point and an x and y translation, calculate new point
 *
 * @param[in] origin The origin
 * @param[in] x The X translation
 * @param[in] y The Y translation
 *
 * @return The co-ordinates as a XPoint structure
 */
static XPoint coordAdd(const XPoint * const origin, int x, int y)
{
     XPoint ret;
     ret.x = origin->x + x;
     ret.y = origin->y + y;
     return ret;
}

/**
 * Draw the play button
 *
 * ...0-1..........
 * ...|..\.........
 * ...|...\........
 * ...|....\.......
 * ...|.....\......
 * ...|......2.....
 * ...|......|\....
 * ...|......|.2...
 * ...|......3.|...
 * ...|...../..|...
 * ...|..../...3...
 * ...|.../.../....
 * ...|../.../.....
 * ...5-4.../......
 * ....\.../.......
 * .....5-4........
 *
 * @param[in] dpy The display
 * @param[in] win The window
 * @param[in] base The origin
 * @param[in] size The size of the button
 * @param[in] colour The colour of the button
 * @param[in] shadow The colour of the shadow
 * @param[in] bg The background colour
 * @param[in] pressed Is the button pressed
 */
void drawPlayButton(Display * dpy, Window win,
                                    const XPoint * base, int size,
                                        GC colour, GC shadow, GC bg, int pressed)

{
     XPoint points[6];

     const int len2 = scale(2, size);
     const int len5 = scale(5, size);
     const int len13 = scale(13, size);

     int s = !pressed ? BUTTON_DEPTH : 0;

     points[0] = coordAdd(base,        len5 - s,  len2 - s);
     points[1] = coordAdd(&points[0],  len2,      0);
     points[2] = coordAdd(&points[1],  len5,      len5);

     points[5] = coordAdd(base,        len5 - s,  len13 - s);
     points[4] = coordAdd(&points[5],  len2,      0);
     points[3] = coordAdd(&points[4],  len5,      -len5);


     if(!pressed)
     {
          int i;

          XFillPolygon(dpy, win, colour, points, 6, Convex, CoordModeOrigin);

          points[2].y++;
          points[5].x++;

          /* Draw the shadow */
          for(i = BUTTON_DEPTH; i > 0; i--)
          {
               XDrawLines(dpy, win, shadow, &points[2], 4, CoordModeOrigin);

               XDrawLine(dpy, win, shadow, points[3].x-1, points[3].y,
                                                 points[4].x-1, points[4].y);

               points[2].x++;
               points[2].y++;
               points[3].x++;
               points[3].y++;
               points[4].x++;
               points[4].y++;
               points[5].x++;
               points[5].y++;
          }
     }
     else
     {
          int x = points[0].x;
          int y = points[0].y;
          unsigned w = points[2].x - points[0].x;
          unsigned h = points[4].y - points[0].y;

          XFillPolygon(dpy, win, colour, points, 6, Convex, CoordModeOrigin);

          /* erase left bit of previous not pressed button */
          XFillRectangle(dpy, win, bg,
                  x - BUTTON_DEPTH, y - BUTTON_DEPTH, BUTTON_DEPTH, h + BUTTON_DEPTH);
          /* erase top bit of previous not pressed button */
          XFillRectangle(dpy, win, bg,
                                            x, y-BUTTON_DEPTH, w, BUTTON_DEPTH);
     }
}

/**
 * Draw a rectangle, if pressed is false, give the rectangle shadow to give
 * impress of a raised button
 *
 * @param[in] dpy The display
 * @param[in] win The window
 * @param[in] x X-coordinate
 * @param[in] y Y-coordinate
 * @param[in] w width
 * @param[in] h height
 * @param[in] d depth
 * @param[in] colour The colour of the button
 * @param[in] shadow The colour of the shadow
 * @param[in] bg The background colour
 * @param[in] pressed If the button is pressed
 */
static void draw3dRectangle(Display * dpy, Window win,
                  int x, int y, unsigned w, unsigned h,
                            GC colour, GC shadow, GC bg, int d, int pressed)
{
     if(pressed)
     {
          XFillRectangle(dpy, win, colour, x, y, w, h);
          /* erase left bit of previous not pressed button */
          XFillRectangle(dpy, win, bg, x-d, y-d, d, h+d);
          /* erase top bit of previous not pressed button */
          XFillRectangle(dpy, win, bg, x, y-d, w, d);
     }
     else
     {
          int i;

          XFillRectangle(dpy, win, colour, x-d, y-d, w, h);

          /* Draw shadow */
          for(i=d; i > 0; i--)
          {
               XDrawLine(dpy, win, shadow, x+w-i, y+1-i, x+w-i, y+h-i);
               XDrawLine(dpy, win, shadow, x+1-i, y+h-i, x+w-i, y+h-i);
          }
     }
}

/**
 * Draw a pause button
 *
 * .0---1.4---5...
 * .|...|.|...|\..
 * .|...|.|...|.5.
 * .|...|.|...|.|.
 * .|...|.|...|.|.
 * .|...|.|...|.|.
 * .|...|.|...|.|.
 * .|...|.|...|.|.
 * .|...|.|...|.|.
 * .|...|.|...|.|.
 * .|...|.|...|.|.
 * .|...|.|...|.|.
 * .3---2.7---6.|.
 * ..\...\.\...\|.
 * ...3---2.7---6.
 * ...............
 *
 * @param[in] dpy The display
 * @param[in] win The window
 * @param[in] base X-coordinate and Y-coordinate
 * @param[in] size The size of the button
 * @param[in] colour The colour of the button
 * @param[in] shadow The colour of the shadow
 * @param[in] bg The background colour
 * @param[in] pressed If the button is pressed
 */
void drawPauseButton(Display * dpy, Window win,
                                 const XPoint * base, int size,
                                       GC colour, GC shadow, GC bg, int pressed)
{
     const int len2 = scale(2, size);
     const int len3 = scale(3, size);
     const int len9 = scale(9, size);
     const int len11 = scale(11, size);

     const int y = base->y + len2;
     const unsigned w = (unsigned) len3 + 1;
     const unsigned h = (unsigned) len11 + 1;

     draw3dRectangle(dpy, win,
                     base->x + len3, y, w, h,
                     colour, shadow, bg, BUTTON_DEPTH, pressed);

     draw3dRectangle(dpy, win,
                     base->x + len9, y, w, h,
                     colour, shadow, bg, BUTTON_DEPTH, pressed);
}

/**
 * Draw a stop button
 *
 * @param[in] dpy The display
 * @param[in] win The window
 * @param[in] base X-coordinate and Y-coordinate
 * @param[in] size The size of the button
 * @param[in] colour The colour of the button
 * @param[in] shadow The colour of the shadow
 * @param[in] bg The background colour
 * @param[in] pressed If the button is pressed
 */
void drawStopButton(Display * dpy, Window win,
                                const XPoint * base, int size,
                                      GC colour, GC shadow, GC bg, int pressed)
{
     const int len3 = scale(3, size);
     const int len9 = scale(9, size);

     unsigned w = (unsigned) len9;

     draw3dRectangle(dpy, win,
                     base->x + len3 + 1, base->y + len3 + 1, w, w,
                     colour, shadow, bg, BUTTON_DEPTH, pressed);
}

/**
 * Draw progress bar
 */
void drawProgressBar(Display * dpy, Window win,
                                const XPoint * base, int size,
                                      GC colour, GC shadow, GC bg, int progress)
{
     unsigned width = scale(1, size) + 1;

     static const int coords[4][2] =
     {
          {0,   0},
          {15,  0},
          {15, 15},
          {0,  15}
     };

     int corner = progress % 4;
     int x, y;
     int i;

     if(progress >= 0)
     {
          x = scale(coords[corner][0], size);
          y = scale(coords[corner][1], size);

          XFillRectangle(dpy, win, colour, x, y, width, width);
     }

     for(i = 0; i < 4; i++)
     {
          if(i == corner)
          {
                continue;
          }
          x = scale(coords[i][0], size);
          y = scale(coords[i][1], size);

          XFillRectangle(dpy, win, bg, x, y, width, width);
     }
}


/**
 * Set the Window Class Hint and Name
 */
void setWindowClassHint(Display * dpy, Window window, char * name)
{
     XClassHint classhint;

     classhint.res_name = name;
     classhint.res_class = name;

     XSetClassHint(dpy, window, &classhint);
     XStoreName(dpy, window, name);
}

/**
 * Set the Window Hints
 */
void setWindowHints(Display * dpy, Window window, int numButtons)
{
     XSizeHints wmHints;

     wmHints.x = 0;
     wmHints.y = 0;
     wmHints.min_width = numButtons * MIN_BUTTON_SIZE;
     wmHints.min_height = MIN_BUTTON_SIZE;
     wmHints.base_width = numButtons * DEFAULT_BUTTON_SIZE;
     wmHints.base_height = DEFAULT_BUTTON_SIZE;
     wmHints.flags = PPosition | USPosition | PMinSize;

     XSetWMNormalHints(dpy, window, &wmHints);
}

