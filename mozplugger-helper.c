/*****************************************************************************
 *
 * Original Author:  Fredrik Hübinette <hubbe@hubbe.net>
 *
 * Current Authors: Louis Bavoil <bavoil@cs.utah.edu>
 *                  Peter Leese <peter@leese.net>
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

#define _GNU_SOURCE /* for getsid() */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sysexits.h>
#include <signal.h>
#include <npapi.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <X11/X.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "mozplugger.h"
#include "child.h"
#include "debug.h"

/*****************************************************************************
 * Type declarations
 *****************************************************************************/
static struct
{
     int    noWmRunning;
     int    mapped;
     int    reparented;
     int    reparentedAttemptCount;
     Window window;
     int    borderWidth;
     int    x;
     int    y;
     int    width;
     int    height;
     pid_t  pid;
} victimDetails;

static struct 
{
     Window  window;
     int     width;
     int     height;
} parentDetails;


/*****************************************************************************
 * Global variables
 *****************************************************************************/
static Display * display = 0;
static int pipe_fd;
static int flags;
static int repeats;
static char *winname;
static char *command;
static char *file;

static XWindowAttributes wattr;

#ifdef USE_MUTEX_LOCK
static Atom swallowMutex;
static int  swallowMutexTaken;
#endif

static Atom windowOwnerMark;

static int xaspect;
static int yaspect;

#define MAX_POSS_VICTIMS 100
static unsigned int possible_victim_count = 0;
static Window possible_victim_windows[MAX_POSS_VICTIMS];


/*****************************************************************************/
/**
 * @brief Error handler callback
 *
 * Callback from X when there is an error. If debug defined error message is
 * printed to debug log, otherise nothing is done
 *
 * @param[in] dpy The display pointer
 * @param[in] err Pointer to the error event data
 *
 * @return Always returns zero
 *
 *****************************************************************************/
static int error_handler(Display *dpy, XErrorEvent *err)
{
     char buffer[1024];
     XGetErrorText(dpy, err->error_code, buffer, sizeof(buffer));
     D("!!!ERROR_HANDLER!!!: %s\n", buffer);
     return 0;
}

#ifdef USE_MUTEX_LOCK
/*****************************************************************************/
/**
 * Initialise semaphore (swallow mutex semaphore protection)
 *
 * @return none
 *
 *****************************************************************************/
static void initSwallowMutex(void)
{
     D("Initialising Swallow Mutex\n"); 
     if(display == 0) 
     {
	  fprintf(stderr, "Display not set so cannot initialise semaphore!\n");
     }
     else
     {
          swallowMutex = XInternAtom(display, "MOZPLUGGER_SWALLOW_MUTEX", 0);
          swallowMutexTaken = 0;
     }
}
#endif

/*****************************************************************************/
/**
 * Initialise the X atom used to mark windows as owned by mozplugger
 *
 * @return None
 *
 *****************************************************************************/
static void initWindowOwnerMarker(void)
{
     windowOwnerMark = XInternAtom(display, "MOZPLUGGER_OWNER", 0);
}

/*****************************************************************************/
/**
 * Get Host ID - construct a host ID using host name as source of information
 *
 * @return The host ID
 *
 *****************************************************************************/
static uint32_t getHostId(void)
{
     char hostName[128];
     uint32_t id;
     int i;
   
     memset(hostName, 0, sizeof(hostName));
     gethostname(hostName, sizeof(hostName)-1);

     D("Host Name = \"%s\"\n", hostName);

     /* OK, Create a 32 bit hash value from the host name....
       use this as a host ID, the possiblity of a collison of hash keys 
       effecting the swallow of victims is so infinitimisally small! */

     id = 0;
     for(i=0; i < (int)(sizeof(hostName)/sizeof(uint32_t)); i++)
     {
          id = ((id << 5) ^ (id >> 27)) ^ ((uint32_t *)hostName)[i];
     }
     return id;
}

/*****************************************************************************/
/**
 * Extract host Id and pid from window property. This is common code used in
 * various places and is factored out due to complexity of 64 bit versus 
 * 32 bit machines.
 *
 * @param[in]  w      The window to get property from
 * @param[in]  name   The name of property to get 
 * @param[out] hostId The ID of the host currently owning the mutex
 * @param[out] pid    The process ID of the mutex
 *
 * @return 1(true) if owner found, 0 otherwise
 *
 *****************************************************************************/
static int getOwnerFromProperty(Window w, Atom name, uint32_t * hostId, uint32_t * pid)
{
     unsigned long nitems;
     unsigned long bytes;      
     int fmt;
     Atom type;
     unsigned char * property = NULL;
     int success = 0;

     /* Get hold of the Host & PID that current holds the semaphore for 
       this display! - watch out for the bizarre 64-bit platform behaviour
       where property returned is actually an array of 2 x  64bit ints with
       the top 32bits set to zero! */
     XGetWindowProperty(display, w, name,
                       0, 2, 0, XA_INTEGER,
                       &type, &fmt, &nitems, &bytes,
                       &property);

     if(property)
     {
          D("XGetWindowProperty() passed\n");

          /* Just check all is correct! */
	  if((type != XA_INTEGER) || (fmt!=32) || (nitems!=2))
          {
	       fprintf(stderr, "XGetWindowProperty returned bad values " 
                            "%ld,%d,%lu,%lu\n", (long) type, fmt, nitems, 
                            bytes);
          }
	  else
	  {
               *hostId = (uint32_t)((unsigned long *)property)[0];
               *pid = (uint32_t)((unsigned long *)property)[1];
               success = 1;
          }
	  XFree(property);
     }
     else
     {
          D("XGetWindowProperty() failed\n");
     }
     return success;
}

#ifdef USE_MUTEX_LOCK
/*****************************************************************************/
/**
 * Get owner of mutex semaphore.  Returns the current owner of the semaphore
 *
 * @param[out] hostId The ID of the host currently owning the mutex
 * @param[out] pid    The process ID of the mutex
 *
 * @return 1(true) if owner found, 0 otherwise
 *
 *****************************************************************************/
