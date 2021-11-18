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
#include <stdint.h>
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

#include "cmd_flags.h"
#include "child.h"
#include "debug.h"
#include "pipe_msg.h"
#include "widgets.h"

#define WINDOW_BORDER_WIDTH 1

struct AppData_s
{
     Window window;
     unsigned int width;
     unsigned int height;
     const char * nextHelper;
     unsigned long flags;
     int pipe_fd;
     int repeats;
     char * command;
};

typedef struct AppData_s AppData_t;

struct SigGlobals_s
{
    Display * dpy;
    pid_t childPid;
};

typedef struct SigGlobals_s SigGlobals_t;


static SigGlobals_t sig_globals;
static GC gc_white;
static GC gc_black;
static GC gc_onColor;
static GC gc_offColor;


/**
 * Redraw the button
 *
 * @param[in] dpy The display handle
 * @param[in] win The window handle
 * @param[in] buttonDown The button state
 */
static void redrawButton(Display * dpy, Window win, int buttonDown)
{
     static int old_bsize = -1;
     static int bsize = 10;

     XWindowAttributes attr;
     XPoint base;
     GC c = (sig_globals.childPid > 0) ? gc_offColor : gc_onColor;

     XGetWindowAttributes(dpy, win, &attr);
     bsize = attr.width;
     if(attr.height < bsize)
     {
          bsize=attr.height;
     }

     if(old_bsize != bsize)
     {
          old_bsize = bsize;
          XFillRectangle(dpy, win, gc_white,
                         0,0,(unsigned)attr.width, (unsigned)attr.height);
     }


     base.x = (attr.width - bsize)/2;
     base.y = (attr.height - bsize)/2;

     drawPlayButton(dpy, win, &base, bsize,
                                        c, gc_black, gc_white, buttonDown == 0);
}

/**
 * Terminate, called from signal handler or when SHUTDOWN_MSG received
 *
 * @param[in] dpy The display pointer
 */
static void terminate(pid_t pid, Display * dpy)
{
     if(pid >= 0)
     {
          kill_app(pid);
     }

     if(dpy)
     {
          XCloseDisplay(dpy);
     }
}


/**
 * Called when the user clicks on the play button
 *
 * @param[in] win The window ID
 * @param[in] data The Application details
 */
static void my_start(Window win, AppData_t * data)
{
     Display * dpy = sig_globals.dpy;
     if(sig_globals.childPid > 0)
     {
          return;
     }

     /* If another helper application, switch to that one */
     if(data->nextHelper)
     {
          char buffer[512];
          char *cmd[4];

          XDestroyWindow(dpy, win);
          close(ConnectionNumber(dpy));

          restore_SIGCHLD_to_default();

          cmd[0] = (char *) data->nextHelper;

          snprintf(buffer, sizeof(buffer), "%lu,%d,%d,%lu,%d,%d",
                  data->flags,
                  data->repeats,
                  data->pipe_fd,
                  (unsigned long int) data->window,
                  (int) data->width,
                  (int) data->height);


          cmd[1] = buffer;
          cmd[2] = data->command;
          cmd[3] = 0;

          D("Switching to helper\n");
          execvp(data->nextHelper, cmd);
          exit(EX_UNAVAILABLE);
     }

     /* else linker is used to launch command */
     sig_globals.childPid = spawn_app(data->command, data->flags);
}

/**
 * Terminate the linked application
 */
static void low_die(void)
{
     terminate(sig_globals.childPid, sig_globals.dpy);
     _exit(0);
}

/**
 * Callback function passed to X for when error occurs. This terminates the
 * linked application
 *
 * @param[in] dpy The display pointer (not used)
 * @param[in] ev The X event (not used)
 *
 * @return Always returns zero
 */
static int die(Display *dpy, XErrorEvent *ev)
{
     low_die();
     return 0;
}

/**
 * Callback function passed to X for when error occurs. This terminates the
 * linked application
 *
 * @param[in] dpy The display pointer (not used)
 *
 * @return Always returns zero
 */
