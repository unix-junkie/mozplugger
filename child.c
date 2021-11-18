/*****************************************************************************
 *
 * Author:  Peter Leese <hubbe@hubbe.net>
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

#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sysexits.h>
#include <stdlib.h>

#include "mozplugger.h"
#include "debug.h"

static int sig_fds[2];

#define SIG_RD_FD  0
#define SIG_WR_FD  1

/******************************************************************************/
/**
 * Restore the SIG CHLD handler to default behaviour
 *
 * @return None
 *
 *****************************************************************************/
void restore_SIGCHLD_to_default(void)
{
     if(sig_fds[SIG_WR_FD] >= 0)
     {
          signal(SIGCHLD, SIG_DFL);
          close(sig_fds[SIG_WR_FD]);
     }

     if(sig_fds[SIG_RD_FD] >= 0)
     {
          close(sig_fds[SIG_RD_FD]);
     }
     sig_fds[SIG_WR_FD] = sig_fds[SIG_RD_FD] = -1;
}


/******************************************************************************/
/**
 * Convert signal to message in pipe - Callback function attached as a signal 
 * handler. This sends a message over a pipe about the signal that has 
 * just happened. This allows signals to be handled by select() 
 *
 * @param[in] sig The signal
 *
 * @return nothing
 *
 *****************************************************************************/
static void sig_to_fd(int sig)
{
     D("Signal %i routed to pipe\n", sig);
     if(write(sig_fds[SIG_WR_FD], &sig, sizeof(int)) != sizeof(int))
     {
          D("Signal write to pipe failed\n");
     }
}

/******************************************************************************/
/**
 * This function creates a signal handler for SIGCHLD that causes a file descriptor
 * to be ready when the signal occurs. This makes it easy to handle the signal 
 * within a select loop.
 *
 * @return N/A
 *
 ******************************************************************************/
void redirect_SIGCHLD_to_fd(void)
{
     if(pipe(sig_fds) != 0)
     {
          D("Failed to create pipe to handle signals");
          sig_fds[SIG_RD_FD] = sig_fds[SIG_WR_FD] = -1;
     }

     if(sig_fds[SIG_WR_FD] >= 0)
     {
          /* Re-direct SIGCHLD events through a pipe to be see by select() */
          signal(SIGCHLD, sig_to_fd);
     }
}

/******************************************************************************/
/**
 * This function is called to get the value of the file descriptor for the
 * SIGCHLD events.
 *
 * @return file descriptor
 *
 ******************************************************************************/
int get_SIGCHLD_fd(void)
{
    return sig_fds[SIG_RD_FD];
}

/******************************************************************************/
/**
 * This function is called to handle the SIGCHLD file descriptor when select
 * detects an event on that file descriptor.
 *
 * @return None
 * 
 ******************************************************************************/
void handle_SIGCHLD_event(void)
{
    int sig;
    read(sig_fds[SIG_RD_FD], &sig, sizeof(int));
}

/*****************************************************************************/
/** 
 * Wrapper for execlp() that calls the application.
 *
 * WARNING: This function runs in the daughter process so one must assume the
 * daughter uses a copy (including) heap memory of the parent's memory space
 * i.e. any write to memory here does not affect the parent memory space. 
 * Since Linux uses copy-on-write, best leave memory read-only and once execlp
 * is called, all daughter memory is wiped anyway (except the stack).
 *
 * @param[in] argv    Pointer to list of arguments
 * @param[in] flags   The flags
 *
 * @return Process ID
 *
 *****************************************************************************/
pid_t spawn_app(char * command, const int flags, int maxFd)
{
     pid_t pid = fork();

     if(pid == 0)
     {
          int i;
          char * app_argv[4];
        
          if( (flags & (H_CONTROLS | H_LINKS)) != 0)
          {
              setpgid(pid, 0);
          }

          app_argv[0] = "/bin/sh";
          app_argv[1] = "-c";
          app_argv[2] = command;
          app_argv[3] = 0;

          /* Redirect stdout & stderr to /dev/null */
          if( (flags & (H_NOISY | H_DAEMON)) != 0)
          {
    	       const int ofd = open("/dev/null", O_RDONLY);

  	       D("Redirecting stdout and stderr\n");

	       if(ofd == -1)
               {
	            exit(EX_UNAVAILABLE);
               }

	       dup2(ofd, 1);
	       dup2(ofd, 2);
	       close(ofd);
          }

          /* For a DAEMON we must also redirect STDIN to /dev/null
           * otherwise we can get some weird behaviour especially with realplayer
           * (I suspect it uses stdin to communicate with its peers) */
          if( (flags & H_DAEMON) != 0) 
          {
               const int ifd = open("/dev/null", O_WRONLY);

               D("Redirecting stdin also\n");

               if(ifd == -1)
               {
                    exit(EX_UNAVAILABLE);
               }
               dup2(ifd, 0);
               close(ifd);
          }

          D("Running %s\n", command);

          close_debug();
          /* Close up those file descriptors */
          for(i = 0; i < 2; i++)
          {
               close(sig_fds[i]);
          }
          for(i = 3; i <= maxFd; i++)
          {
              close(i);
          }
 
          execvp(app_argv[0], app_argv);
          D("Execvp failed. (errno=%d)\n", errno);
          exit(EX_UNAVAILABLE);
     }
     return pid;
}