static int getSwallowMutexOwner(uint32_t * hostId, uint32_t * pid)
{   
     return getOwnerFromProperty(wattr.root, swallowMutex, hostId, pid);
}

/*****************************************************************************/
/**
 * Set owner of mutex semaphore
 *
 * @param[in] hostId The ID of the new owner of the mutex
 * @param[in] pid    The process ID of the mutex
 *
 * @return none
 *
 *****************************************************************************/
static void setSwallowMutexOwner(uint32_t hostId, uint32_t pid)
{
     unsigned long temp[2] = {hostId, pid};

     D("Setting swallow mutex owner, hostId = 0x%08X, pid=%u\n", 
                                            (unsigned) hostId, (unsigned) pid); 
     XChangeProperty(display, wattr.root, swallowMutex,
                        XA_INTEGER, 32, PropModeAppend,
                        (unsigned char*) (&temp), 2);
}

/*****************************************************************************/
/**
 * Take mutex semaphore ownership.
 *
 * @return none
 *
 *****************************************************************************/
static void takeSwallowMutex(void)
{
     int countDown;
     const uint32_t ourPid = (uint32_t)getpid();
     const uint32_t ourHostId = getHostId();
     uint32_t otherPid;
     uint32_t otherHostId;
     uint32_t prevOtherPid = 0;
 
     if((display == 0) || (swallowMutex == 0))
     {
	  return;
     }

     D("Attempting to take Swallow Mutex\n"); 
     /* Try up tp forty times ( 40 * 250ms = 10 seconds) to take 
       the semaphore... */

     countDown = 40;
     while(1)
     {

          /* While someone owns the semaphore */
          while(getSwallowMutexOwner(&otherHostId, &otherPid))
          {
               if( otherHostId == ourHostId)    
               {
                    if(otherPid == ourPid)     
                    {
 	                /* Great we have either successfully taken the semaphore
                           OR (unlikely) we previously had the semaphore! Exit
                           the function...*/
	                 swallowMutexTaken = 1;
                         D("Taken Swallow Mutex\n"); 
		         return;
                    }

     	    	    D("Semaphore currently taken by pid=%ld\n", (long) otherPid);
 		    /* Check that the process that has the semaphore exists...
        	     Bit of a hack, I cant find a function to directly check
                     if process exists. */
            	    if( (getsid(otherPid) < 0 ) && (errno == ESRCH))
	            {
	    	         D("Strange other Pid(%lu) cannot be found\n",(unsigned long) otherPid);
                         XDeleteProperty(display, wattr.root, swallowMutex);
                         break;    /* OK force early exit of inner while loop */
                    }
	       }

               /* Check if the owner of semaphore hasn't recently changed if it
                has restart timer */
               if(prevOtherPid != otherPid)
               {
   	            D("Looks like semaphore's owner has changed pid=%ld\n", 
                        (long) otherPid);
	            countDown = 40;
                    prevOtherPid = otherPid;
               }
            
               /* Do one step of the timer... */
	       countDown--;
               if(countDown <= 0)
               {
	    	    D("Waited long enough for Pid(%lu)\n", (unsigned long) otherPid);
                    XDeleteProperty(display, wattr.root, swallowMutex);
		    break;
               }
	
               usleep(250000);        /* 250ms */
 	  }

          /* else no one has semaphore, timeout, or owner is dead -
           Set us as the owner, but we need to check if we weren't 
           beaten to it so once more around the loop.
           Note even doing the recheck does not work in 100% of all
           cases due to task switching occuring at just the wrong moment
           see Mozdev bug 20088 - the fix is done use the stillHaveMutex
           function */

          setSwallowMutexOwner(ourHostId, ourPid);
     }
}

/*****************************************************************************/
/**
 * Check if we still have the mutex semaphore
 *
 * @return true if we still have the Mutex semaphore
 *
 *****************************************************************************/
static int stillHaveMutex(void)
{
     uint32_t otherPid;
     uint32_t otherHostId;

     if(getSwallowMutexOwner(&otherHostId, &otherPid))
     {
          const uint32_t ourHostId = getHostId();
          if( otherHostId == ourHostId)    
          {
               const uint32_t ourPid = (uint32_t)getpid();
               if(otherPid == ourPid)     
               {
                    return 1;
               }
          }
     }
     D("Race condition detected, semaphore pinched by Pid(%lu)\n", 
                                                      (unsigned long) otherPid);
     return 0;
}
 
/*****************************************************************************/
/**
 * Gives away ownership of the mutex semaphore
 *
 * @return none
 *
 *****************************************************************************/
static void giveSwallowMutex(void)
{
     if((display == 0) || (swallowMutex == 0) || (swallowMutexTaken ==0))
     {
	  return;
     }
     D("Giving Swallow Mutex\n"); 
     XDeleteProperty(display, wattr.root, swallowMutex);
     swallowMutexTaken = 0;
}
#endif

/*****************************************************************************/
/**
 * Mark the victim window with a property that indicates that this instance
 * of mozplugger-helper has made a claim to this victim. This is used to 
 * guard against two instances of mozplugger-helper claiming the same victim.
 *
 * @param[in] w The window
 *
 * @return True(1) if taken, False(0) if not
 *
 *****************************************************************************/
static int chkAndMarkVictimWindow(Window w)
{
     unsigned long temp[2];
     uint32_t ourPid;
     uint32_t ourHostId;

     uint32_t pid;
     uint32_t hostId;
     int gotIt = 0;

     /* See if some other instance of Mozplugger has already 'marked' this
      * window */
     if(getOwnerFromProperty(w, windowOwnerMark, &hostId, &pid))
     {
          return 0;          /* Looks like someone else has marked this window */
     }

     /* OK lets claim it for ourselves... */
     ourPid = (uint32_t)getpid();
     ourHostId = getHostId();

     temp[0] = ourHostId;
     temp[1] = ourPid;

     XChangeProperty(display, w, windowOwnerMark,
                     XA_INTEGER, 32, PropModeAppend,
                     (unsigned char*) (&temp), 2);

     /* See if we got it */
     if(getOwnerFromProperty(w, windowOwnerMark, &hostId, &pid))
     {
          if( (pid == ourPid) && (hostId == ourHostId) )
          {
               gotIt = 1;
          }
     }
     else         
     {
          D("Strange, couldn't set windowOwnerMark\n");
          gotIt = 1;
     }

     return gotIt;
}

