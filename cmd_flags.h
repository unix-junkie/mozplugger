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

#ifndef _MOZPLUGGER_CMD_FLAGS_H_
#define _MOZPLUGGER_CMD_FLAGS_H_


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
#define H_SMALL_CNTRLS  0x10000u

#define INF_LOOPS 0x7fffffff

struct str2flag_s
{
     const char * name;
     unsigned int value;
};

typedef struct str2flag_s str2flag_t;

#endif
