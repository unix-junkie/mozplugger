/*****************************************************************************
 *
 * Original Author:  Fredrik Hübinette <hubbe@hubbe.net>
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/wait.h>

#include "mozplugger.h"
#include "debug.h"

/*****************************************************************************/
/**
 * @brief Wrapper for kill
 *
 * Adaptive kill(), keep calling kill with higher and higher signal level,
 *
 * NOTE: kill return zero on successfully sending the signal so the code
 * only exits the nested ifs when kill no longer can send the signal!
 *
 * @param[in] pid The process ID of process to kill
 *
 *****************************************************************************/
void my_kill(pid_t pid)
{
     int status;

     D("Killing PID %d with SIGTERM\n", pid);
     if (!kill(pid, SIGTERM))             /* '!kill' == true if signal sent! */
     {
          usleep(KILL_TIMEOUT_USEC);
          D("Killing PID %d with SIGTERM\n", pid);
          if (!kill(pid, SIGTERM))
          {
               usleep(KILL_TIMEOUT_USEC);
               D("Killing PID %d with SIGTERM\n", pid);
               if (!kill(pid, SIGTERM))
               {
                    D("Killing PID %d with SIGKILL\n", pid);
                    kill(pid, SIGKILL);
               }
          }
     }

     D("Waiting for sons\n");
     while (waitpid(-1, &status, WNOHANG) > 0);
}
