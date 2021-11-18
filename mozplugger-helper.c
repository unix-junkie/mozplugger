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

#define _GNU_SOURCE /* for getsid() */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sysexits.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <X11/X.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdbool.h>

#include "npapi.h"
#include "cmd_flags.h"
#include "child.h"
#include "debug.h"
#include "pipe_msg.h"

/**
 * Control use of semaphore in mozplugger-helper, define if one wants
 * semaphores (rare cases doesnt work)
 */

#define USE_MUTEX_LOCK

struct VictimDetails_s
{
     int noWmRunning;
     int mapped;
     int reparented;
     int reparentedAttemptCount;
     Window window;
     int borderWidth;
     int x;
     int y;
     int idealWidth; /* The width the victim wants to be if unconstrained */
     int idealHeight; /* The height the victim wants to be if unconstrained */
     int currentWidth;
     int currentHeight;
};

typedef struct VictimDetails_s VictimDetails_t;

struct ParentDetails_s
{
     Window window;
     int width;
     int height;
};

typedef struct ParentDetails_s ParentDetails_t;

struct SwallowMutex_s
{
    Atom xObj;
    int taken;
    Window win;
    Display * dpy;
};

typedef struct SwallowMutex_s SwallowMutex_t;

struct SigGlobals_s
{
    Display * dpy;
#ifdef USE_MUTEX_LOCK
    SwallowMutex_t * mutex;
#endif
    pid_t childPid;
};

typedef struct SigGlobals_s SigGlobals_t;

/**
 * Global variables
 */

static SigGlobals_t sig_globals;
static VictimDetails_t victimDetails;
static ParentDetails_t parentDetails;
static int pipe_fd;
static int flags;
static char * winname;
static char * file;

static XWindowAttributes wattr;

#define MAX_POSS_VICTIMS 100
static unsigned int possible_victim_count = 0;
static Window possible_victim_windows[MAX_POSS_VICTIMS];


/**
 * Callback from X when there is an error. If debug defined error message is
 * printed to debug log, otherise nothing is done
 *
 * @param[in] dpy The display pointer
 * @param[in] err Pointer to the error event data
 *
 * @return Always returns zero
 */
static int error_handler(Display *dpy, XErrorEvent *err)
{
     char buffer[1024];
     XGetErrorText(dpy, err->error_code, buffer, sizeof(buffer));
     D("!!!ERROR_HANDLER!!!: %s\n", buffer);
     return 0;
}

#ifdef USE_MUTEX_LOCK
/**
 * Initialise semaphore (swallow mutex semaphore protection)
 *
 * @param[in] dpy The display
 * @param[in] win The window
 * @param[out] mutex The Mutex initialised
 *
 * @return none
 */
static void initSwallowMutex(Display * dpy, Window win, SwallowMutex_t * mutex)
{
     D("Initialising Swallow Mutex\n");
     if(dpy == 0)
     {
	  fprintf(stderr, "Display not set so cannot initialise semaphore!\n");
          mutex->dpy = NULL;
     }
     else
     {
          mutex->dpy = dpy;
          mutex->win = win;
          mutex->xObj = XInternAtom(dpy, "MOZPLUGGER_SWALLOW_MUTEX", 0);
          mutex->taken = 0;
     }
}
#endif

/**
 * Initialise the X atom used to mark windows as owned by mozplugger
 *
 * @param[in] dpy The display pointer
 *
 * @return None
 */
static Atom getWindowOwnerMarker(Display * dpy)
{
     static Atom mark = 0;
     if(mark == 0)
     {
          mark = XInternAtom(dpy, "MOZPLUGGER_OWNER", 0);
     }
     return mark;
}

/**
 * Get Host ID - construct a host ID using host name as source of information
 *
 * @return The host ID
 */
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

/**
 * Extract host Id and pid from window property. This is common code used in
 * various places and is factored out due to complexity of 64 bit versus
 * 32 bit machines.
 *
 * @param[in] dpy The display pointer
 * @param[in] w The window to get property from
 * @param[in] name The name of property to get
 * @param[out] hostId The ID of the host currently owning the mutex
 * @param[out] pid The process ID of the mutex
 *
 * @return 1(true) if owner found, 0 otherwise
 */
