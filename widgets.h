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

#ifndef _MOZPLUGGER_WIDGETS_H_
#define _MOZPLUGGER_WIDGETS_H_

#define MAX_BUTTON_SIZE (100)
#define DEFAULT_BUTTON_SIZE (20)
#define MIN_BUTTON_SIZE (16)

extern void drawPlayButton(Display * dpy, Window win,
                                      const XPoint * base, int size,
                                      GC colour, GC shadow, GC bg, int pressed);

extern void drawPauseButton(Display * dpy, Window win,
                                      const XPoint * base, int size,
                                      GC colour, GC shadow, GC bg, int pressed);

extern void drawStopButton(Display * dpy, Window win,
                                      const XPoint * base, int size,
                                      GC colour, GC shadow, GC bg, int pressed);

extern void drawProgressBar(Display * dpy, Window win,
                                      const XPoint * base, int size,
                                      GC colour, GC shadow, GC bg, int progress);

extern void setWindowClassHint(Display * dpy, Window window, char * name);

extern void setWindowHints(Display * dpy, Window window, int numButtons);

#endif
