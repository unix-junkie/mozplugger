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

#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sysexits.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "cmd_flags.h"
#include "debug.h"

/* Time to wait for a process to exit */
#define KILL_TIMEOUT_USEC 100000

static int sig_rd_fd = -1;
static int sig_wr_fd = -1;
static int chld_rd_fd = -1; /* fd to read stdout of child */

/**
 * Restore the SIG CHLD handler to default behaviour
 *
 * @return None
 */
void restore_SIGCHLD_to_default(void)
{
     if(sig_wr_fd >= 0)
     {
          signal(SIGCHLD, SIG_DFL);
          close(sig_wr_fd);
          sig_wr_fd = -1;
     }

     if(sig_rd_fd >= 0)
     {
          close(sig_rd_fd);
          sig_rd_fd = -1;
     }
}


/**
 * Convert signal to message in pipe - Callback function attached as a signal
 * handler. This sends a message over a pipe about the signal that has
 * just happened. This allows signals to be handled by select()
 *
 * @param[in] sig The signal
 *
 * @return nothing
 */
static void sig_to_fd(int sig)
{
     D("Signal %i routed to pipe\n", sig);
     if(write(sig_wr_fd, &sig, sizeof(int)) != sizeof(int))
     {
          D("Signal write to pipe failed\n");
     }
}

/**
 * This function creates a signal handler for SIGCHLD that causes a file descriptor
 * to be ready when the signal occurs. This makes it easy to handle the signal
 * within a select loop.
 *
 * @return The fd
 */
int redirect_SIGCHLD_to_fd(void)
{
     int fds[2];
     if(pipe(fds) != 0)
     {
          D("Failed to create pipe to handle signals");
          sig_rd_fd = sig_wr_fd = -1;
          return -1;
     }

     sig_rd_fd = fds[0];
     sig_wr_fd = fds[1];

     /* Re-direct SIGCHLD events through a pipe to be see by select() */
     signal(SIGCHLD, sig_to_fd);

     return sig_rd_fd;
}

/**
 * This function is called to get the value of the file descriptor for the
 * SIGCHLD events.
 *
 * @return file descriptor
 */
int get_SIGCHLD_fd(void)
{
    return sig_rd_fd;
}

/**
 * This function is called to handle the SIGCHLD file descriptor when select
 * detects an event on that file descriptor.
 *
 * @return None
 */
void handle_SIGCHLD_event(void)
{
     int sig;
     read(sig_rd_fd, &sig, sizeof(int));
     D("Seen SIGCHLD event\n");
}

/**
 * This function is called to get the value of the file descriptor for the
 * stdout of the child.
 *
 * @return file descriptor
 */
int get_chld_out_fd(void)
{
     return chld_rd_fd;
}

/**
 * Handle the event of the child writing to stdout / stderr
 *
 * @param[in] fd
 *
 * @return false if failed
 */
bool handle_chld_out_event(int fd)
{
     static char linebuf[256];
     static int pos = 0;

     int n;
     int left = sizeof(linebuf) - pos - 1;
     
     if(left <= 0)
     {
          D("chld stdout > 256 without \n");
          D("[CHLD]:%s\n", linebuf);
          pos = 0;
          left = sizeof(linebuf) -1;
     }

     if((n = read(fd, &linebuf[pos], left)) > 0)
     {
          char * end;

          pos += n;
          linebuf[pos] = '\0';
          while( (end = strchr(linebuf, '\n')) != NULL)
          {
               char * p = end;
               while((p >= linebuf) && ((*p == '\r') || (*p == '\n') || (*p == ' ') || (*p == '\t')))
               {
                    p--;
               }
               p[1] = '\0';
               if(p > linebuf)
               {
                    D("[CHLD]:%s\n", linebuf);
               }
               pos -= (&end[1] - linebuf);
               memmove(linebuf, &end[1], pos+1);
          }
          return true;
     }
     else
     {
          if(pos > 0)
          {
              D("[CHLD]:%s\n", linebuf);
          }
          pos = 0;
          return false;
     }
}

/**
 * Wrapper for execlp() that calls the application.
 *
 * WARNING: This function runs in the daughter process so one must assume the
 * daughter uses a copy (including) heap memory of the parent's memory space
 * i.e. any write to memory here does not affect the parent memory space.
 * Since Linux uses copy-on-write, best leave memory read-only and once execlp
 * is called, all daughter memory is wiped anyway (except the stack).
 *
 * @param[in] command Pointer to list of arguments
 * @param[in] flags The flags
 *
 * @return Process ID
 */
