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

#ifndef _NPN_GET_HELPERS_H_
#define _NPN_GET_HELPERS_H_

extern void get_api_version(void);

extern NPBool does_browser_have_resize_bug(void);

extern NPBool does_browser_support_xembed(void);

extern NPNToolkitType get_browser_toolkit(NPP instance);

extern NPBool does_browser_support_key_handling(NPP instance);

extern const char * NPPVariableToString(NPPVariable variable);

#endif