/*****************************************************************************/
/**
 * Calculate scaling factor. Given a and b calculate a number that both a 
 * and b can be divided by
 *
 * @param[in] a  First value 
 * @param[in] b  Second value
 *
 * @return Scaling factor
 *
 *****************************************************************************/
static int gcd(int a, int b)
{
     if (a < b) 
     {
          return gcd(b,a);
     }
     if (b == 0) 
     {
          return a;
     }
     return gcd(b, a % b);
}

/*****************************************************************************/
/**
 * Set aspect ratio. Store away the aspect ratio
 *
 * @param[in] x  X value 
 * @param[in] y  Y value
 *
 * @return 1(true) if change has occurred
 *
 *****************************************************************************/
static int set_aspect(int x, int y)
{
     const int ox = xaspect;
     const int oy = yaspect;
     const int d = gcd(x, y);
     xaspect = x / d;
     yaspect = y / d;
     D("xaspect=%d yaspect=%d\n", xaspect, yaspect);
     return ((ox != xaspect) || (oy != yaspect));
}

/*****************************************************************************/
/**
 * Adjust window size
 *
 * @return none
 *
 *****************************************************************************/
static void adjust_window_size(void)
{
     int x = 0;
     int y = 0;
     int w = parentDetails.width;
     int h = parentDetails.height;


     if (flags & H_FILL)
     {
	  D("Resizing window 0x%x with FILL\n", (unsigned) victimDetails.window);
     }
     else if (flags & H_MAXASPECT)
     {
	  if (xaspect && yaspect)
	  {
               int tmpw, tmph;

	       D("Resizing window 0x%x with MAXASPECT\n", 
                                               (unsigned) victimDetails.window);
	       tmph = h / yaspect;
	       tmpw = w / xaspect;
	       if (tmpw < tmph) 
               {
                    tmph = tmpw;
               }
	       tmpw = tmph * xaspect;
	       tmph = tmph * yaspect;
	       
	       x = (w - tmpw) / 2;
	       y = (h - tmph) / 2;
	       
	       w = tmpw;
	       h = tmph;
	  }
	  else
	  {
	       D("Not resizing window\n");
	       return;
	  }
     }
  
     /* Compensate for the Victims border width (usually set to zero anyway) */
     w -= 2 * victimDetails.borderWidth;
     h -= 2 * victimDetails.borderWidth;

     D("New size: %dx%d+%d+%d\n", w, h, x, y);

     if((victimDetails.x == x) && (victimDetails.y == y)
        && (victimDetails.width == w) && (victimDetails.height == h))
     {
          XEvent event;
          D("No change in window size so sending ConfigureNotify instead\n");
          /* According to X11 ICCCM specification, a compliant  window 
           * manager, (or proxy in this case) should sent a  ConfigureNotify
           * event to the client even if no size change has occurred!
           * The code below is taken and adapted from the
           * TWM window manager and fixed movdev bug #18298. */
          event.type = ConfigureNotify;
          event.xconfigure.display = display;
          event.xconfigure.event = victimDetails.window;
          event.xconfigure.window = victimDetails.window;
          event.xconfigure.x = x;
          event.xconfigure.y = y;
          event.xconfigure.width = w;
          event.xconfigure.height = h;
          event.xconfigure.border_width = victimDetails.borderWidth;
          event.xconfigure.above = Above;
          event.xconfigure.override_redirect = False;

          XSendEvent(display, victimDetails.window, False, StructureNotifyMask,
                  &event);
     }

     /* Always resize the window, even when the target size has not changed.
	This is needed for gv. */
     XMoveResizeWindow(display, victimDetails.window, 
		       x, y, (unsigned)w, (unsigned)h);
     victimDetails.x = x;
     victimDetails.y = y;
     victimDetails.width = w;
     victimDetails.height = h;
}

/*****************************************************************************/
/**
 * Change the leader of the window. Peter - not sure why this code is needed
 * as it does say in the ICCCM that only client applications should change
 * client HINTS. Also all the window_group does is hint to the window manager
 * that there is a group of windows that should be handled together.
 *
 * @return none
 *
 *****************************************************************************/
static void change_leader(void)
{
     D("Changing leader of window 0x%x\n", (unsigned) victimDetails.window);

     XWMHints *leader_change;
     if ((leader_change = XGetWMHints(display, victimDetails.window)))
     {
          if((leader_change->flags & WindowGroupHint) != 0)
          {
		D("Old window leader was 0x%x\n", 
                                        (unsigned) leader_change->window_group);
          }
          if((leader_change->flags & InputHint) != 0)
          {
		D("Input hint = %i\n", leader_change->input);
          }
          if((leader_change->flags & StateHint) != 0)
          {
		D("InitialState hint = %i\n", 
                                       (unsigned) leader_change->initial_state);
          }

	  leader_change->flags |= WindowGroupHint;
	  leader_change->window_group = wattr.root;

	  D("New window leader is 0x%x\n",
                                        (unsigned) leader_change->window_group);
	  XSetWMHints(display,victimDetails.window,leader_change);
	  XFree(leader_change);
     }
     else
     {
	  D("XGetWMHints returned NULL\n");
     }
}

/*****************************************************************************/
/**
 * Reparent the window, a count is kept of the number of attempts. If this
 * exceeds 10, give up (i.e. avoid two instances of helper fighting over
 * the same application). Originally this was done with a semaphore, but this
 * caused more problems than it fixed. So alternative is to assume that it is
 * such an unlikely occurance that two instances of helper will run at exactly
 * the same time and try to get the same window, that we give up.
 *
 * @return none
 * 
 *****************************************************************************/
