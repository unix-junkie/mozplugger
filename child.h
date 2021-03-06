/**
 *
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *
 */

#ifndef _MOZPLUGGER_CHILD_
#define _MOZPLUGGER_CHILD_

extern void restore_SIGCHLD_to_default(void);

/* Use this to redirct the SIGCHLD signal to a file descriptor so we can
 * select on this signal */
extern int redirect_SIGCHLD_to_fd(void);

extern int get_SIGCHLD_fd(void);

extern void handle_SIGCHLD_event(void);


extern int get_chld_out_fd(void);

extern void handle_chld_out_event(int fd);


extern pid_t spawn_app(char * command, const int flags);

extern int wait_child(pid_t pid);

extern void kill_app(pid_t pid);

#endif
