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

#define MAX_CONTROLS_WIDTH           300
#define MAX_CONTROLS_HEIGHT          100

#define DEFAULT_CONTROLS_WIDTH        60
#define DEFAULT_CONTROLS_HEIGHT       20

#define MIN_CONTROLS_WIDTH            48
#define MIN_CONTROLS_HEIGHT           16

#define WINDOW_BORDER_WIDTH           1

#define BUTTON_DEPTH                  2

static Display *dpy=0;
static Window topLevel;
static int buttonsize=10;

#define STATE_PLAY 1
#define STATE_PAUSE 2
#define STATE_STOP 3

static int state=STATE_STOP;
static int buttonDown = -1;

static pid_t pid=-1;

static int repeats = 0;
static int repeatsResetVal;

static int pipe_fd;
static int flags;

static GC gc_white;
static GC gc_black;
static GC gc_onColor;
static GC gc_offColor;

static int bx;

static int smallControls=0;

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
 * @brief draw 3D rectangle
 *
 * Draw a rectangle, if pressed is false, give the rectangle shadow to give 
 * impress of a raised button
 *
 * @param[in] x       X-coordinate
 * @param[in] y       Y-coordinate
 * @param[in] w       width
 * @param[in] h       height
 * @param[in] d       depth
 * @param[in] c       The color of the button
 * @param[in] pressed If the button is pressed
 *
 *****************************************************************************/
static void draw3dRectangle(const int x, const int y, const unsigned w, 
                            const unsigned h, const GC c, const int d, 
                            const int pressed)
{
     if(pressed)
     {    
          XFillRectangle(dpy, topLevel, c, x, y, w, h);
          /* erase left bit of previous not pressed button */
          XFillRectangle(dpy, topLevel, gc_white, x-d, y-d, d, h+d);
          /* erase top bit of previous not pressed button */
          XFillRectangle(dpy, topLevel, gc_white, x, y-d, w, d);
     }
     else
     {  
          int i;

          XFillRectangle(dpy, topLevel, c, x-d, y-d, w, h);

          /* Draw shadow */
          for(i=BUTTON_DEPTH; i > 0; i--)
          {
               XDrawLine(dpy, topLevel, gc_black, x+w-i, y+1-i, x+w-i, y+h-i);
               XDrawLine(dpy, topLevel, gc_black, x+1-i, y+h-i, x+w-i, y+h-i);
          }
     }
}

/******************************************************************************/
/**
 * @brief redraw
 *
 * Redraw the three control buttons
 *
 *****************************************************************************/
static void redraw(void)
{
     static int old_buttonsize = -1;
     XWindowAttributes attr;
     XPoint points[6];
     XPoint base;
     int x, y;
     const int d = BUTTON_DEPTH;
     unsigned w, h;
     GC c;

     XGetWindowAttributes(dpy, topLevel, &attr);
     buttonsize = attr.width/3;
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
    

     base.x = (attr.width - buttonsize*3)/2;
     base.y = (attr.height - buttonsize)/2;

     /* Set global variable, required by button press event */
     bx = base.x;

     /***** play ******/

     {
          int s = (buttonDown != 0) ? d : 0;

          points[0] = coordAdd(&base,       scale(5)-s,  scale(2)-s);
          points[1] = coordAdd(&points[0],  scale(2),    0);
          points[2] = coordAdd(&points[1],  scale(5),    scale(5));

          points[5] = coordAdd(&base,       scale(5)-s, scale(13)-s);
          points[4] = coordAdd(&points[5],  scale(2),   0);
          points[3] = coordAdd(&points[4],  scale(5),  -scale(5));
     }

     c = (state==STATE_PLAY) ? gc_onColor : gc_offColor;

     if(buttonDown != 0)
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
          XFillPolygon(dpy, topLevel, c, points, 6,
                 Convex, CoordModeOrigin);

          x = points[0].x;
          y = points[0].y;
          w = points[2].x - points[0].x;
          h = points[4].y - points[0].y;

          /* erase left bit of previous not pressed button */
          XFillRectangle(dpy, topLevel, gc_white, x-d, y-d, d, h+d);
          /* erase top bit of previous not pressed button */
          XFillRectangle(dpy, topLevel, gc_white, x, y-d, w, d);
     }

     /***** pause *****/
     base.x += buttonsize;
     y = base.y + scale(2);
     w = (unsigned) scale(3) + 1;
     h = (unsigned) scale(11) + 1;
     c = (state==STATE_PAUSE) ? gc_onColor : gc_offColor;

     draw3dRectangle(base.x + scale(3),
                     y, w, h, c, d, buttonDown == 1);

     draw3dRectangle(base.x + scale(9),
                     y, w, h, c, d, buttonDown == 1);

     /***** stop *****/
     base.x += buttonsize;
     w = (unsigned) scale(9);

     draw3dRectangle(base.x + scale(3) + 1,
                     base.y + scale(3) + 1,
                     w, w,
                     (state==STATE_STOP) ? gc_onColor : gc_offColor,
                     d,
                     buttonDown == 2);
}