pid_t spawn_app(char * command, const int flags)
{
     int fds[2] = {-1, -1};
     pid_t pid;

     /* For debug connect a pipe to the stdout / stderr of the child */
     if( (((flags & H_DAEMON)) == 0) && is_debugging())
     {
         pipe(fds);
     }

     pid = fork();

     if(pid == 0)
     {
          int maxFd;
          int i;
          char * app_argv[4];

          /* Group child and any grand children under the same process group */
          if(setpgid(pid, 0) != 0)
          {
               D("Failed to set process group ID\n");
          }

          app_argv[0] = "/bin/sh";
          app_argv[1] = "-c";
          app_argv[2] = command;
          app_argv[3] = 0;

          /* Redirect stdout & stderr to /dev/null */
          if(( (flags & (H_NOISY | H_DAEMON)) != 0) && (fds[1] <= 0))
          {
               fds[1] = open("/dev/null", O_RDONLY);
          }

          if(fds[1] > 0)
          {
  	       D("Redirecting stdout and stderr to %i\n", fds[1]);
	       dup2(fds[1], 1);
	       dup2(fds[1], 2);
	       close(fds[1]);
	       close(fds[0]);
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

          close(sig_rd_fd);
          close(sig_wr_fd);
          maxFd = sysconf(_SC_OPEN_MAX);
          for(i = 3; i <= maxFd; i++)
          {
              close(i);
          }
//          fprintf(stderr, "Started\n");

          execvp(app_argv[0], app_argv);
          D("Execvp failed. (errno=%d)\n", errno);
          exit(EX_UNAVAILABLE);
     }
     else
     {
          if(fds[1] > 0)
          {
	       close(fds[1]);
               chld_rd_fd = fds[0];
          }
     }
     return pid;
}
/**
 * Wait for the child
 *
 * @param[in] pid The child pid
 *
 * @return (-2) If app crashed, (-1) If app exited but with error, 
 * (0) If child dettached or exited successfully, (1) If still running
 */
int wait_child(pid_t pid)
{
     int retVal = 1;
     int status;

     if(waitpid(pid, &status, WNOHANG) != 0)
     {
          while(chld_rd_fd >= 0)
          {
               fd_set fds;
               struct timeval timeout;
               timeout.tv_usec = 100000;
               timeout.tv_sec = 0;
        
               FD_ZERO(&fds);
               FD_SET(chld_rd_fd, &fds);
               if(select(chld_rd_fd + 1, &fds, NULL, NULL, &timeout) > 0)
               {
                    if(!handle_chld_out_event(chld_rd_fd))
                    {
                         close(chld_rd_fd);
                         chld_rd_fd = -1;
                    }
               }
               else
               {
                    close(chld_rd_fd);
                    chld_rd_fd = -1;
               }
          }

          /* If Application completed in a bad way, then lets give up now */
          if (!WIFEXITED(status))
          {
               D("Process dumped core or something...\n");
               retVal = -2;
          }
          else if (WEXITSTATUS(status))
          {
               D("Process exited with status: %d\n", WEXITSTATUS(status));
               retVal = -1;
          }
          else
          {
               D("Child has stopped or detached\n");
               retVal = 0;
          }
     }
//     D("wait_child retVal=%i\n", retVal);
     return retVal;
}


/**
 * Adaptive kill(), keep calling kill with higher and higher signal level,
 *
 * NOTE: kill return zero on successfully sending the signal so the code
 * only exits the nested ifs when kill no longer can send the signal!
 *
 * @param[in] pid The process ID of process to kill
 */
void kill_app(pid_t pid)
{
     int status;

     D("Killing PID %d and associated process group with with SIGTERM\n", pid);
     kill(pid, SIGTERM); /* Signal the child */
     if(kill(-pid, SIGTERM) == 0) /* Signal the childs progress group */
     {
          usleep(KILL_TIMEOUT_USEC);
          /* Child is still alive */
          D("Killing process group %d with SIGTERM\n", pid);
          if( kill(-pid, SIGTERM) == 0)             /* child got signal */
          {
               usleep(KILL_TIMEOUT_USEC);
               /* Child is still alive */
               D("Killing process group %d with SIGTERM\n", pid);
               if( kill(-pid, SIGTERM) == 0)             /* child got signal */
               {
                    usleep(KILL_TIMEOUT_USEC);
                    /* Child still ALIVE? */
                    D("Killing PID %d with SIGKILL\n", pid);
                    kill(pid, SIGKILL);
                    kill(-pid, SIGKILL);
               }
          }
     }

     D("Waiting for kids\n");
     while(waitpid(-1, &status, WNOHANG) > 0);

     if(chld_rd_fd >= 0)
     {
          close(chld_rd_fd);
     }
}