static int die2(Display *dpy)
{
     low_die();
     return 0;
}

/**
 * Callback function attached as a signal handler. This terminates the
 * linked application
 *
 * @param[in] sig The signal (not used)
 */
static void sigdie(int sig)
{
     low_die();
}

/**
 * Checks for X events and processes them. Returns when no more events left
 * to process.
 *
 * @param[in] win The window ID
 * @param[in] data The Application data
 * @param[in] doDraw
 *
 * @return nothing
 */
static void check_x_events(Window win, AppData_t * data, int doDraw)
{
     Display * dpy = sig_globals.dpy;
     int numEvents = XPending(dpy);
     static int buttonDown = 0;

     if(doDraw)
     {
          redrawButton(dpy, win, buttonDown);
     }
     while(numEvents > 0)
     {
          XEvent ev;

          XNextEvent(dpy, &ev);

          switch(ev.type)
          {
          case ButtonPress:
               if(ev.xbutton.button == 1)
               {
                    pid_t tmp = sig_globals.childPid;
                    buttonDown = 1;
                    redrawButton(dpy, win, buttonDown); /* do first gives quicker visual feedback */
                    XSync(dpy, False);

                    my_start(win, data);

                    if(tmp != sig_globals.childPid)
                    {
                        redrawButton(dpy, win, buttonDown); /* do first gives quicker visual feedback */
                    }
               }
               break;

          case ButtonRelease:
               if(buttonDown)
               {
                    buttonDown = 0;
                    redrawButton(dpy, win, buttonDown);
               }
               break;

          case Expose:
               if(ev.xexpose.count)
               {
                    break;
               }
          case ResizeRequest:
          case MapNotify:
               redrawButton(dpy, win, buttonDown);
               break;

          default:
               D("Unknown event %d\n",ev.type);
               break;
          }

          /* If this is the last of this batch, check that more havent
           * been added in the meantime as a result of an action */
          numEvents--;
          if(numEvents == 0)
          {
               numEvents = XPending(dpy);
          }
     }
}

/**
 * Given initial width & height of the window containing the buttons
 * limit the width and aspect ratio and place in the center of the
 * window (calculate x & y)
 *
 * @param[in,out] width The width before and after
 * @param[in,out] height The height before and after
 * @param[out] x The x co-ordinate after
 * @param[out] y The y co-ordinate after
 * @param[in] flags The application flags
 */
static void normalise_window_coords(unsigned * width, unsigned * height,
                                    int * x, int * y, unsigned long flags)
{
     unsigned w = *width;
     unsigned h = *height;

     if(w > MAX_BUTTON_SIZE)
     {
          w = MAX_BUTTON_SIZE;
     }

     /* Keep the link button in a square window */
     if(h < w)
     {
          w = h;
     }
     else if(h > w)
     {
          h = w;
     }

     *x = (int) (*width - w)/2;
     *y = (int) (*height - h)/2;

     *height = h;
     *width = w;
}

/**
 * Check to see if new window size information has arrived from plugin and if
 * so resize the button accordingly
 *
 * @param[in] win The window ID
 * @param[in] data The Application data
 */
static void check_pipe_fd_events(Window win, AppData_t * data)
{
     struct PipeMsg_s msg;
     int n;
     int x, y;
     unsigned w, h;

     D("Got pipe_fd data, pipe_fd=%d\n", data->pipe_fd);

     n = read(data->pipe_fd, ((char *) &msg),  sizeof(msg));
     if((n == 0) || ((n < 0) && (errno != EINTR)))
     {
          D("Pipe returned n=%i\n", n);
          terminate(sig_globals.childPid, sig_globals.dpy);
	  exit(EX_UNAVAILABLE);
     }

     if (n != sizeof(msg))
     {
          if(n > 0)
          {
              D("Pipe msg too short, size = %i\n", n);
          }
          return;
     }

     switch(msg.msgType)
     {
          case WINDOW_MSG:
          {
               data->window = (Window) msg.window_msg.window;
               data->width = msg.window_msg.width;
               data->height = msg.window_msg.height;

               /* Adjust the width & height to compensate for the window border width */
               w = (unsigned) msg.window_msg.width - 2 * WINDOW_BORDER_WIDTH;
               h = (unsigned) msg.window_msg.height - 2 * WINDOW_BORDER_WIDTH;

               normalise_window_coords(&w, &h, &x, &y, data->flags);

               D("Linker window: x=%i, y=%i, w=%u, h=%u\n", x, y, w, h);

               XMoveResizeWindow(sig_globals.dpy, win, x, y, w, h);
          }
          break;
     }
}