static void reparent_window(void)
{
     if (!victimDetails.window) 
     {
          D("reparent_window: No victim to reparent\n");
          return;
     }
     if (!parentDetails.window) 
     {
          D("reparent_window: No parent to reparent to\n");
          return;
     }

     if(victimDetails.reparentedAttemptCount > 10)
     {
          D("Giving up reparenting to avoid - tried 10 times\n");
          return;
     }

     victimDetails.reparentedAttemptCount++;

     
     D("Reparenting window 0x%x into 0x%x\n", (unsigned)victimDetails.window, 
                                                (unsigned)parentDetails.window);

     XReparentWindow(display, victimDetails.window, parentDetails.window, 0, 0);
}

/******************************************************************************/
/**
 * Traditionally strcmp returns -1, 0 and +1 depending if name comes before
 * or after windowname, this function also returns +1 if error
 *
 * @param[in] windowname The window name to compare against
 * @param[in] name       The name to compare against window name
 *
 * @return 0 if match, -1/+1 if different 
 *
 *****************************************************************************/
static int my_strcmp(const char *windowname, const char *name)
{
     if (!name) 
     {
          return 1;
     }
     if (!windowname) 
     {
          return 1;
     }

     switch (name[0])
     {
     case '=':
	  return strcmp(name+1, windowname);
     case '~':
	  return strcasecmp(name+1, windowname);
     case '*':
	  return strncasecmp(name+1, windowname, strlen(name)-1);
     default:
          /* Return 0 for success so need to invert logic as strstr
             returns pointer to the match or NULL if no match */
	  return !strstr(windowname, name);
     }
}

/******************************************************************************/
/**
 * Check name against the name of the passed window
 *
 * @param[in] w      The window to compare against
 * @param[in] name   The name to compare against window name
 *
 * @return 1 if match, 0 if not 
 *
 *****************************************************************************/
static char check_window_name(Window w, const char *name)
{
     char * windowname;
     XClassHint windowclass;

     if (XFetchName(display, w, &windowname))
     {
	  const char match = (my_strcmp(windowname, name) == 0);

          D("XFetchName, checking window NAME 0x%x (%s %s %s)\n",
                            (unsigned)w, windowname, match ? "==" : "!=", name);
	  XFree(windowname);
	  if (match) 
          {
	       return 1;
	  }
     }
     else
     {
          D("XFetchName, window has no NAME\n");
     }
     
     if (XGetClassHint(display, w, &windowclass))
     {
	  const char match = (my_strcmp(windowclass.res_name, name) == 0);

          D("XGetClassHint, checking window CLASS 0x%x (%s %s %s)\n",
                  (unsigned)w, windowclass.res_name, match ? "==" : "!=", name);
	  XFree(windowclass.res_class);
	  XFree(windowclass.res_name);

          return match;
     }
     else
     {
          D("XGetClassHint, window has no CLASS\n");
     }
     return 0;
}

/*****************************************************************************/
/**
 * Setup display ready for any reparenting of the application window. If the
 * WINDOW is NULL (can happen see mozdev bug #18837), then return failed
 * code from this function to indicate no window available. This then means
 * no swallowing will occur (even if requested).
 *
 * @return 1(true) if success, 0(false) if not
 *
 *****************************************************************************/
static int setup_display(void)
{
     char *displayname;

     if(parentDetails.window == 0)       /* mozdev bug #18837 */
     {
          D("setup_display() WINDOW is Null - so nothing setup\n");
          return 0;
     }

     displayname = getenv("DISPLAY");
     D("setup_display(%s)\n", displayname);

     XSetErrorHandler(error_handler);

     display = XOpenDisplay(displayname);
     if(display == 0)
     {
          D("setup_display() failed cannot open display!!\n");
	  return 0;
     }

     if (!XGetWindowAttributes(display, parentDetails.window, &wattr))
     {
          D("setup_display() failed cannot get window attributes!!\n");
          XCloseDisplay(display);
          display = 0;
          return 0;
     }
     D("display=0x%x\n", (unsigned) display);
     D("WINDOW =0x%x\n", (unsigned) parentDetails.window);
     D("rootwin=0x%x\n", (unsigned) wattr.root);
     
     D("setup_display() done\n");

     return 1;
}

/*****************************************************************************/
/**
 * Add to list of possible victim windows. Called whenever we get a
 * CREATE_NOTIFY on the root window.
 *
 * @param[in] window The window
 *
 * @return none
 *
 *****************************************************************************/
static void add_possible_victim(Window window)
{
     possible_victim_windows[possible_victim_count] = window;
     if(possible_victim_count < MAX_POSS_VICTIMS)
     {
          possible_victim_count++;
     }
}

/*****************************************************************************/
/**
 * Checks if the passed window is the victim.
 *
 * @param[in] window The window
 *
 * @return True if found our victim for first time.
 *
 *****************************************************************************/
static int find_victim(Window window)
{
     if (!victimDetails.window)
     {
          Window found = 0;

	  D("Looking for victim... (%s)\n", winname);

          /* New way, check through list of newly created windows */
          int i; 
	  for(i = 0; i < possible_victim_count; i++)
          {
               if(window == possible_victim_windows[i])
	       {
                    if (check_window_name(window, winname))
                    {
                         found = true;
                         break;
                    }
               }
          }

	  if (found)
	  {
	       XWindowAttributes ca;
	       if(XGetWindowAttributes(display, window, &ca))
               {
                    /* See if some instance of mozplugger got the window 
                     * before us, if not mark it as ours */
                    if(chkAndMarkVictimWindow(window))
                    {
                         victimDetails.window = window;
                         victimDetails.borderWidth = ca.border_width;
                         victimDetails.x = ca.x;
                         victimDetails.y = ca.y;
                         victimDetails.width = ca.width;
                         victimDetails.height = ca.height;

      	                 D("Found victim=0x%x, x=%i, y=%i, width=%i, height=%i, "
                                                                  "border=%i\n",
                                          (unsigned) victimDetails.window, 
                                                     victimDetails.x,
                                                     victimDetails.y,
                                                     victimDetails.width, 
                                                     victimDetails.height,
                                                     victimDetails.borderWidth);

                         /* To avoid losing events, enable monitoring events on 
                          * victim at earlist opportunity */

                         XSelectInput(display, victimDetails.window, 
                                                           StructureNotifyMask);
                         XSync(display, False);

                         XSelectInput(display, wattr.root, 0);

#ifdef USE_MUTEX_LOCK
	                 giveSwallowMutex();
#endif 	
          	         if (flags & H_MAXASPECT)
	                 {
	        	      set_aspect(ca.width, ca.height);
	                 }
                         return true;
                    }
                    else
                    {
                         D("Window 0x%x already claimed by another"
                                " instance of mozplugger\n", (unsigned) window);
                    }
               }
               else
               {    
                    D("XGetWindowAttributes failed for 0x%x\n", 
                                                             (unsigned) window);
               }
	  }
     }
     return false;
}

