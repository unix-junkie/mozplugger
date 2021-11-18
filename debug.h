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
#ifndef _DEBUG_H_
#define _DEBUG_H_


/*****************************************************************************
 * Defines
 *****************************************************************************/

#define DEBUG_FILENAME "mozdebug"
void close_debug(void);
char * get_debug_path(void);

#ifdef __GNUC__
void D(char *fmt, ...) __attribute__((format(printf,1,2)));
#else
void D(char *fmt, ...);
#endif

#endif
