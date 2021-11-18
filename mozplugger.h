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
#ifndef _MOZPLUGGER_H_
#define _MOZPLUGGER_H_


/*****************************************************************************
 * Defines
 *****************************************************************************/

#ifndef MAXINT
#define MAXINT 0x7fffffff
#endif

#define MAX(X,Y) ((X)>(Y)?(X):(Y))

/* Time to wait for a process to exit */
#define KILL_TIMEOUT_USEC 100000

#define MAX_STATIC_MEMORY_POOL   65536

/* Maximum size of the buffer used for environment variables. */
#define ENV_BUFFER_SIZE 16348

#define LARGE_BUFFER_SIZE 16384
#define LARGE_BUFFER_SIZE_STR "16384"

#define SMALL_BUFFER_SIZE 128
#define SMALL_BUFFER_SIZE_STR "128"
#define FIND_CACHE_SIZE   10

/* Flags */
#define H_LOOP          0x00001u
#define H_DAEMON        0x00002u
#define H_STREAM        0x00004u
#define H_NOISY         0x00008u
#define H_REPEATCOUNT   0x00010u
#define H_EMBED         0x00020u
#define H_NOEMBED       0x00040u
#define H_IGNORE_ERRORS 0x00080u
#define H_SWALLOW       0x00100u
#define H_MAXASPECT     0x00200u
#define H_FILL          0x00400u
#define H_NEEDS_XEMBED  0x00800u
#define H_CONTROLS      0x01000u
#define H_LINKS         0x02000u
#define H_FMATCH        0x04000u
#define H_AUTOSTART     0x08000u

/*****************************************************************************
 * Control use of semaphore in mozplugger-helper, define if one wants
 * semaphores (rare cases doesnt work)
 *****************************************************************************/
#define USE_MUTEX_LOCK


/*****************************************************************************
 * mozplugger-common.c functions
 *****************************************************************************/

void my_kill(pid_t pid);

#endif