/******************************************************************************/
/**
 * @brief Get environment variable as integer
 *
 * Get the value of environment variable 'var' and convert to an integer value
 * - use 'def' if enviroment variable does not exist
 *
 * @param[in] var The name of the variable
 * @param[in] def The default value to use
 *
 * @return The integer value
 *
 *****************************************************************************/
static int igetenv(char *var, int def)
{
     char *tmp=getenv(var);
     if(!tmp) 
     {
          return def;
     }
     return atoi(tmp);
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
static void my_play(char * command)
{
     int maxFd;

     if(state != STATE_STOP)
     {
          if(!kill(-pid, SIGCONT))
          {
               state=STATE_PLAY;
               return;
          }
     }

     maxFd = pipe_fd;
     if(dpy && ConnectionNumber(dpy) > maxFd)
     {
         maxFd = ConnectionNumber(dpy);
     }

     pid = spawn_app(command, flags, maxFd);
     if(pid == -1)
     {
          state=STATE_STOP;
          return;
     }

     state=STATE_PLAY;
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
 * @brief Pause
 *
 * Called when the user clicks on the pause button
 *
 * @param[in] command The command to execute (not used)
 *
 *****************************************************************************/
static void my_pause(char * command)
{
     if(state != STATE_STOP)
     {
          if(!kill(-pid, SIGSTOP))
          {
               state = STATE_PAUSE;
          }
          else
          {
               state = STATE_STOP;
          }
          return;
     }
}

/******************************************************************************/
/**
 * @brief Wrapper for kill
 *
 * Terminate the controller application
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
 * @brief Stop
 *
 * Called when the user clicks on the stop button
 *
 * @param[in] command The command to execute (not used)
 *
 *****************************************************************************/
static void my_stop(char * command)
{
     if(state == STATE_PAUSE) 
     {
          my_play(command);
     }
     if(state == STATE_PLAY)
     {
          if(pid > 0) 
          {
               my_kill(-pid);
          }
          state=STATE_STOP;
          repeats=0;
     }
}

/******************************************************************************/
/**
 * @brief Wrapper for kill
 *
 * Callback function passed to X for when error occurs. This terminates the 
 * controlled application
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
 * controlled application
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
 * controlled application
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
                    buttonDown = (ev.xbutton.x - bx) / buttonsize;
                    redraw();  /* do first gives quicker visual feedback */
                    XSync(dpy, False);

                    switch(buttonDown)
                    {
                    case 0: /* play */ 
                         my_play(command); 
                         break;
                    case 1: /* pause*/ 
                         my_pause(command);
                         break;
                    case 2: /* stop */ 
                         my_stop(command); 
                         break;
                    }
               }
               break;

          case ButtonRelease:
               if(buttonDown != -1)
               {   
                    buttonDown = -1;
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
 * @param[in,out] x      The x co-ordinate before and after
 * @param[in,out] y      The y co-ordinate before and after
 *
 ******************************************************************************/
static void normalise_window_coords(unsigned * width, unsigned * height,
                                    int * x, int * y)
{
     unsigned w = *width;
     unsigned h = *height;
     const unsigned target_aspect = DEFAULT_CONTROLS_WIDTH /
                                               DEFAULT_CONTROLS_HEIGHT;
     if(smallControls)
     {
          *y = *x = 0;
          *width  = MIN_CONTROLS_WIDTH;
          *height = MIN_CONTROLS_HEIGHT;
          return;
     }

     if(flags & H_FILL)        /* Dont bother if user wants to fill the window */
     {
          *x = *y = 0;
          return;
     }

     if((w > MAX_CONTROLS_WIDTH) & !(flags & H_MAXASPECT))
     {
          w = MAX_CONTROLS_WIDTH;
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

/******************************************************************************/
/**
 * @brief Check incoming pipe 
 *
 * Check to see if new window size information has arrived from plugin and if
 * so resize the controls accordingly
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

     /* Adjust the width & height to compensate for the window border width */
     w = (unsigned) wintmp.width -  2 * WINDOW_BORDER_WIDTH;
     h = (unsigned) wintmp.height - 2 * WINDOW_BORDER_WIDTH;

     normalise_window_coords(&w, &h, &x, &y);

     D("Controller window: x=%i, y=%i, w=%u, h=%u\n", x, y, w, h);

     XMoveResizeWindow(dpy, topLevel, x, y, w, h);
}

/******************************************************************************/
/**
 * @brief Exit early
 *
 * Called if we exit main() early because of invalid passed argument into
 * the main function. print to both debug and stderr (just in case someone
 * called mozplugger-controller directly)
 *
 *****************************************************************************/
static void exitEarly(void)
{
     D("Invalid parameters passed to Controller - controller exiting\n");

     fprintf(stderr,"MozPlugger version " VERSION 
                    " controller application.\n"
                    "Please see 'man mozplugger' for details.\n");
     exit(1);
}

/******************************************************************************/
/**
 * mozplugger-controller takes two arguments. The first is a shell command line
 * to run when the 'play' button is pressed (or if autostart is set). The second
 * parameter is optional and is the ID of the parent window into which to draw
 * the controls.
 *
 * If the command line contains the string "$window", then no controls are 
 * drawn.
 *
 * @param[in]  argc    The number of arguments
 * @param[in]  argv    Array of the arguments
 * @param[out] pWidth  Width of window
 * @param[out] pHeight Height of window
 * @param[out] pWindow The Window ID
 * @param[out] command The command line
 *
 * @return True if all Ok
 *
 ******************************************************************************/
static int readCommandLine(int argc, char **argv, unsigned int * pWidth,
                                                   unsigned int * pHeight,
                                                   Window * pWindow,
                                                   char ** pCommand)
{
     unsigned long temp = 0;
     int x, y ;
     int i;
     char * fileName;

     D("Controller started.....\n");

     if (argc < 3)
     {
          return 0;
     }

     /* Global variables set are:
        - repeatsResetVal
        - pipe_fd
        - flags */

     i = sscanf(argv[1],"%d,%d,%d,%lu,%d,%d,%d,%d",
            &flags,
            &repeatsResetVal,
            &pipe_fd,
            &temp,
            (int *)&x,  /* Not needed and not used */
            (int *)&y,  /* Not needed and not used */
            (int *)pWidth,
            (int *)pHeight);

     if(i < 8)
     {
          return 0;
     }

     *pWindow = (Window)temp;
     *pCommand = argv[2];

     fileName = getenv("file"); 

     D("CONTROLLER: %s %s %s %s\n",
       argv[0],
       argv[1],
       fileName ? fileName : "NULL",
       argv[2]);

     return 1;
}

/******************************************************************************/
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
 *
 ******************************************************************************/
int main(int argc, char **argv)
{
     int old_state;

     /* Set defaults, may be changed later */
     unsigned int width =  DEFAULT_CONTROLS_WIDTH; 
     unsigned int height = DEFAULT_CONTROLS_HEIGHT;
     int x, y;

     Window parentWid = 0;
     char * command = 0;

     XColor colour;
     XClassHint classhint;
     XSetWindowAttributes attr;
     XSizeHints wmHints;

     if(!readCommandLine(argc, argv, &width, 
                                 &height, 
                                 &parentWid,
                                 &command))
     {
         exitEarly();
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

     /* If the command contains a reference to window it is likely that that
      * application also wants to draw into the same window as the controls
      * so lets use small controls in top left corner */
     if(strstr(command,"$window") || strstr(command,"$hexwindow"))
     {
          smallControls=1;
     }

     /* Adjust the width & height to compensate for the window border
      * width */
     width  -= 2 * WINDOW_BORDER_WIDTH;
     height -= 2 * WINDOW_BORDER_WIDTH;

     /* x, y co-ords of the parent is of no interest, we need to know
      * the x, y relative to the parent. */
     normalise_window_coords(&width, &height, &x, &y);
     
     D("Controller window: x=%i, y=%i, w=%u, h=%u\n", x, y, width, height);

     wmHints.x=0;
     wmHints.y=0;
     wmHints.min_width  = MIN_CONTROLS_WIDTH;
     wmHints.min_height = MIN_CONTROLS_HEIGHT;
     wmHints.base_width  = DEFAULT_CONTROLS_WIDTH;
     wmHints.base_height = DEFAULT_CONTROLS_HEIGHT;
     wmHints.flags=PPosition | USPosition | PMinSize;

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

     classhint.res_name = "mozplugger-controller";
     classhint.res_class = "mozplugger-controller";
     XSetClassHint(dpy, topLevel, &classhint);
     XStoreName(dpy, topLevel, "mozplugger-controller");

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

     old_state=state;

     redirect_SIGCHLD_to_fd();

     signal(SIGHUP, sigdie);
     signal(SIGINT, sigdie);
     signal(SIGTERM, sigdie);

     if(igetenv("autostart",1))
     {
          my_play(command);
     }

     while(1)
     {
          fd_set fds;
          int maxFd;
          int sig_chld_fd;

          if(state != old_state)
          {
               redraw();
               old_state=state;
          }

          check_x_events(command);

          FD_ZERO(&fds);

          FD_SET(ConnectionNumber(dpy), &fds);
          FD_SET(pipe_fd, &fds);

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
                    pid=-1;
                    if(state == STATE_PLAY)
                    {
                         state = STATE_STOP;
                         if(repeats > 0) 
                         {
                              my_play(command);
                         }
                    }
               }
          }
     }
}

