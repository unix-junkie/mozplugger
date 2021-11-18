/*****************************************************************************
 *
 * Original author: Fredrik Hübinette <hubbe@hubbe.net>
 *
 * Current Authors: Louis Bavoil <bavoil@cs.utah.edu>
 *                  Peter Leese <hubbe@hubbe.net>
 *
 * This code is based on and is a branch of plugger written 
 * by Fredrik Hübinette <hubbe@hubbe.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sysexits.h>
#include <signal.h>
#include <npapi.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <X11/X.h>
#include <X11/Xutil.h>

#include "mozplugger.h"
#include "child.h"
#include "debug.h"

#define MAX_BUTTON_SIZE           100
#define DEFAULT_BUTTON_SIZE        20
#define MIN_BUTTON_SIZE            16

#define WINDOW_BORDER_WIDTH           1

#define BUTTON_DEPTH                  2

static Display *dpy=0;
static Window topLevel;
static int buttonsize=10;

static int buttonDown = 0;

static pid_t pid = -1;

static int repeats = 0;
static int repeatsResetVal;

static int pipe_fd;
static int flags;

static struct 
{
     Window  window;
     int x;
     int y;
     unsigned int width;
     unsigned int height;
} parentDetails;

static GC gc_white;
static GC gc_black;
static GC gc_onColor;
static GC gc_offColor;

static const char * nextHelper = NULL;

/******************************************************************************/
/**
 * @brief scale
 *
 * Given v when buttonsize is 16 scale to v if buttonsize was buttonsize,
 * make sure we round up or down correctly.
 *
 * @param[in] v The value to scale
 *
 * @return The scaled value
 *
 *****************************************************************************/
static int scale(int v)
{
     return (v * buttonsize +8) / 16;
}

/******************************************************************************/
/**
 * @brief coordAdd
 *
 * Given origin point and an x and y translation, calculate new point
 *
 * @param[in] origin The origin
 * @param[in] x The X translation 
 * @param[in] y The Y translation
 *
 * @return The co-ordinates as a XPoint structure
 *
 *****************************************************************************/
static XPoint coordAdd(const XPoint * const origin, int x, int y)
{
     XPoint ret;
     ret.x = origin->x + x;
     ret.y = origin->y + y;
     return ret;
}

/******************************************************************************/
/**
 * @brief redraw
 *
 * Redraw the link button
 *
 *****************************************************************************/
static void redraw(void)
{
     static int old_buttonsize = -1;
     XWindowAttributes attr;
     XPoint points[6];
     XPoint base;
     const int d = BUTTON_DEPTH;
     GC c = (pid > 0) ? gc_onColor : gc_offColor;
     const int s = (!buttonDown) ? d : 0;

     XGetWindowAttributes(dpy, topLevel, &attr);
     buttonsize = attr.width;
     if(attr.height < buttonsize) 
     {
          buttonsize=attr.height;
     }

     if(old_buttonsize != buttonsize)
     {
          old_buttonsize=buttonsize;
          XFillRectangle(dpy, topLevel, gc_white,
                         0,0,(unsigned)attr.width, (unsigned)attr.height);
     }
    

     base.x = (attr.width - buttonsize)/2;
     base.y = (attr.height - buttonsize)/2;

     points[0] = coordAdd(&base,       scale(5)-s,  scale(2)-s);
     points[1] = coordAdd(&points[0],  scale(2),    0);
     points[2] = coordAdd(&points[1],  scale(5),    scale(5));

     points[5] = coordAdd(&base,       scale(5)-s, scale(13)-s);
     points[4] = coordAdd(&points[5],  scale(2),   0);
     points[3] = coordAdd(&points[4],  scale(5),  -scale(5));

     if(!buttonDown)
     {
          int i;

          XFillPolygon(dpy, topLevel, c, points, 6,
                  Convex, CoordModeOrigin);

          points[2].y++;
          points[5].x++;

          /* Draw the shadow */
          for(i = BUTTON_DEPTH; i > 0; i--)
          {
               XDrawLines(dpy, topLevel, gc_black, &points[2], 4, CoordModeOrigin);

               XDrawLine(dpy, topLevel, gc_black, points[3].x-1, points[3].y, 
                                                 points[4].x-1, points[4].y);

               points[2].x++; points[2].y++;
               points[3].x++; points[3].y++;
               points[4].x++; points[4].y++;
               points[5].x++; points[5].y++;
          }
     }
     else
     {
          const int x = points[0].x;
          const int y = points[0].y;
          const unsigned w = points[2].x - points[0].x;
          const unsigned h = points[4].y - points[0].y;

          XFillPolygon(dpy, topLevel, c, points, 6,
                  Convex, CoordModeOrigin);

          /* erase left bit of previous not pressed button */
          XFillRectangle(dpy, topLevel, gc_white, x-d, y-d, d, h+d);
          /* erase top bit of previous not pressed button */
          XFillRectangle(dpy, topLevel, gc_white, x, y-d, w, d);
     }
}