/******************************************************************************/
/**
 * handle X event for the root window. Mozplugger in effect is acting like a
 * window manager by intercepting root window events. The one event it would
 * like to intercept, but cannot is the MapRequest. It cannot because only one
 * client is allowed to intercept that event (i.e. the real window manager). When
 * the real window manager receives the MapRequest, it will most likely try and
 * reparent that window into a frame containing decorations (i.e. title bar,
 * etc). The only way mozplugger knows this has happened is when it sees the
 * ReparentNotify. Ideally Mozplugger needs to over-ride this reparenting, the
 * current solution is to reparent that window again. Result is a fight with
 * window manager. The alternative is to set the override_redirect attribute
 * on the window (this stops the MapRequest event). But according to ICCCM this
 * is bad practice, so mozplugger only does it as a last resort (see 
 * reparent_window function above).
 *
 * An alternative would be to reparent the window before the MapRequest i.e.
 * when the window is invisible. Unfortunately some apps create many invisible
 * root windows and it is difficult to know which window is the one to grab until
 * the application makes it clear by Mapping that window.
 *
 * @param[in] ev the event
 *
 * @return none
 *
 *****************************************************************************/
static void handle_rootWindow_event(const XEvent * const ev)
{
     switch (ev->type)
     {
     case CreateNotify:
          D("***CreateNotify for root, window=0x%x, "
                                            "override_redirect=%i\n",  
                                            (unsigned) ev->xcreatewindow.window, 
                                           ev->xcreatewindow.override_redirect);
          /* All toplevel new app windows will be created on desktop (root),
           * so lets keep a list as they are created */
          add_possible_victim(ev->xcreatewindow.window);
	  break;

     case MapNotify:
          D("***MapNotify for root, window=0x%x, override_redirect=%i\n",
                                                    (unsigned) ev->xmap.window,
                                                    ev->xmap.override_redirect);
          /* A window has been mapped, if window manager is allowed to
           * fiddle, lets see if this is the window */
          if(!ev->xmap.override_redirect)
          {
               Window wnd = ev->xmap.window;
               if(find_victim(wnd))
               {
                    /* If we get MapNotify on root before ReparentNotify for our
                     * victim this means either:-
                     * (1) There is no reparenting window manager running
                     * (2) We are running over NX 
                     * With NX, we get the MapNotify way too early! (window not
                     * mapped yet on the server), so need to be clever?
                     * Set flag, this will be used later in the select loop */
                    D("Looks like no reparenting WM running\n");
		    change_leader();
                    victimDetails.noWmRunning = true;
		    reparent_window();
               }
          }
	  break;


     case ReparentNotify:
          D("***ReparentNotify for root, parent=0x%x, window=0x%x, "
                                           "x=%i, y=%i, override_redirect=%i\n",
                                               (unsigned) ev->xreparent.parent,
                                               (unsigned) ev->xreparent.window,
                                               ev->xreparent.x, ev->xreparent.y,
					       ev->xreparent.override_redirect);

          /* A window has been reparented, if window manager is allowed to
           * fiddle, lets see if this is the window  we are looking for */
          if(!ev->xreparent.override_redirect)
          {
               Window wnd = ev->xreparent.window;

               if(find_victim(wnd))
               {
                    /* Avoid the fight with window manager to get reparent,
                     * instead be kind and withdraw window and wait for WM
                     * to reparent window back to root */
                   D("Withdraw window 0x%x\n", (unsigned) wnd);

                   XWithdrawWindow(display, wnd, DefaultScreen(display));
#if 0
		   change_leader();
                   reparent_window();
#endif
               }
          }
	  break;


     case ConfigureNotify:
          D("***ConfigureNotify for root, window=0x%x, "
                      "x=%i, y=%i, width=%i, height=%i, override_redirect=%i\n",
                                              (unsigned) ev->xconfigure.window,
                                              ev->xconfigure.x, ev->xconfigure.y,
                                              ev->xconfigure.width,
                                              ev->xconfigure.height,
					      ev->xconfigure.override_redirect);	
          break;



     case UnmapNotify:
	  D("***UnmapNotify for root, window=0x%x, from_configure=%i, "
                                "send_event=%i\n", (unsigned) ev->xunmap.window,
                                                      ev->xunmap.from_configure,
                                                         ev->xunmap.send_event);
          break;


     case DestroyNotify:
          D("***DestroyNotify for root, window=0x%x\n", 
                                          (unsigned) ev->xdestroywindow.window);
          break;

     case ClientMessage:
          D("***ClientMessage for root\n");
          break;

     default: 
          D("!!Got unhandled event for root->%d\n", ev->type);
          break;
     }
}

/******************************************************************************/
/**
 * handle X event for the victim window
 *
 * @param[in] ev the event
 *
 * @return none
 *
 *****************************************************************************/
