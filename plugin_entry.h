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

#ifndef _PLUGIN_ENTRY_H_
#define _PLUGIN_ENTRY_H_

/* forward references */
struct NPNetscapeFuncs_s;
struct NPPluginFuncs_s;

NP_EXPORT(NPError) NP2_Initialize(const char * magic,
                                     const struct NPNetscapeFuncs_s * nsTable,
                                          struct NPPluginFuncs_s * pluginFuncs);

NP_EXPORT(NPError) NP2_GetValue(const char * magic, NPPVariable variable, void *value);

NP_EXPORT(const char *) NP2_GetMIMEDescription(const char * magic);

NP_EXPORT(NPError) NP2_Shutdown(const char * magic);

NP_EXPORT(const char *) NP2_GetPluginVersion(const char * magic);

#endif