static int getOwnerFromProperty(Display * dpy, Window w, Atom name, uint32_t * hostId, uint32_t * pid)
{
     unsigned long nitems;
     unsigned long bytes;
     int fmt;
     Atom type;
     unsigned char * property = NULL;
     int success = 0;

     /* Get hold of the Host & PID that current holds the semaphore for
       this display! - watch out for the bizarre 64-bit platform behaviour
       where property returned is actually an array of 2 x 64bit ints with
       the top 32bits set to zero! */
     XGetWindowProperty(dpy, w, name,
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
/**
 * Get owner of mutex semaphore.  Returns the current owner of the semaphore
 *
 * @param[in] mutex The Swallow mutex structure
 * @param[out] hostId The ID of the host currently owning the mutex
 * @param[out] pid The process ID of the mutex
 *
 * @return 1(true) if owner found, 0 otherwise
 */
static int getSwallowMutexOwner(const SwallowMutex_t * mutex,
                                             uint32_t * hostId, uint32_t * pid)
{
     return getOwnerFromProperty(mutex->dpy, mutex->win, mutex->xObj, hostId, pid);
}

/**
 * Set owner of mutex semaphore
 *
 * @param[in,out] mutex The swallow mutex structure
 * @param[in] hostId The ID of the new owner of the mutex
 * @param[in] pid The process ID of the mutex
 *
 * @return none
 */
static void setSwallowMutexOwner(SwallowMutex_t * mutex,
                                                 uint32_t hostId, uint32_t pid)
{
     unsigned long temp[2] = {hostId, pid};

     D("Setting swallow mutex owner, hostId = 0x%08X, pid=%u\n",
                                            (unsigned) hostId, (unsigned) pid);
     XChangeProperty(mutex->dpy, mutex->win, mutex->xObj,
                        XA_INTEGER, 32, PropModeAppend,
                        (unsigned char*) (&temp), 2);
}

/**
 * Take mutex semaphore ownership.
 *
 * @param[in,out] mutex The swallow mutex structure
 *
 * @return none
 */
static void takeSwallowMutex(SwallowMutex_t * mutex)
{
     int countDown;
     const uint32_t ourPid = (uint32_t)getpid();
     const uint32_t ourHostId = getHostId();
     uint32_t otherPid;
     uint32_t otherHostId;
     uint32_t prevOtherPid = 0;

     if(!mutex || (mutex->dpy == 0))
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
          while(getSwallowMutexOwner(mutex, &otherHostId, &otherPid))
          {
               if( otherHostId == ourHostId)
               {
                    if(otherPid == ourPid)
                    {
 	                /* Great we have either successfully taken the semaphore
                           OR (unlikely) we previously had the semaphore! Exit
                           the function...*/
	                 mutex->taken = 1;
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
                         XDeleteProperty(mutex->dpy, mutex->win, mutex->xObj);
                         break; /* OK force early exit of inner while loop */
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
                    XDeleteProperty(mutex->dpy, mutex->win, mutex->xObj);
		    break;
               }

               usleep(250000); /* 250ms */
 	  }

          /* else no one has semaphore, timeout, or owner is dead -
           Set us as the owner, but we need to check if we weren't
           beaten to it so once more around the loop.
           Note even doing the recheck does not work in 100% of all
           cases due to task switching occuring at just the wrong moment
           see Mozdev bug 20088 - the fix is done use the stillHaveMutex
           function */

          setSwallowMutexOwner(mutex, ourHostId, ourPid);
     }
}

/**
 * Check if we still have the mutex semaphore
 *
 * @param[in,out] mutex The swallow mutex structure
 *
 * @return true if we still have the Mutex semaphore
 */
static int stillHaveMutex(SwallowMutex_t * mutex)
{
     uint32_t otherPid;
     uint32_t otherHostId;

     if(getSwallowMutexOwner(mutex, &otherHostId, &otherPid))
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

/**
 * Gives away ownership of the mutex semaphore
 *
 * @param[in,out] mutex The swallow mutex structure
 *
 * @return none
 */
static void giveSwallowMutex(SwallowMutex_t * mutex)
{
     if(!mutex || (mutex->dpy == 0))
     {
	  return;
     }
     D("Giving Swallow Mutex\n");
     XDeleteProperty(mutex->dpy, mutex->win, mutex->xObj);
     mutex->taken = 0;
}
#endif

/**
 * Mark the victim window with a property that indicates that this instance
 * of mozplugger-helper has made a claim to this victim. This is used to
 * guard against two instances of mozplugger-helper claiming the same victim.
 *
 * @param[in] dpy The display pointer
 * @param[in] w The window
 *
 * @return True(1) if taken, False(0) if not
 */
static int chkAndMarkVictimWindow(Display * dpy, Window w)
{
     unsigned long temp[2];
     uint32_t ourPid;
     uint32_t ourHostId;

     uint32_t pid;
     uint32_t hostId;
     int gotIt = 0;

     Atom windowOwnerMark = getWindowOwnerMarker(dpy);

     /* See if some other instance of Mozplugger has already 'marked' this
      * window */
     if(getOwnerFromProperty(dpy, w, windowOwnerMark, &hostId, &pid))
     {
          return 0; /* Looks like someone else has marked this window */
     }

     /* OK lets claim it for ourselves... */
     ourPid = (uint32_t)getpid();
     ourHostId = getHostId();

     temp[0] = ourHostId;
     temp[1] = ourPid;

     XChangeProperty(dpy, w, windowOwnerMark,
                     XA_INTEGER, 32, PropModeAppend,
                     (unsigned char*) (&temp), 2);

     /* See if we got it */
     if(getOwnerFromProperty(dpy, w, windowOwnerMark, &hostId, &pid))
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

/**
 * Calculate highest common factor
 *
 * @param[in] a First value
 * @param[in] b Second value
 *
 * @return Scaling factor
 */
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

/**
 * Adjust window size
 *
 * @param[in] dpy The display pointer
 *
 * @return none
 */
static void adjust_window_size(Display * dpy)
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
          const int d = gcd(victimDetails.idealWidth, victimDetails.idealHeight);
          int xaspect = victimDetails.idealWidth / d;
          int yaspect = victimDetails.idealHeight / d;

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
        && (victimDetails.currentWidth == w) && (victimDetails.currentHeight == h))
     {
          XEvent event;
          D("No change in window size so sending ConfigureNotify instead\n");
          /* According to X11 ICCCM specification, a compliant window
           * manager, (or proxy in this case) should sent a ConfigureNotify
           * event to the client even if no size change has occurred!
           * The code below is taken and adapted from the
           * TWM window manager and fixed movdev bug #18298. */
          event.type = ConfigureNotify;
          event.xconfigure.display = dpy;
          event.xconfigure.event = victimDetails.window;
          event.xconfigure.window = victimDetails.window;
          event.xconfigure.x = x;
          event.xconfigure.y = y;
          event.xconfigure.width = w;
          event.xconfigure.height = h;
          event.xconfigure.border_width = victimDetails.borderWidth;
          event.xconfigure.above = Above;
          event.xconfigure.override_redirect = False;

          XSendEvent(dpy, victimDetails.window, False, StructureNotifyMask,
                  &event);
     }

     /* Always resize the window, even when the target size has not changed.
	This is needed for gv. */
     XMoveResizeWindow(dpy, victimDetails.window,
		       x, y, (unsigned)w, (unsigned)h);
     victimDetails.x = x;
     victimDetails.y = y;
     victimDetails.currentWidth = w;
     victimDetails.currentHeight = h;
}

/**
 * Change the leader of the window. Peter - not sure why this code is needed
 * as it does say in the ICCCM that only client applications should change
 * client HINTS. Also all the window_group does is hint to the window manager
 * that there is a group of windows that should be handled together.
 *
 * @param[in] dpy The display pointer
 * @param[in] win The window ID
 * @param[in] newLeader The new window leader
 *
 * @return none
 */
static void change_leader(Display * dpy, Window win, Window newLeader)
{
     XWMHints *leader_change;
     D("Changing leader of window 0x%x\n", (unsigned) win);

     if ((leader_change = XGetWMHints(dpy, win)))
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
	  leader_change->window_group = newLeader;

	  D("New window leader is 0x%x\n",
                                        (unsigned) leader_change->window_group);
	  XSetWMHints(dpy, win,leader_change);
	  XFree(leader_change);
     }
     else
     {
	  D("XGetWMHints returned NULL\n");
     }
}

/**
 * Reparent the window, a count is kept of the number of attempts. If this
 * exceeds 10, give up (i.e. avoid two instances of helper fighting over
 * the same application). Originally this was done with a semaphore, but this
 * caused more problems than it fixed. So alternative is to assume that it is
 * such an unlikely occurance that two instances of helper will run at exactly
 * the same time and try to get the same window, that we give up.
 *
 * @param[in] dpy The display pointer
 *
 * @return none
 */
static void reparent_window(Display * dpy)
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

     XReparentWindow(dpy, victimDetails.window, parentDetails.window, 0, 0);
}

