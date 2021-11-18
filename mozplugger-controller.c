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

struct SigGlobals_s
{
    pid_t childPid;
};

typedef struct SigGlobals_s SigGlobals_t;

struct AppData_s
{
     unsigned long cmd_flags;
     char * command;
     int repeats;
     int dlProgress;
     int paused;
};

typedef struct AppData_s AppData_t;



static SigGlobals_t sig_globals;
static GC gc_white;
static GC gc_black;
static GC gc_onColor;
static GC gc_offColor;

/**
 * Redraw the three control buttons
 *
 * @param[in] dpy
 * @param[in] win
 * @param[in] mouseClickPos, where mouse was clicked
 *
 * @return Which button is down
 */
static int redraw(Display * dpy, Window win, int mouseClickPos,
                                                           AppData_t * appData)
{
     static int old_buttonsize = -1;

     int buttonsize;
     XWindowAttributes attr;
     XPoint base;
     int buttonDown = -1;

     XGetWindowAttributes(dpy, win, &attr);
     buttonsize = attr.width / 3;
     if(attr.height < buttonsize)
     {
          buttonsize = attr.height;
     }

     if(old_buttonsize != buttonsize)
     {
          old_buttonsize = buttonsize;
          XFillRectangle(dpy, win, gc_white,
                             0, 0, (unsigned)attr.width, (unsigned)attr.height);
     }


     base.x = (attr.width - buttonsize * 3)/2;
     base.y = (attr.height - buttonsize)/2;

     if(mouseClickPos > 0)
     {
         buttonDown = (mouseClickPos - base.x) / buttonsize;
     }

     /***** play ******/

     drawPlayButton(dpy, win, &base, buttonsize,
            ((sig_globals.childPid > 0) && (!appData->paused)) ? gc_onColor : gc_offColor,
                                           gc_black, gc_white, buttonDown == 0);
#if 0
     drawProgressBar(dpy, win, &base, buttonsize, gc_onColor, gc_black, gc_white, appData->dlProgress);
#endif

     /***** pause *****/
     base.x += buttonsize;
     drawPauseButton(dpy, win, &base, buttonsize,
             ((sig_globals.childPid > 0) && appData->paused) ? gc_onColor : gc_offColor,
                                           gc_black, gc_white, buttonDown == 1);

     /***** stop *****/
     base.x += buttonsize;
     drawStopButton(dpy, win, &base, buttonsize,
             (sig_globals.childPid > 0) ? gc_onColor : gc_offColor, gc_black, gc_white,
                                                               buttonDown == 2);
     return buttonDown;
}

/**
 * Get the value of environment variable 'var' and convert to an integer value
 * - use 'def' if enviroment variable does not exist
 *
 * @param[in] var The name of the variable
 * @param[in] def The default value to use
 *
 * @return The integer value
 */
static int igetenv(char *var, int def)
{
     char *tmp=getenv(var);
     if(!tmp)
     {
          return def;
     }
     return atoi(tmp);
}

/**
 * Called when the user clicks on the play button
 *
 * @param[in] data Data associated with the application to execute
 * @param[in] maxFd The maximum open file descriptor
 */
static void my_play(AppData_t * appData)
{
     appData->paused = 0;
     if(sig_globals.childPid > 0)
     {
          if(!kill(-sig_globals.childPid, SIGCONT))
          {
               return;
          }
     }

     sig_globals.childPid = spawn_app(appData->command, appData->cmd_flags);
}

/**
 * Called when the user clicks on the pause button
 *
 * @param[in] appData Pointer to the App data structure
 */
static void my_pause(AppData_t * appData)
{
     if(sig_globals.childPid > 0)
     {
          if(!kill(-sig_globals.childPid, SIGSTOP))
          {
               appData->paused = 1;
          }
     }
}

/**
 * Terminate the controller application
 */
static void low_die(void)
{
     if(sig_globals.childPid > 0)
     {
          kill_app(sig_globals.childPid);
     }
     _exit(0);
}

/**
 * Called when the user clicks on the stop button
 *
 * @param[in] appData Pointer to the App data structure
 */
static void my_stop(AppData_t * appData)
{
     if(sig_globals.childPid > 0)
     {
          if(appData->paused)
          {
               kill(-sig_globals.childPid, SIGCONT);
               appData->paused = 0;
          }
          kill_app(sig_globals.childPid);
          sig_globals.childPid = -1;
     }
}