/**
 * Called if we exit main() early because of invalid passed argument into
 * the main function. print to both debug and stderr (just in case someone
 * called mozplugger-linker directly)
 */
static void exitEarly(void)
{
     D("Invalid parameters passed to Linker - linker exiting\n");

     fprintf(stderr,"MozPlugger version " VERSION
                    " linker application.\n"
                    "Please see 'man mozplugger' for details.\n");
     exit(EXIT_FAILURE);
}

/**
 * Create the on color
 *
 * @param[in] dpy Display handle
 * @param[in] window The window handle
 */
static void createOnColor(Display * dpy, Window window)
{
     XColor colour;

     gc_onColor = XCreateGC(dpy, window, 0, 0);
     colour.red = 0x0000;
     colour.green = 0xa000;
     colour.blue = 0x0000;
     colour.pixel = 0;
     colour.flags=0;

     XAllocColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &colour);
     XSetForeground(dpy, gc_onColor, colour.pixel);
}

/**
 * Create the off color
 *
 * @param[in] dpy Display handle
 * @param[in] window The window handle
 */
static void createOffColor(Display * dpy, Window window)
{
     XColor colour;

     gc_offColor = XCreateGC(dpy, window, 0, 0);
     colour.red = 0x8000;
     colour.green = 0x8000;
     colour.blue = 0x8000;
     colour.pixel = 0;
     colour.flags = 0;

     XAllocColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &colour);
     XSetForeground(dpy, gc_offColor, colour.pixel);
}

/**
 * mozplugger-linker takes two arguments. The first is a shell command line
 * to run when the 'play' button is pressed (or if autostart is set). The second
 * parameter is optional and is the ID of the parent window into which to draw
 * the button.
 *
 * @param[in] argc The number of arguments
 * @param[in] argv Array of the arguments
 *
 * @return Never returns unless the application exits
 */