/**
 * Traditionally strcmp returns -1, 0 and +1 depending if name comes before
 * or after windowname, this function also returns +1 if error
 *
 * @param[in] windowname The window name to compare against
 * @param[in] name The name to compare against window name
 *
 * @return 0 if match, -1/+1 if different
 */
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

/**
 * Check name against the name of the passed window
 *
 * @param[in] dpy The display pointer
 * @param[in] w The window to compare against
 * @param[in] name The name to compare against window name
 *
 * @return 1 if match, 0 if not
 */
static char check_window_name(Display * dpy, Window w, const char *name)
{
     char * windowname;
     XClassHint windowclass;

     if (XFetchName(dpy, w, &windowname))
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

     if (XGetClassHint(dpy, w, &windowclass))
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

/**
 * Setup display ready for any reparenting of the application window. If the
 * WINDOW is NULL (can happen see mozdev bug #18837), then return failed
 * code from this function to indicate no window available. This then means
 * no swallowing will occur (even if requested).
 *
 * @return The display pointer or NULL
 */
static Display * setup_display(void)
{
     Display * dpy = NULL;
     char * displayname;

     if(parentDetails.window == 0)       /* mozdev bug #18837 */
     {
          D("setup_display() WINDOW is Null - so nothing setup\n");
          return NULL;
     }

     displayname = getenv("DISPLAY");
     D("setup_display(%s)\n", displayname);

     XSetErrorHandler(error_handler);

     dpy = XOpenDisplay(displayname);
     if(dpy == 0)
     {
          D("setup_display() failed cannot open display!!\n");
	  return 0;
     }

     if (!XGetWindowAttributes(dpy, parentDetails.window, &wattr))
     {
          D("setup_display() failed cannot get window attributes!!\n");
          XCloseDisplay(dpy);
          return NULL;
     }
     D("display=0x%x\n", (unsigned) dpy);
     D("WINDOW =0x%x\n", (unsigned) parentDetails.window);
     D("rootwin=0x%x\n", (unsigned) wattr.root);

     D("setup_display() done\n");

     return dpy;
}

/**
 * Add to list of possible victim windows. Called whenever we get a
 * CREATE_NOTIFY on the root window.
 *
 * @param[in] window The window
 *
 * @return none
 */
static void add_possible_victim(Window window)
{
     possible_victim_windows[possible_victim_count] = window;
     if(possible_victim_count < MAX_POSS_VICTIMS)
     {
          possible_victim_count++;
     }
}

/**
 * Checks if the passed window is the victim.
 *
 * @param[in] dpy The display pointer
 * @param[in] window The window ID
 * @param[in] mutex The Swallow mutex
 *
 * @return True if found our victim for first time.
 */
static int find_victim(Display * dpy, Window window, SwallowMutex_t * mutex)
{
     if (!victimDetails.window)
     {
          int i;
          Window found = 0;

	  D("Looking for victim... (%s)\n", winname);

          /* New way, check through list of newly created windows */
	  for(i = 0; i < possible_victim_count; i++)
          {
               if(window == possible_victim_windows[i])
	       {
                    if (check_window_name(dpy, window, winname))
                    {
                         found = true;
                         break;
                    }
               }
          }

	  if (found)
	  {
	       XWindowAttributes ca;
	       if(XGetWindowAttributes(dpy, window, &ca))
               {
                    /* See if some instance of mozplugger got the window
                     * before us, if not mark it as ours */
                    if(chkAndMarkVictimWindow(dpy, window))
                    {
                         victimDetails.window = window;
                         victimDetails.borderWidth = ca.border_width;
                         victimDetails.x = ca.x;
                         victimDetails.y = ca.y;
                         victimDetails.idealWidth = ca.width;
                         victimDetails.idealHeight = ca.height;
                         victimDetails.currentWidth = ca.width;
                         victimDetails.currentHeight = ca.height;

      	                 D("Found victim=0x%x, x=%i, y=%i, width=%i, height=%i, "
                                                                  "border=%i\n",
                                          (unsigned) victimDetails.window,
                                                     victimDetails.x,
                                                     victimDetails.y,
                                                     victimDetails.idealWidth,
                                                     victimDetails.idealHeight,
                                                     victimDetails.borderWidth);

                         /* To avoid losing events, enable monitoring events on
                          * victim at earlist opportunity */

                         XSelectInput(dpy, victimDetails.window,
                                             StructureNotifyMask | FocusChangeMask
                                           | EnterWindowMask | LeaveWindowMask);
                         XSync(dpy, False);

                         XSelectInput(dpy, wattr.root, 0);

#ifdef USE_MUTEX_LOCK
	                 giveSwallowMutex(mutex);
#endif
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
 * @param[in] dpy The display pointer
 * @param[in] rootWin The window ID
 * @param[in] ev the event
 * @param[in] mutex The swallow mutex
 *
 * @return none
 */
static void handle_rootWindow_event(Display * dpy, Window rootWin, const XEvent * const ev,
                                                        SwallowMutex_t * mutex)
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
               if(find_victim(dpy, wnd, mutex))
               {
                    /* If we get MapNotify on root before ReparentNotify for our
                     * victim this means either:-
                     * (1) There is no reparenting window manager running
                     * (2) We are running over NX
                     * With NX, we get the MapNotify way too early! (window not
                     * mapped yet on the server), so need to be clever?
                     * Set flag, this will be used later in the select loop */
                    D("Looks like no reparenting WM running\n");
		    change_leader(dpy, victimDetails.window, rootWin);
                    victimDetails.noWmRunning = true;
		    reparent_window(dpy);
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
           * fiddle, lets see if this is the window we are looking for */
          if(!ev->xreparent.override_redirect)
          {
               Window wnd = ev->xreparent.window;

               if(find_victim(dpy, wnd, mutex))
               {
                    /* Avoid the fight with window manager to get reparent,
                     * instead be kind and withdraw window and wait for WM
                     * to reparent window back to root */
                   D("Withdraw window 0x%x\n", (unsigned) wnd);

                   XWithdrawWindow(dpy, wnd, DefaultScreen(dpy));
#if 0
		   change_leader(dpy, victimDetails.window, rootWin);
                   reparent_window(dpy);
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

/**
 * handle X event for the victim window
 *
 * @param[in] dpy The display pointer
 * @param[in] ev the event
 *
 * @return none
 */
static void handle_victimWindow_event(Display * dpy, const XEvent * const ev)
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
	       adjust_window_size(dpy);
          }
          break;

     case ReparentNotify:
          {
               const XReparentEvent * evt = &ev->xreparent;

               if (evt->parent == parentDetails.window)
               {
 	             D("REPARENT NOTIFY on victim to the right window, "
		                           "parent=0x%x, window=0x%x, "
                                           "x=%i, y=%i, override_redirect=%i\n",
                                                (unsigned) evt->parent,
                                                (unsigned) evt->window,
                                               evt->x, evt->y,
					       evt->override_redirect);
	            victimDetails.reparented = true;
                    if(!victimDetails.mapped)
                    {
	                 D("XMapWindow(0x%x)\n", (unsigned) victimDetails.window);
                         XMapWindow(dpy, victimDetails.window);
                    }
                    else
                    {
	                 adjust_window_size(dpy);
                    }
	       }
               else
               {
	            D("REPARENT NOTIFY on victim to some other window! "
		                           "parent=0x%x, window=0x%x, "
                                           "x=%i, y=%i, override_redirect=%i\n",
                                               (unsigned) evt->parent,
                                               (unsigned) evt->window,
                                               evt->x, evt->y,
  					       evt->override_redirect);
                    victimDetails.noWmRunning = false;
	            victimDetails.reparented = false;
                    reparent_window(dpy);
	       }
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
               XSelectInput(dpy, victimDetails.window, 0);
               victimDetails.window = 0;
          }
          break;

     case EnterNotify:
          D("ENTER NOTIFY for victim, window=0x%x\n", (unsigned) ev->xcrossing.window);
          if(ev->xcrossing.mode == NotifyGrab)
          {
               D("Pointer grabbed by child!!\n");
          }
          break;

     default:
          {
              char * name = "";
              switch (ev->type)
              {
                   case FocusIn: name="FOCUS IN"; break;
                   case FocusOut: name="FOCUS OUT"; break;
                   case LeaveNotify: name="LEAVE NOTIFY"; break;
              }
              D("!!Got unhandled event for victim->%s(%d)\n", name, ev->type);
          }
          break;
     }
}

/**
 * handle X event for the parent window
 *
 * @param[in] dpy The display Pointer
 * @param[in] ev the event
 *
 * @return none
 */
static void handle_parentWindow_event(Display * dpy, const XEvent * const ev)
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
	            victimDetails.idealWidth = ev->xconfigurerequest.width,
                    victimDetails.idealHeight = ev->xconfigurerequest.height;
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
                    XSetWindowBorderWidth(dpy, victimDetails.window,
                                                                  (unsigned) w);
               }
               /* Only adjust if window has been mapped and reparented */
               if(adjustWindow && victimDetails.mapped
                                                    && victimDetails.reparented)
               {
                    adjust_window_size(dpy);
               }
          }
	  break;

     default:
          D("!!Got unhandled event for PARENT->%d\n", ev->type);
          break;
     }
}

/**
 * Check events from X
 * Read in events from X and process, keep reading in events until no more
 * and then exit. It is important that all events have been processed and
 * not left pending.
 *
 * @param[in] dpy The display pointer
 * @param[in] mutex The swallow mutex
 *
 * @return none
 */
static void check_x_events(Display * dpy, SwallowMutex_t * mutex)
{
     int numEvents = XPending(dpy);

     /* While some events pending... Get and action */
     while (numEvents > 0)
     {
          XEvent ev;

          XNextEvent(dpy, &ev);

	  if (ev.xany.window == wattr.root)
	  {
               handle_rootWindow_event(dpy, wattr.root, &ev, mutex);
	  }
	  else if (victimDetails.window
                               && ev.xany.window == victimDetails.window)
	  {
               handle_victimWindow_event(dpy, &ev);
	  }
          else if (parentDetails.window
                                    && (ev.xany.window == parentDetails.window))
	  {
               handle_parentWindow_event(dpy, &ev);
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
               numEvents = XPending(dpy);
          }
     }
}

/**
 * Terminate, called from signal handler or when SHUTDOWN_MSG received
 *
 * @param[in] mutex The swallow mutex
 * @param[in] pid The process ID of the mutex
 * @param[in] dpy The display pointer
 */
static void terminate(SwallowMutex_t * mutex, pid_t pid, Display * dpy)
{
#ifdef USE_MUTEX_LOCK
     giveSwallowMutex(mutex);
#endif

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
 * Check events from pipe connected to mozplugger
 * Read in events from pipe connected to mozplugger and process
 *
 * @param[in] dpy The display pointer
 * @param[in] mutex The swallow mutex
 * @param[in] pid The process ID of the mutex
 *
 * @return none
 */
static void check_pipe_fd_events(Display * dpy, SwallowMutex_t * mutex)
{
     struct PipeMsg_s msg;
     int n;

     Window oldwindow = parentDetails.window;

     n = read(pipe_fd, ((char *)&msg),  sizeof(msg));
     if((n == 0) || ((n < 0) && (errno != EINTR)))
     {
          D("Pipe read error, returned n=%i\n", n);
          terminate(mutex, sig_globals.childPid, dpy);
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
               parentDetails.window = (Window) msg.window_msg.window;
               parentDetails.width = (int) msg.window_msg.width;
               parentDetails.height = (int) msg.window_msg.height;
               D("Read Pipe, got WINDOW_MSG (win=0x%x - %i x %i)\n",
                       (unsigned) parentDetails.window, parentDetails.width, parentDetails.height);
               break;

          case SHUTDOWN_MSG:
               D("Read Pipe, got SHUTDOWN_MSG, terminating..\n");
               terminate(mutex, sig_globals.childPid, dpy);
               exit(EXIT_SUCCESS);

          default:
               D("Read Pipe, got unknown msg\n");
               return;
     }

     if (parentDetails.window && dpy )
     {
          if(parentDetails.window != oldwindow)
          {
               D("Switch parent window from 0x%x to 0x%x\n", (unsigned)oldwindow, (unsigned) parentDetails.window);
               victimDetails.reparented = false;

               /* To avoid losing events, enable monitoring events on new parent
                * before disabling monitoring events on the old parent */

               XSelectInput(dpy, parentDetails.window, SubstructureRedirectMask | FocusChangeMask);
               XSync(dpy, False);
               XSelectInput(dpy, oldwindow, 0);

               if(victimDetails.window)
               {
                    reparent_window(dpy);
               }
               else
               {
                    D("Victim window not ready to be reparented\n");
               }
          }

          if(victimDetails.window && victimDetails.mapped && victimDetails.reparented)
          {
               /* The window has been resized. */
               adjust_window_size(dpy);
          }
          else
          {
               D("Victim window not ready to be adjusted\n");
          }
     }
}

/**
 * Check the app status
 *
 * @param[in] flags The command flags
 *
 * @return -2 if broken, -1 
 */
static int check_app_status(unsigned flags)
{
     int retVal = 1;
     int status = wait_child(sig_globals.childPid);
             
     switch(status)
     {
     case -2:  /* App crashed */
          sig_globals.childPid = -1;
          exit(EX_UNAVAILABLE);
          break;

     case -1: /* App exited OK */
          sig_globals.childPid = -1;
          if (!(flags & H_IGNORE_ERRORS))
          {
               exit(WEXITSTATUS(status));
          }
          break;

     case 0: /* App deattached */
          sig_globals.childPid = -1;
          if((flags & H_DAEMON) == 0)
          {
               retVal = -1;
          }
          else
          {
               D("Child process has detached, keep going\n");
               retVal = 0;
          }
     /* case 1:  App still running */
     }
     return retVal;
}

/**
 * Keep checking all events until application dies
 * Use select to check if any events need processing
 *
 * @param[in] dpy The display pointer
 * @param[in] mutex The swallow mutex
 *
 * @return none
 */
static void check_all_events(Display * dpy, SwallowMutex_t * mutex)
{
     int sig_chld_fd = get_SIGCHLD_fd();

     while(1)
     {
          int rd_chld_fd = get_chld_out_fd();
	  int selectRetVal;
          int maxfd = 0;

	  struct timeval timeout;
          struct timeval * pTimeout = 0;

          fd_set fds;

          FD_ZERO(&fds);
          if (dpy)
          {
	       check_x_events(dpy, mutex);

               maxfd = ConnectionNumber(dpy);
               FD_SET(ConnectionNumber(dpy), &fds);
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
          }
          maxfd = pipe_fd > maxfd ? pipe_fd : maxfd;
          FD_SET(pipe_fd, &fds);
          if(sig_chld_fd >= 0)
          {
               maxfd = sig_chld_fd > maxfd ? sig_chld_fd : maxfd;
               FD_SET(sig_chld_fd, &fds);
          }
          if(rd_chld_fd >= 0)
          {
               maxfd = rd_chld_fd > maxfd ? rd_chld_fd : maxfd;
               FD_SET(rd_chld_fd, &fds);
          }

          D("SELECT IN %s\n", pTimeout ? "with timeout" : "");
          selectRetVal = select(maxfd + 1, &fds, NULL, NULL, pTimeout);
          D("SELECT OUT\n");

          if(selectRetVal > 0)
          {
	       if((pipe_fd >= 0) && FD_ISSET(pipe_fd, &fds))
               {
	            check_pipe_fd_events(dpy, mutex);
               }
               if((sig_chld_fd >= 0) && FD_ISSET(sig_chld_fd, &fds))
               {
                    handle_SIGCHLD_event();
               }
               if((rd_chld_fd >= 0) && FD_ISSET(rd_chld_fd, &fds))
               {
                    handle_chld_out_event(rd_chld_fd);
               }
          }
	  else if((selectRetVal == 0) && pTimeout)
          {
               D("Select timeout and suspect invisible WM\n");
               reparent_window(dpy);
          }
          else
          {
               D("Select exited unexpected, errno = %i\n", errno);
          }

          if(sig_globals.childPid >= 0)
          {
               int retVal = check_app_status(flags);
	       if(retVal < 0)
               {
                    return;
               }
               if(retVal == 0)
               {
                   sig_globals.childPid = -1;
               }
          }
     }
}

/**
 * Handle the application
 * Wait for application to finish and exit helper if a problem
 *
 * @param[in] dpy The display pointer
 * @param[in] mutex The swallow mutex
 *
 * @return none
 */
static void handle_app(Display * dpy, SwallowMutex_t * mutex)
{
     if (flags & H_SWALLOW)
     {
          /* Whilst waiting for the Application to complete, check X events */
	  check_all_events(dpy, mutex);

#ifdef USE_MUTEX_LOCK
          /* Make sure the semaphore has been released */
          giveSwallowMutex(mutex);
#endif
     }
     else if(flags & H_DAEMON)
     {
          /* If Daemon, then it is not supposed to exit, so we exit instead */
          D("App has become a daemon, so exit\n");
          terminate(mutex, -1, dpy);
          exit(EXIT_SUCCESS);
     }
     else
     {
	  check_all_events(NULL, NULL);
     }

     sig_globals.childPid = -1;
     D("Exited OK!\n");
}

/**
 * Exit early due to problem. Prints a message to stderr and then exits
 * the application
 *
 * @return none
 */
static void exitEarly(void)
{
     fprintf(stderr,"MozPlugger version " VERSION " helper application.\n"
		  "Please see 'man mozplugger' for details.\n");
     exit(EXIT_FAILURE);
}

/**
 * Expands the winname if it contains %f or %p by replacing %f by the file
 * name part of the URL or %p by the whole of thr URL.
 *
 * @param[in,out] buffer The data buffer to use
 * @param[in] bufferLen Size of buffer
 * @param[in] winname The window name
 * @param[in] file The file name associated with child
 *
 * @return none
 */
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
                         r = strrchr(file, '/'); /* Find the filename start */
                         if(r == 0)
                         {
                              r = file;
                         }
                         else
                         {
                              r++; /* Skip the slash */
                         }
                         while((*r != '\0') && (q < end))
                         {
                              *q++ = *r++;
                         }
                         p += 2; /* Skip the %f */
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

/**
 * Handle the SIGTERM signal. Terminate the helper and child. Calling C-lib
 * functions in a signal handler is not good, so avoid if we can. Best if
 * mozplugger.so uses the SHUTDOWN_MSG.
 *
 * @return none
 */
static void sigTerm()
{
     D("SIGTERM received\n");
     terminate(sig_globals.mutex, sig_globals.childPid, sig_globals.dpy);
     _exit(EXIT_SUCCESS);
}

/**
 * main() - normally called from the child process started by mozplugger.so
 *
 * @param[in] argc The number of arguments
 * @param[in] argv List of arguments
 *
 * @return Never returns (unless app exits)
 */
int main(int argc, char **argv)
{
     char buffer[100];

     unsigned long temp = 0;
     int i;
     int repeats;
     Display * dpy;
     SwallowMutex_t mutex;
     char * command;

     D("Helper started.....\n");

     if (argc < 3)
     {
          exitEarly();
     }

     memset(&victimDetails, 0, sizeof(victimDetails));
     memset(&parentDetails, 0, sizeof(parentDetails));

     i = sscanf(argv[1],"%d,%d,%d,%lu,%d,%d",
	    &flags,
	    &repeats,
	    &pipe_fd,
	    &temp,
	    &parentDetails.width,
	    &parentDetails.height);

     if(i < 6)
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


     sig_globals.dpy = dpy = setup_display();
     sig_globals.mutex = NULL;
     sig_globals.childPid = -1;

     signal(SIGTERM, sigTerm);

     if (!dpy)
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

	  /* This application will use the $repeat variable */
	  if (flags & H_REPEATCOUNT)
          {
	       loops = repeats;
          }

	  /* Expecting the application to loop */
	  if (flags & H_LOOP)
          {
	       loops = INF_LOOPS;
          }

	  if (flags & H_SWALLOW)
	  {
#ifdef USE_MUTEX_LOCK
	       /* If we are swallowing a victim window we need to guard
		  against more than one instance of helper running in
		  parallel, this is done using a global mutex semaphore.
                  Although its not successful in all cases! */
	       initSwallowMutex(dpy, wattr.root, &mutex);
               sig_globals.mutex = &mutex;

               /* There is a race condition when taking the semaphore that
                * means occasionally we think we have it but we dont
                * This do-while loop checks for that case - this
                * is better than putting a wait in the take semaphore
                * rechecking loop fixes Mozdev bug 20088 */
               do
               {
	           takeSwallowMutex(&mutex);
               }
               while(!stillHaveMutex(&mutex));
#endif
               XSelectInput(dpy, parentDetails.window, SubstructureRedirectMask);
	       XSelectInput(dpy, wattr.root, SubstructureNotifyMask);
	       XSync(dpy, False);
	  }

          pid = spawn_app(command, flags);
	  if(pid == -1)
          {
               terminate(&mutex, -1, dpy);
	       exit(EX_UNAVAILABLE);
          }
          sig_globals.childPid = pid;

	  D("Waiting for pid=%d\n", pid);

	  handle_app(dpy, &mutex);

	  D("Wait done (repeats=%d, loops=%d)\n", repeats, loops);
	  if (repeats < INF_LOOPS)
          {
	       repeats -= loops;
	  }
     }

     D("All done\n");
     terminate(&mutex, -1, dpy);
     return EXIT_SUCCESS;
}