static void handle_victimWindow_event(const XEvent * const ev)
{
     switch (ev->type)
     {
     case UnmapNotify:
	  D("UNMAPNOTIFY for victim, window=0x%x, from_configure=%i, "
                                "send_event=%i\n", (unsigned) ev->xunmap.window,
                                                     ev->xunmap.from_configure,
                                                     ev->xunmap.send_event);
	  victimDetails.mapped = false;
          break;
		    
     case MapNotify:
          D("MAPNOTIFY for victim, window=0x%x, override_redirect=%i\n",
                                                    (unsigned) ev->xmap.window,
                                                    ev->xmap.override_redirect);
          victimDetails.mapped = true;
	  if(victimDetails.reparented)
          {
	       adjust_window_size();
          }
          break;
	    
     case ReparentNotify:
          if (ev->xreparent.parent == parentDetails.window)
          {
 	       D("REPARENT NOTIFY on victim to the right window, "
		                           "parent=0x%x, window=0x%x, "
                                           "x=%i, y=%i, override_redirect=%i\n",
                                                (unsigned) ev->xreparent.parent,
                                                (unsigned) ev->xreparent.window,
                                               ev->xreparent.x, ev->xreparent.y,
					       ev->xreparent.override_redirect);
	       victimDetails.reparented = true;
               if(!victimDetails.mapped)
               {
	            D("XMapWindow(0x%x)\n", (unsigned) victimDetails.window);
                    XMapWindow(display, victimDetails.window);
               }
               else
               {
	            adjust_window_size();
               }
	  } 
          else 
          {
	       D("REPARENT NOTIFY on victim to some other window! "
		                           "parent=0x%x, window=0x%x, "
                                           "x=%i, y=%i, override_redirect=%i\n",
                                               (unsigned) ev->xreparent.parent,
                                               (unsigned) ev->xreparent.window,
                                               ev->xreparent.x, ev->xreparent.y,
  					       ev->xreparent.override_redirect);
               victimDetails.noWmRunning = false;
	       victimDetails.reparented = false;
               reparent_window();
	  }
	  break;

     case ConfigureNotify:
          D("CONFIGURE NOTIFY for victim, window=0x%x," 
                                    " x=%d, y=%d, w=%d, h=%d, "
                                    "border_width=%d, override_redirect=%i\n",
                                              (unsigned) ev->xconfigure.window,
                                             ev->xconfigure.x, ev->xconfigure.y, 
                                              ev->xconfigure.width, 
                                              ev->xconfigure.height, 
                          		      ev->xconfigure.border_width,
					      ev->xconfigure.override_redirect);	
          break;

     case ClientMessage:
          D("CLIENT MESSAGE for victim\n");
                    /* I see this with evince! perhaps it needs to be handled? */
          break;

     case DestroyNotify:
          D("DESTROY NOTIFY for victim, window=0x%x\n", 
                                          (unsigned) ev->xdestroywindow.window);
          if(ev->xdestroywindow.window == victimDetails.window)
          {
               XSelectInput(display, victimDetails.window, 0);
               victimDetails.window = 0;
          }
          break;

     default: 
          D("!!Got unhandled event for victim->%d\n", ev->type);
          break;
     }
}

/******************************************************************************/
/**
 * handle X event for the parent window
 *
 * @param[in] ev the event
 *
 * @return none
 *
 *****************************************************************************/
static void handle_parentWindow_event(const XEvent * const ev)
{
     unsigned long mask;
     switch (ev->type)
     {
     case ConfigureRequest:
          mask = ev->xconfigurerequest.value_mask;
          D("ConfigureRequest to WINDOW mask=0x%lx\n", mask);
          if(ev->xconfigurerequest.window != victimDetails.window)
          {
               D("- Strange Configure Request not for victim\n");
          }
          else
          {
               int adjustWindow = false;

               if(mask & (CWHeight | CWWidth))
               {
                    D(" - request to set width & height\n");
	            set_aspect(ev->xconfigurerequest.width, 
                                                  ev->xconfigurerequest.height);
                    adjustWindow = true;
               }
               if(mask & (CWBorderWidth))
               {
                    const int w = ev->xconfigurerequest.border_width; 
                    D("- request to set border width=%d\n", w);
                    if(victimDetails.borderWidth != w)
                    {
                         victimDetails.borderWidth = w;
                         adjustWindow = true;
                    }
                    XSetWindowBorderWidth(display, victimDetails.window, 
                                                                  (unsigned) w);
               }
               /* Only adjust if window has been mapped and reparented */
               if(adjustWindow && victimDetails.mapped 
                                                    && victimDetails.reparented)
               {
                    adjust_window_size();
               }
          }
	  break;

     default: 
          D("!!Got unhandled event for PARENT->%d\n", ev->type);
          break;
     }
}
	
/******************************************************************************/
/**
 * Check events from X
 * Read in events from X and process, keep reading in events until no more
 * and then exit. It is important that all events have been processed and
 * not left pending.
 *
 * @return none
 *
 *****************************************************************************/
static void check_x_events(void)
{
     int numEvents = XPending(display);

     /* While some events pending... Get and action */
     while (numEvents > 0)
     {
          XEvent ev;

          XNextEvent(display, &ev);
         
	  if (ev.xany.window == wattr.root)
	  {
               handle_rootWindow_event(&ev);
	  }
	  else if (victimDetails.window 
                               && ev.xany.window == victimDetails.window)
	  {
               handle_victimWindow_event(&ev);
	  }
          else if (parentDetails.window 
                                    && (ev.xany.window == parentDetails.window))
	  {
               handle_parentWindow_event(&ev);
	  }
          else
          {
	       D("!!Got unhandled event for unknown->%d\n", ev.type);
          }

          /* If this is the last of this batch, check that more havent
           * been added in the meantime as a result of an action */
          numEvents--;
          if(numEvents == 0)
          {
               numEvents = XPending(display);
          }
     }
}

/******************************************************************************/
/**
 * Check events from pipe connected to mozplugger
 * Read in events from pipe connected to mozplugger and process
 *
 * @return none
 *
 *****************************************************************************/