int main(int argc, char **argv)
{
     unsigned long temp = 0;
     int x, y;
     unsigned int width, height;
     int i;

     XSetWindowAttributes attr;
     Display * dpy = 0;
     Window topLevel;
     int repeatsLeft;
     int sig_chld_fd;
     int rd_chld_fd;

     AppData_t data;

     D("Linker started.....\n");

     if (argc < 3)
     {
          exitEarly();
     }

     i = sscanf(argv[1],"%lu,%d,%d,%lu,%d,%d",
            &data.flags,
            &data.repeats,
            &data.pipe_fd,
            &temp,
            (unsigned int *)&data.width,
            (unsigned int *)&data.height);

     if(i < 6)
     {
          exitEarly();
     }

     data.window = (Window)temp;
     data.command = argv[2];

     D("LINKER: %s %s %s %s\n",
       argv[0],
       argv[1],
       getenv("file"),
       data.command);

     if(argc > 3)
     {
          data.nextHelper = argv[3];
     }
     else
     {
          data.nextHelper = NULL;
     }

     /* The application will use the $repeat variable */
     if (data.flags & H_REPEATCOUNT)
     {
          data.repeats = 1;
     }
     repeatsLeft = data.repeats;

     if(!(dpy = XOpenDisplay(getenv("DISPLAY"))))
     {
          D("%s: unable to open display %s\n",
                 argv[0], XDisplayName(getenv("DISPLAY")));
          exitEarly();
     }

     /* Adjust the width & height to compensate for the window border
      * width */

     width = data.width - 2 * WINDOW_BORDER_WIDTH;
     height = data.height - 2 * WINDOW_BORDER_WIDTH;

     /* x, y co-ords of the parent is of no interest, we need to know
      * the x, y relative to the parent. */
     normalise_window_coords(&width, &height, &x, &y, data.flags);

     D("Linker window: x=%i, y=%i, w=%u, h=%u\n", x, y, width, height);

     attr.border_pixel = 0;
     attr.background_pixel = WhitePixelOfScreen(DefaultScreenOfDisplay(dpy));
     attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask;
     attr.override_redirect=0;

     topLevel = XCreateWindow(dpy,
                              data.window,
                              x, y,
                              width, height,
                              WINDOW_BORDER_WIDTH,
                              CopyFromParent,
                              InputOutput, (Visual *) CopyFromParent,
                              (unsigned long)(CWBorderPixel|
                              CWEventMask|
                               CWOverrideRedirect|
                               CWBackPixel),
                              &attr);

     setWindowClassHint(dpy, topLevel, "mozplugger-linker");

     gc_black = XCreateGC(dpy, topLevel, 0, 0);
     XSetForeground(dpy,gc_black,BlackPixel(dpy, DefaultScreen(dpy)));

     gc_white = XCreateGC(dpy, topLevel, 0, 0);
     XSetForeground(dpy,gc_white,WhitePixel(dpy, DefaultScreen(dpy)));

     createOnColor(dpy, topLevel);

     createOffColor(dpy, topLevel);

     setWindowHints(dpy, topLevel, 3);

     /* Map the window, if the parent has asked for redirect this does nothing
      * (i.e. if swallow has been used in mozplugger.c) */
     XMapWindow(dpy, topLevel);

     XSetIOErrorHandler(die2);
     XSetErrorHandler(die);

     sig_chld_fd = redirect_SIGCHLD_to_fd();
     rd_chld_fd = get_chld_out_fd();

     sig_globals.childPid = -1;
     sig_globals.dpy = dpy;
     signal(SIGHUP, sigdie);
     signal(SIGINT, sigdie);
     signal(SIGTERM, sigdie);

     while(1)
     {
          fd_set fds;
          int maxFd;

          check_x_events(topLevel, &data, 0);

          FD_ZERO(&fds);
          FD_SET(ConnectionNumber(sig_globals.dpy),&fds);
          maxFd = ConnectionNumber(sig_globals.dpy);

          FD_SET(data.pipe_fd, &fds);
          maxFd = data.pipe_fd > maxFd ? data.pipe_fd : maxFd;

          if(sig_chld_fd >= 0)
          {
               FD_SET(sig_chld_fd, &fds);
               maxFd = sig_chld_fd > maxFd ? sig_chld_fd : maxFd;
          }
          if(rd_chld_fd >= 0)
          {
               FD_SET(rd_chld_fd, &fds);
               maxFd = rd_chld_fd > maxFd ? rd_chld_fd : maxFd;
          }


          D("SELECT IN maxFd = %i\n", maxFd);
          if( select(maxFd + 1, &fds, NULL, NULL, 0) > 0)
          {
               if (FD_ISSET(data.pipe_fd, &fds))
               {
                    check_pipe_fd_events(topLevel, &data);
               }
               if(FD_ISSET(sig_chld_fd, &fds))
               {
                    handle_SIGCHLD_event();
               }
               if( FD_ISSET(rd_chld_fd, &fds))
               {
                    handle_chld_out_event(rd_chld_fd);
               }
          }

          D("SELECT OUT\n");

          if(sig_globals.childPid > 0)
          {
               int status = wait_child(sig_globals.childPid);
               if(status <= 0 )
               {
                    sig_globals.childPid = -1;
                    if(data.repeats != INF_LOOPS)
                    {
                         repeatsLeft--;
                    }
                    if(repeatsLeft > 0)
                    {
                         my_start(topLevel, &data);
                         rd_chld_fd = get_chld_out_fd();
                    }
                    check_x_events(topLevel, &data, 1);
               }
          }
          else
          {
               repeatsLeft = data.repeats;
          }
     }
}