/**
 * Terminate, called from signal handler or when SHUTDOWN_MSG received
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
 * Callback function passed to X for when error occurs. This terminates the
 * controlled application
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
 * controlled application
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
 * controlled application
 *
 * @param[in] sig The signal (not used)
 */
static void sigdie(int sig)
{
     low_die();
}

/**
 * Force the repaint to happen
 *
 * @param[in] dpy The display pointer
 * @param[in] win The window ID
 */
static void forceRepaint(Display * dpy, Window win)
{
     XEvent event;

     event.type = Expose;
     event.xexpose.display = dpy;
     event.xexpose.window = win;
     event.xexpose.count = 0;

     XSendEvent(dpy, win, False, ExposureMask, &event);
}

/**
 * Checks for X events and processes them. Returns when no more events left
 * to process.
 *
 * @param[in] dpy
 * @param[in] win
 *
 * @return nothing
 */
static void check_x_events(Display * dpy, Window win, AppData_t * appData)
{
     int numEvents = XPending(dpy);
     static int mouseClickPos = -1;

     while(numEvents > 0)
     {
          XEvent ev;

          XNextEvent(dpy, &ev);

          switch(ev.type)
          {
          case ButtonPress:
               if(ev.xbutton.button == 1)
               {
                    int buttonDown;
                    mouseClickPos = ev.xbutton.x;
                    buttonDown = redraw(dpy, win, mouseClickPos, appData); /* do first gives quicker visual feedback */
                    XSync(dpy, False);

                    switch(buttonDown)
                    {
                    case 0: /* play */
                         my_play(appData);
                         break;
                    case 1: /* pause*/
                         my_pause(appData);
                         break;
                    case 2: /* stop */
                         my_stop(appData);
                         break;
                    }

                    redraw(dpy, win, mouseClickPos, appData);
               }
               break;

          case ButtonRelease:
               if(mouseClickPos != -1)
               {
                    mouseClickPos = -1;
                    redraw(dpy, win, mouseClickPos, appData);
               }
               break;

          case Expose:
               if(ev.xexpose.count)
               {
                    break;
               }
          case ResizeRequest:
          case MapNotify:
               redraw(dpy, win, mouseClickPos, appData);
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
 * @param[in,out] x The x co-ordinate before and after
 * @param[in,out] y The y co-ordinate before and after
 * @param[in] flags The Flags as read from mozpluggerrc
 */
static void normalise_window_coords(unsigned * width, unsigned * height,
                                    int * x, int * y, unsigned long flags)
{
     unsigned w = *width;
     unsigned h = *height;
     const unsigned target_aspect = 3;

     if(flags & H_SMALL_CNTRLS)
     {
          *y = *x = 0;
          *width = 3 * MIN_BUTTON_SIZE;
          *height = MIN_BUTTON_SIZE;
          return;
     }

     if(w > 3 * MAX_BUTTON_SIZE)
     {
          w = 3 * MAX_BUTTON_SIZE;
     }

     /* Keep the controls as close to default ratio as possible */
     if(h > w / target_aspect)
     {
          h = w / target_aspect;
     }

     *x = (int) (*width - w)/2;
     *y = (int) (*height - h)/2;

     *height = h;
     *width = w;
}

/**
 * Check to see if new window size information has arrived from plugin and if
 * so resize the controls accordingly.
 *
 * @param[in] dpy The display
 * @param[in] win The window
 * @param[in] appData Pointer to the application data structure
 * @param[in] pipeFd The Fd of the pipe
 */
static void check_pipe_fd_events(Display * dpy, Window win,
                                    AppData_t * appData, int pipe_fd)
{
     struct PipeMsg_s msg;
     int n;

     D("Got pipe_fd data, pipe_fd=%d\n", pipe_fd);

     n = read(pipe_fd, ((char *) &msg),  sizeof(msg));
     if((n == 0) || ((n < 0) && (errno != EINTR)))
     {
          D("Pipe returned n=%i\n", n);
          terminate(sig_globals.childPid, dpy);
          exit(EX_UNAVAILABLE);
     }

     if(n != sizeof(msg))
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
               int x, y;
               /* Adjust the width & height to compensate for the window border width */
               unsigned w = (unsigned) msg.window_msg.width - 2 * WINDOW_BORDER_WIDTH;
               unsigned h = (unsigned) msg.window_msg.height - 2 * WINDOW_BORDER_WIDTH;

               normalise_window_coords(&w, &h, &x, &y, appData->cmd_flags);

               D("Controller window: x=%i, y=%i, w=%u, h=%u\n", x, y, w, h);

               XMoveResizeWindow(dpy, win, x, y, w, h);
          }
          break;

          case PROGRESS_MSG:
          {
               if(msg.progress_msg.done)
               {
                    appData->dlProgress = -1;
               }
               else
               {
                    appData->dlProgress++;
               }
               forceRepaint(dpy, win);
          }
          break;

          case STATE_CHG_MSG:
          {
               switch(msg.stateChg_msg.stateChgReq)
               {
                    case STOP_REQ:
                          my_stop(appData);
                          break;

                    case PLAY_REQ:
                          my_play(appData);
                          break;

                    case PAUSE_REQ:
                          my_pause(appData);
                          break;
               }
               forceRepaint(dpy, win);
          }
          break;
     }
}

/**
 * Called if we exit main() early because of invalid passed argument into
 * the main function. print to both debug and stderr (just in case someone
 * called mozplugger-controller directly)
 */
static void exitEarly(void)
{
     D("Invalid parameters passed to Controller - controller exiting\n");

     fprintf(stderr,"MozPlugger version " VERSION
                    " controller application.\n"
                    "Please see 'man mozplugger' for details.\n");
     exit(EXIT_FAILURE);
}

/**
 * mozplugger-controller takes two arguments. The first is a shell command line
 * to run when the 'play' button is pressed (or if autostart is set). The second
 * parameter is optional and is the ID of the parent window into which to draw
 * the controls.
 *
 * If the command line contains the string "$window", then no controls are
 * drawn.
 *
 * @param[in]  argc The number of arguments
 * @param[in]  argv Array of the arguments
 * @param[out] pWidth Width of window
 * @param[out] pHeight Height of window
 * @param[out] pWindow The Window ID
 * @param[out] command The command line
 *
 * @return True if all Ok
 */
static int readCommandLine(int argc, char **argv, unsigned int * pWidth,
                                                   unsigned int * pHeight,
                                                   Window * pWindow,
                                                   AppData_t * appData,
                                                   int * pPipeFd)
{
     unsigned long temp = 0;
     int i;
     char * fileName;

     D("Controller started.....\n");

     if (argc < 3)
     {
          return 0;
     }

     i = sscanf(argv[1],"%lu,%d,%d,%lu,%d,%d",
            &appData->cmd_flags,
            &appData->repeats,
            pPipeFd,
            &temp,
            (int *)pWidth,
            (int *)pHeight);

     if(i < 6)
     {
          return 0;
     }

     *pWindow = (Window)temp;
     appData->command = argv[2];

     fileName = getenv("file");

     D("CONTROLLER: %s %s %s %s\n",
       argv[0],
       argv[1],
       fileName ? fileName : "NULL",
       argv[2]);

     /* The application will use the $repeat variable */
     if (appData->cmd_flags & H_REPEATCOUNT)
     {
          appData->repeats = 1;
     }

      /* If the command contains a reference to window it is likely that that
      * application also wants to draw into the same window as the controls
      * so lets use small controls in top left corner */
     if(strstr(appData->command,"$window") || strstr(appData->command,"$hexwindow"))
     {
          appData->cmd_flags |= H_SMALL_CNTRLS;
     }
     appData->paused = 0;
     appData->dlProgress = 0;
     return 1;
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
 * mozplugger-controller main()
 *
 * If the command line contains the string "$window", then no controls are
 * drawn.
 *
 * @param[in] argc The number of arguments
 * @param[in] argv Array of the arguments
 *
 * @return Never returns unless the application exits
 */
int main(int argc, char **argv)
{
     /* Set defaults, may be changed later */
     unsigned int width =  3 * DEFAULT_BUTTON_SIZE;
     unsigned int height = DEFAULT_BUTTON_SIZE;
     int x, y;

     Window parentWid = 0;
     AppData_t appData;

     XSetWindowAttributes attr;
     Display * dpy = 0;
     Window topLevel;
     int pipe_fd;
     int repeatsLeft;
     int sig_chld_fd;

     if(!readCommandLine(argc, argv, &width,
                                 &height,
                                 &parentWid,
                                 &appData,
                                 &pipe_fd))
     {
         exitEarly();
     }


     repeatsLeft = appData.repeats;

     if(!(dpy = XOpenDisplay(getenv("DISPLAY"))))
     {
          D("%s: unable to open display %s\n",
                   argv[0], XDisplayName(getenv("DISPLAY")));
          exitEarly();
     }

     /* Adjust the width & height to compensate for the window border
      * width */
     width -= 2 * WINDOW_BORDER_WIDTH;
     height -= 2 * WINDOW_BORDER_WIDTH;

     /* x, y co-ords of the parent is of no interest, we need to know
      * the x, y relative to the parent. */
     normalise_window_coords(&width, &height, &x, &y, appData.cmd_flags);

     D("Controller window: x=%i, y=%i, w=%u, h=%u\n", x, y, width, height);

     attr.border_pixel = 0;
     attr.background_pixel = WhitePixelOfScreen(DefaultScreenOfDisplay(dpy));
     attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask;
     attr.override_redirect=0;

     topLevel = XCreateWindow(dpy,
                              parentWid,
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

     setWindowClassHint(dpy, topLevel, "mozplugger-controller");

     gc_black=XCreateGC(dpy,topLevel,0,0);
     XSetForeground(dpy,gc_black,BlackPixel(dpy,DefaultScreen(dpy)));

     gc_white=XCreateGC(dpy,topLevel,0,0);
     XSetForeground(dpy,gc_white,WhitePixel(dpy,DefaultScreen(dpy)));

     createOnColor(dpy, topLevel);

     createOffColor(dpy, topLevel);

     setWindowHints(dpy, topLevel, 3);

     /* Map the window, if the parent has asked for redirect this does nothing
      * (i.e. if swallow has been used in mozplugger.c) */
     XMapWindow(dpy, topLevel);

     XSetIOErrorHandler(die2);
     XSetErrorHandler(die);


     sig_globals.childPid = -1;
     signal(SIGHUP, sigdie);
     signal(SIGINT, sigdie);
     signal(SIGTERM, sigdie);

     if(igetenv("autostart",1))
     {
          my_play(&appData);
     }

     sig_chld_fd = redirect_SIGCHLD_to_fd();

     while(1)
     {
          fd_set fds;
          int maxFd;
          int rd_chld_fd = get_chld_out_fd();

          check_x_events(dpy, topLevel, &appData);

          FD_ZERO(&fds);

          maxFd = ConnectionNumber(dpy);
          FD_SET(ConnectionNumber(dpy), &fds);

          maxFd = pipe_fd > maxFd ? pipe_fd : maxFd;
          FD_SET(pipe_fd, &fds);

          if(sig_chld_fd >= 0)
          {
               maxFd = sig_chld_fd > maxFd ? sig_chld_fd : maxFd;
               FD_SET(sig_chld_fd, &fds);
          }
          if(rd_chld_fd >= 0)
          {
               FD_SET(rd_chld_fd, &fds);
               maxFd = rd_chld_fd > maxFd ? rd_chld_fd : maxFd;
          }


          D("SELECT IN maxFd = %i\n", maxFd);
          if( select(maxFd + 1, &fds, NULL, NULL, 0) > 0)
          {
               if (FD_ISSET(pipe_fd, &fds))
               {
                    check_pipe_fd_events(dpy, topLevel, &appData, pipe_fd);
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
               int status;
               if(waitpid(sig_globals.childPid, &status, WNOHANG) > 0)
               {
                    appData.paused = 0;
                    sig_globals.childPid = -1;
                    if(appData.repeats != INF_LOOPS)
                    {
                         repeatsLeft--;
                    }
                    if(repeatsLeft > 0)
                    {
                         my_play(&appData);
                         rd_chld_fd = get_chld_out_fd();
                    }
                    forceRepaint(dpy, topLevel);
               }
          }
          else
          {
               repeatsLeft = appData.repeats;
          }
     }
}