static void check_pipe_fd_events(void)
{
     static NPWindow wintmp;
     int n;
  
     Window oldwindow = parentDetails.window;
     D("Got pipe_fd data, old parent=0x%x pipe_fd=%d\n", 
                                                 (unsigned) oldwindow, pipe_fd);
  
     n = read(pipe_fd, ((char *)& wintmp),  sizeof(wintmp));
     if (n < 0)
     {
	  if (errno == EINTR) 
          {
               return;
          }
	  D("Winddata read error, exiting\n");
#ifdef USE_MUTEX_LOCK
          giveSwallowMutex();
#endif
	  exit(EX_UNAVAILABLE);
     }
  
     if (n == 0)
     {
	  D("Winddata EOF, exiting\n");
#ifdef USE_MUTEX_LOCK
          giveSwallowMutex();
#endif
	  exit(EX_UNAVAILABLE);
     }
  
     if (n != sizeof(wintmp))
     {
	  return;
     }
  
     parentDetails.window = (Window) wintmp.window;
     parentDetails.width = (int) wintmp.width;
     parentDetails.height = (int) wintmp.height;

     D("Got pipe_fd data, new parent=0x%x\n", (unsigned) parentDetails.window);
  
     if (parentDetails.window && display )
     {
          if(parentDetails.window != oldwindow)
          {
               victimDetails.reparented = false;

               /* To avoid losing events, enable monitoring events on new parent
                * before disabling monitoring events on the old parent */

               XSelectInput(display, parentDetails.window, 
                                                      SubstructureRedirectMask);
               XSync(display, False);
               XSelectInput(display, oldwindow, 0);
        
               if(victimDetails.window)
               {
                    reparent_window();
               }
               else
               {
                    D("Victim window not ready to be reparented\n");
               }
          }
 
          if(victimDetails.window && victimDetails.mapped 
                                                    && victimDetails.reparented)
          {
               /* The window has been resized. */
               adjust_window_size();
          }
          else
          {
               D("Victim window not ready to be adjusted\n");
          }
     }
}

/******************************************************************************/
/**
 * Keep checking all events until application dies
 * Use select to check if any events need processing
 *
 * @return none
 *
 *****************************************************************************/
static void check_all_events(pid_t pid)
{
     int maxfd;
     fd_set fds;
     int status;

     while(1)
     {
	  int selectRetVal;
          int sig_chld_fd;

	  struct timeval   timeout;
          struct timeval * pTimeout = 0;
          if (display)
          {
	       check_x_events();
          }

          FD_ZERO(&fds);
          FD_SET(ConnectionNumber(display), &fds);
          FD_SET(pipe_fd, &fds);

          maxfd = MAX(ConnectionNumber(display), pipe_fd);

          sig_chld_fd = get_SIGCHLD_fd();
          if(sig_chld_fd >= 0)
          {
               FD_SET(sig_chld_fd, &fds);
               maxfd = MAX(maxfd, sig_chld_fd);
          }

          /* If NX is is use and nxagent is configured as rootless, it can
           * appear that there is no WM running, but actually there is and its
           * actions (reparenting to add decorations) is invisible. In this
           * case just reparent 4 times in a row with 1 second gaps. If the
           * link latency > 4 seconds this may fail? */ 
          if((victimDetails.noWmRunning)
                             && (victimDetails.reparentedAttemptCount < 4))
          {
               pTimeout = &timeout;
               pTimeout->tv_usec = 50000;
               pTimeout->tv_sec = 0;
          }

          D("SELECT IN %s\n", pTimeout ? "with timeout" : "");

          selectRetVal = select(maxfd + 1, &fds, NULL, NULL, pTimeout);
          if(selectRetVal > 0)
          {
	       if (FD_ISSET(pipe_fd, &fds))
               {
	            check_pipe_fd_events();
               }
               if( FD_ISSET(sig_chld_fd, &fds))
               {
                    handle_SIGCHLD_event();
               }  
          }
	  else if((selectRetVal == 0) && pTimeout)
          {
               D("Select timeout and suspect invisible WM\n");
               reparent_window();
          }
          else
          {
               D("Select exited unexpected, errno = %i\n", errno);
          }
          D("SELECT OUT\n");

          if(pid >= 0)
          {
	       if(waitpid(pid, &status, WNOHANG))
               {
                    pid = -1;     
                    if((flags & H_DAEMON) == 0)
                    {
                         D("Child process has died status=%i\n", status);
                         return;
                    }
                    else
                    {
                        D("Child process has detached, keep going\n");
                    }
               }
          }
     }
}

/******************************************************************************/
/**
 * Handle the application
 * Wait for application to finish and exit helper if a problem
 *
 * @param[in] pid process ID of the application
 *
 * @return none
 *
 *****************************************************************************/
static void handle_app(pid_t pid)
{
     int status;

     victimDetails.pid = pid;

     if (flags & H_SWALLOW)
     {
          /* Whilst waiting for the Application to complete, check X events */
	  check_all_events(pid);

#ifdef USE_MUTEX_LOCK
          /* Make sure the semaphore has been released */
          giveSwallowMutex();
#endif
     }
     else if(flags & H_DAEMON)
     {
          victimDetails.pid = -1;
          /* If Daemon, then it is not supposed to exit, so we exit instead */
          exit(0);
     }
     else
     {
          /* Just wait for the Application to complete dont check X events */
	  waitpid(pid, &status, 0);
     }

     victimDetails.pid = -1;

     /* If Application completed is a bad way, then lets give up now */
     if (!WIFEXITED(status))
     {
	  D("Process dumped core or something...\n");
	  exit(EX_UNAVAILABLE);
     }

     if (WEXITSTATUS(status) && !(flags & H_IGNORE_ERRORS))
     {
	  D("Process exited with error code: %d\n", WEXITSTATUS(status));
	  exit(WEXITSTATUS(status));
     }

     D("Exited OK!\n");
}

/******************************************************************************/
/**
 * Exit early due to problem. Prints a message to stderr and then exits
 * the application
 *
 * @return none
 *
 *****************************************************************************/