/******************************************************************************/
/**
 * @brief Play
 *
 * Called when the user clicks on the play button
 *
 * @param[in] command The command to execute
 *
 *****************************************************************************/
static void my_start(char * command)
{
     int maxFd;
     if(pid > 0)
     {
          return;
     }

     /* If another helper application, switch to that one */
     if(nextHelper)
     {
          char buffer[ENV_BUFFER_SIZE];
          char *cmd[4];

          XDestroyWindow(dpy, topLevel);
          close(ConnectionNumber(dpy));

          restore_SIGCHLD_to_default();

          cmd[0] = (char *) nextHelper;

          snprintf(buffer, sizeof(buffer), "%d,%d,%d,%lu,%d,%d,%d,%d",
                  flags,
                  repeats,
                  pipe_fd,
                  (unsigned long int) parentDetails.window,
                  (int) parentDetails.x,
                  (int) parentDetails.y,
                  (int) parentDetails.width,
                  (int) parentDetails.height);


          cmd[1] = buffer;
          cmd[2] = command;
          cmd[3] = 0;

          D("Switching to helper\n");
          execvp(nextHelper, cmd);
          exit(EX_UNAVAILABLE);
     }

     /* else linker is used to launch command */
     maxFd = pipe_fd;
     if(dpy && ConnectionNumber(dpy) > maxFd)
     {
         maxFd = ConnectionNumber(dpy);
     }

     pid = spawn_app(command, flags, maxFd);
     if(pid == -1)
     {
          return;
     }

     if(!repeats) 
     {
          repeats = repeatsResetVal;
     }
     if((repeats > 0) && (repeats != MAXINT)) 
     {
          repeats--;
     }
}

/******************************************************************************/
/**
 * @brief Wrapper for kill
 *
 * Terminate the linked application
 *
 *****************************************************************************/
static void low_die(void)
{
     if(pid > 0) 
     {
           my_kill(-pid);
     }
     _exit(0);
}

/******************************************************************************/
/**
 * @brief Wrapper for kill
 *
 * Callback function passed to X for when error occurs. This terminates the 
 * linked application
 *
 * @param[in] dpy The display pointer (not used)
 * @param[in] ev  The X event         (not used) 
 *
 * @return Always returns zero
 *
 *****************************************************************************/
static int die(Display *dpy, XErrorEvent *ev) 
{ 
     low_die(); 
     return 0; 
}

/******************************************************************************/
/**
 * @brief Wrapper for kill
 *
 * Callback function passed to X for when error occurs. This terminates the 
 * linked application
 *
 * @param[in] dpy The display pointer (not used)
 *
 * @return Always returns zero
 *
 *****************************************************************************/
static int die2(Display *dpy) 
{ 
     low_die(); 
     return 0; 
}

/******************************************************************************/
/**
 * @brief Wrapper for kill
 *
 * Callback function attached as a signal handler. This terminates the 
 * linked application
 *
 * @param[in] sig The signal (not used)
 *
 *****************************************************************************/
static void sigdie(int sig) 
{ 
     low_die(); 
}

/******************************************************************************/
/**
 * Checks for X events and processes them. Returns when no more events left
 * to process.
 *
 * @param[in] command - The command to execute if play pressed.
 *
 * @return nothing
 *
 *****************************************************************************/
