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

#ifndef _MOZPLUGGER_DEBUG_H_
#define _MOZPLUGGER_DEBUG_H_

#define DEBUG_FILENAME "mozdebug"

void close_debug(void);

char * get_debug_path(void);

int is_debugging(void);

#ifdef __GNUC__
void D(char *fmt, ...) __attribute__((format(printf,1,2)));
#else
void D(char *fmt, ...);
#endif

#endif