static void exitEarly(void)
{
     fprintf(stderr,"MozPlugger version " VERSION " helper application.\n"
		  "Please see 'man mozplugger' for details.\n");
     exit(1);
}

/******************************************************************************/
/**
 * Expands the winname if it contains %f or %p by replacing %f by the file
 * name part of the URL or %p by the whole of thr URL.
 *
 * @return none
 *
 *****************************************************************************/
static void expand_winname(char * buffer, int bufferLen, const char * winame, const char * file)
{
     const char * p = winame;
     char * q = buffer;
     char * end = &buffer[bufferLen-1];

     D("winame before expansion is '%s'\n", winname);

     while((*p != '\0') && (q < end))
     {
          if( *p != '%')
          {
               *q++ = *p++;
          }
          else
          {
               const char * r;
               switch(p[1])
               {
                    case '%' :
                         *q++ = '%';
                         p += 2;
                         break;

                    case 'f' :       /* %f = insert just the filename no path */
                         r = strrchr(file, '/');   /* Find the filename start */
                         if(r == 0)
                         {
                              r = file;
                         }
                         else
                         {
                              r++;       /* Skip the slash */
                         }
                         while((*r != '\0') && (q < end))
                         {
                              *q++ = *r++;
                         }
                         p += 2;       /* Skip the %f */ 
                         break;

                    case 'p' :
                         r = file;
                         while((*r != '\0') && (q < end))
                         {
                              *q++ = *r++;
                         }
                         p += 2;
                         break;

                    default :
                         *q++ = *p++;
                         break;
               }
          }
     }
     *q = '\0';

     D("winame after expansion is '%s'\n", buffer);
}

/******************************************************************************/
/**
 * Handle the SIGTERM signal. Terminate the helper and child.
 *
 * @return none
 *
 *****************************************************************************/
static void sigTerm()
{
     D("SIGTERM received\n");
#ifdef USE_MUTEX_LOCK
     giveSwallowMutex();
#endif 	
     if(display)
     {
          XCloseDisplay(display);
          display = 0;
     }

     if(victimDetails.pid >= 0)
     {
          my_kill(-victimDetails.pid);
          victimDetails.pid = -1;
     }

    _exit(0);
}

/******************************************************************************
 **
 * main() -  normally called from the child process started by mozplugger.so 
 *
 * @param[in] argc The number of arguments
 * @param[in] argv List of arguments
 *
 * @return Never returns (unless app exits)
 * 
 *****************************************************************************/
int main(int argc, char **argv)
{
     char buffer[100];

     unsigned long temp = 0;
     int x, y, i;
     D("Helper started.....\n");

     if (argc < 3)
     {
          exitEarly();
     }

     memset(&victimDetails, 0, sizeof(victimDetails));
     victimDetails.pid = -1;
     memset(&parentDetails, 0, sizeof(parentDetails));

     i = sscanf(argv[1],"%d,%d,%d,%lu,%d,%d,%d,%d",
	    &flags,
	    &repeats,
	    &pipe_fd,
	    &temp,
	    &x,       /* Not needed and not used */
	    &y,       /* Not needed and not used */
	    &parentDetails.width,
	    &parentDetails.height);

     if(i < 8)
     {
          exitEarly();
     }

     parentDetails.window = (Window)temp;

     command = argv[2];
     winname = getenv("winname");
     file = getenv("file");

     if(winname)
     {
          expand_winname(buffer, sizeof(buffer), winname, file);
          winname = buffer;
     }
     D("HELPER: %s %s %s %s\n",
       argv[0],
       argv[1],
       file,
       command);

     redirect_SIGCHLD_to_fd();

     /* Create handler for when terminating the helper */

     signal(SIGTERM, sigTerm);

     if (!setup_display())
     {
         if( (flags & H_SWALLOW) != 0)
         { 
	      D("Failed to open X display - canceling swallow functionality\n");
	      flags &=~ H_SWALLOW;
         }
     }

     if (repeats < 1) 
     {
          repeats = 1;
     }

     while (repeats > 0)
     {
	  int loops = 1;
	  pid_t pid;
          int maxFd;

	  /* This application will use the $repeat variable */
	  if (flags & H_REPEATCOUNT)
          {
	       loops = repeats;
          }

	  /* Expecting the application to loop */
	  if (flags & H_LOOP)
          {
	       loops = MAXINT;
          }

	  if (flags & H_SWALLOW)
	  {
               initWindowOwnerMarker();

#ifdef USE_MUTEX_LOCK
	       /* If we are swallowing a victim window we need to guard
		  against more than one instance of helper running in
		  parallel, this is done using a global mutex semaphore.
                  Although its not successful in all cases! */
	       initSwallowMutex();


               /* There is a race condition when taking the semaphore that 
                * means occasionally we think we have it but we dont
                * This do-while loop checks for that case - this
                * is better than putting a wait in the take semaphore
                * rechecking loop fixes Mozdev bug 20088 */
               do
               {
	           takeSwallowMutex(); 
               }
               while(!stillHaveMutex());
#endif
               XSelectInput(display, parentDetails.window, 
                                                      SubstructureRedirectMask);
	       XSelectInput(display, wattr.root, SubstructureNotifyMask);
	       XSync(display, False);
	  }

          maxFd = pipe_fd;
          if(display && (ConnectionNumber(display) > maxFd))
          {
               maxFd = ConnectionNumber(display);
          }

          pid = spawn_app(command, flags, maxFd);
	  if(pid == -1)
          {
#ifdef USE_MUTEX_LOCK
               giveSwallowMutex();  
#endif
	       exit(EX_UNAVAILABLE);
          }
     
	  D("Waiting for pid=%d\n", pid);

	  handle_app(pid);

	  D("Wait done (repeats=%d, loops=%d)\n", repeats, loops);
	  if (repeats < MAXINT)
          {
	       repeats -= loops;
	  }
    }

    if(display)
    {
          XCloseDisplay(display);
          display = 0;
     }

     exit(0);
}