static void check_x_events(char * command)
{
     int numEvents = XPending(dpy);

     while(numEvents > 0)
     {
          XEvent ev;

          XNextEvent(dpy, &ev);

          switch(ev.type)
          {
          case ButtonPress:
               if(ev.xbutton.button == 1)
               {
                    buttonDown = 1;
                    redraw();  /* do first gives quicker visual feedback */
                    XSync(dpy, False);

                    my_start(command); 
               }
               break;

          case ButtonRelease:
               if(buttonDown)
               {   
                    buttonDown = 0;
                    redraw();
               }     
               break;

          case Expose:
               if(ev.xexpose.count)
               {
                    break;
               }
          case ResizeRequest:
          case MapNotify:
               redraw();
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

/******************************************************************************/
/** 
 * @brief Normalise window co-ordinates
 *
 * Given initial width & height of the window containing the buttons 
 * limit the width and aspect ratio and place in the center of the 
 * window (calculate x & y)
 *
 * @param[in,out] width  The width before and after
 * @param[in,out] height The height before and after
 * @param[out] x         The x co-ordinate after
 * @param[out] y         The y co-ordinate after
 *
 ******************************************************************************/
static void normalise_window_coords(unsigned * width, unsigned * height,
                                    int * x, int * y)
{
     unsigned w = *width;
     unsigned h = *height;

     if(flags & H_FILL)        /* Dont bother if user wants to fill the window */
     {
          *x = *y = 0;
          return;
     }

     if((w > MAX_BUTTON_SIZE) & !(flags & H_MAXASPECT))
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

/******************************************************************************/
/**
 * @brief Check incoming pipe 
 *
 * Check to see if new window size information has arrived from plugin and if
 * so resize the button accordingly
 *
 ******************************************************************************/
static void check_pipe_fd_events(void)
{
     NPWindow wintmp;
     int n;
     int x, y;
     unsigned w, h;

     D("Got pipe_fd data, pipe_fd=%d\n", pipe_fd);
  
     n = read(pipe_fd, ((char *)& wintmp),  sizeof(wintmp));
     if (n < 0)
     {
          if (errno == EINTR)
          {
               return;
          }
          exit(EX_UNAVAILABLE);
     }
  
     if (n == 0)
     {
          exit(EX_UNAVAILABLE);
     }
  
     if (n != sizeof(wintmp))
     {
          return;
     }

     parentDetails.window = (Window) wintmp.window;
     parentDetails.x = wintmp.x;
     parentDetails.y = wintmp.y;
     parentDetails.width = wintmp.width;
     parentDetails.height = wintmp.height;

     /* Adjust the width & height to compensate for the window border width */
     w = (unsigned) wintmp.width -  2 * WINDOW_BORDER_WIDTH;
     h = (unsigned) wintmp.height - 2 * WINDOW_BORDER_WIDTH;

     normalise_window_coords(&w, &h, &x, &y);

     D("Linker window: x=%i, y=%i, w=%u, h=%u\n", x, y, w, h);

     XMoveResizeWindow(dpy, topLevel, x, y, w, h);
}

/******************************************************************************/
/**
 * @brief Exit early
 *
 * Called if we exit main() early because of invalid passed argument into
 * the main function. print to both debug and stderr (just in case someone
 * called mozplugger-linker directly)
 *
 *****************************************************************************/
static void exitEarly(void)
{
     D("Invalid parameters passed to Linker - linker exiting\n");

     fprintf(stderr,"MozPlugger version " VERSION 
                    " linker application.\n"
                    "Please see 'man mozplugger' for details.\n");
     exit(1);
}

/******************************************************************************/
/**
 * @brief main
 *
 * mozplugger-linker takes two arguments. The first is a shell command line
 * to run when the 'play' button is pressed (or if autostart is set). The second
 * parameter is optional and is the ID of the parent window into which to draw
 * the button.
 *
 * @param[in] argc The number of arguments
 * @param[in] argv Array of the arguments
 *
 * @return Never returns unless the application exits
 *
 ******************************************************************************/
int main(int argc, char **argv)
{
     int old_pid;

     unsigned long temp = 0;
     int x, y;
     unsigned int width, height;
     char * command;
     int i;

     XColor colour;
     XClassHint classhint;
     XSetWindowAttributes attr;
     XSizeHints wmHints;

     D("Linker started.....\n");

     if (argc < 3)
     {
          exitEarly();
     }

     i = sscanf(argv[1],"%d,%d,%d,%lu,%d,%d,%d,%d",
            &flags,
            &repeatsResetVal,
            &pipe_fd,
            &temp,
            (int *)&parentDetails.x,
            (int *)&parentDetails.y,
            (unsigned int *)&parentDetails.width,
            (unsigned int *)&parentDetails.height);

     if(i < 8)
     {
          exitEarly();
     }

     parentDetails.window = (Window)temp;
     command = argv[2];

     D("LINKER: %s %s %s %s\n",
       argv[0],
       argv[1],
       getenv("file"),
       command);

     if(argc > 3)
     {
          nextHelper = argv[3];
     }
     else
     {
          nextHelper = NULL;
     }

     /* The application will use the $repeat variable */
     if (flags & H_REPEATCOUNT)
     {   
          repeatsResetVal = 1;
     }

     if(!(dpy = XOpenDisplay(getenv("DISPLAY"))))
     {
          D("%s: unable to open display %s\n",
                 argv[0], XDisplayName(getenv("DISPLAY")));
          exitEarly();
     }

     /* Adjust the width & height to compensate for the window border
      * width */

     width = parentDetails.width - 2 * WINDOW_BORDER_WIDTH;
     height = parentDetails.height - 2 * WINDOW_BORDER_WIDTH;

     /* x, y co-ords of the parent is of no interest, we need to know
      * the x, y relative to the parent. */
     normalise_window_coords(&width, &height, &x, &y);
     
     D("Linker window: x=%i, y=%i, w=%u, h=%u\n", x, y, width, height);

     wmHints.x=0;
     wmHints.y=0;
     wmHints.min_width  = MIN_BUTTON_SIZE;
     wmHints.min_height = MIN_BUTTON_SIZE;
     wmHints.base_width  = DEFAULT_BUTTON_SIZE;
     wmHints.base_height = DEFAULT_BUTTON_SIZE;
     wmHints.flags=PPosition | USPosition | PMinSize;

     attr.border_pixel = 0; 
     attr.background_pixel = WhitePixelOfScreen(DefaultScreenOfDisplay(dpy));
     attr.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask;
     attr.override_redirect=0;

     topLevel = XCreateWindow(dpy,
                              parentDetails.window,
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

     classhint.res_name = "mozplugger-linker";
     classhint.res_class = "mozplugger-linker";
     XSetClassHint(dpy, topLevel, &classhint);
     XStoreName(dpy, topLevel, "mozplugger-linker");

     gc_black=XCreateGC(dpy,topLevel,0,0);
     XSetForeground(dpy,gc_black,BlackPixel(dpy,DefaultScreen(dpy)));

     gc_white=XCreateGC(dpy,topLevel,0,0);
     XSetForeground(dpy,gc_white,WhitePixel(dpy,DefaultScreen(dpy)));

     gc_onColor=XCreateGC(dpy,topLevel,0,0);
     colour.red=0x0000;
     colour.green=0xa000;
     colour.blue=0x0000;
     colour.pixel=0;
     colour.flags=0;

     XAllocColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &colour);
     XSetForeground(dpy,gc_onColor,colour.pixel);

     gc_offColor=XCreateGC(dpy,topLevel,0,0);
     colour.red=0x8000;
     colour.green=0x8000;
     colour.blue=0x8000;
     colour.pixel=0;
     colour.flags=0;

     XAllocColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &colour);
     XSetForeground(dpy,gc_offColor,colour.pixel);

     XSetWMNormalHints(dpy, topLevel, &wmHints);

     /* Map the window, if the parent has asked for redirect this does nothing
      * (i.e. if swallow has been used in mozplugger.c) */
     XMapWindow(dpy, topLevel);

     XSetIOErrorHandler(die2);
     XSetErrorHandler(die);

     old_pid=pid;

     redirect_SIGCHLD_to_fd();

     signal(SIGHUP, sigdie);
     signal(SIGINT, sigdie);
     signal(SIGTERM, sigdie);

     while(1)
     {
          fd_set fds;
          int maxFd;
          int sig_chld_fd;

          if(pid != old_pid)
          {
               redraw();
               old_pid=pid;
          }

          check_x_events(command);

          FD_ZERO(&fds);
          FD_SET(ConnectionNumber(dpy),&fds);
          FD_SET(pipe_fd,&fds);

          maxFd = MAX(ConnectionNumber(dpy), pipe_fd);

          sig_chld_fd = get_SIGCHLD_fd();
          if(sig_chld_fd >= 0)
          {
               FD_SET(sig_chld_fd, &fds);
               maxFd = MAX(maxFd, sig_chld_fd);
          }

          D("SELECT IN maxFd = %i\n", maxFd);
          if( select(maxFd + 1, &fds, NULL, NULL, 0) > 0)
          {
               if (FD_ISSET(pipe_fd, &fds))
               {    
                    check_pipe_fd_events();
               }
               if(FD_ISSET(sig_chld_fd, &fds))
               {
                    handle_SIGCHLD_event();
               }
          }
          D("SELECT OUT\n");

          if(pid != -1)
          {
               int status;
               if(waitpid(pid, &status, WNOHANG) > 0)
               {
                    if(pid > 0)
                    {
                         pid = -1;
                         if(repeats > 0) 
                         {
                              my_start(command);
                         }
                    }
               }
          }
     }
}

