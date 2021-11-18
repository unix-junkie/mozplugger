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

#include <stdio.h>

#include "npapi.h"
#include "np_funcs.h"
#include "plugin_entry.h"
#include "plugin_name.h"

/**
 * Plugin magic, separated to allow it to be easy to modify the objfile,
 * to put in different plugin magics. This maps to a plugin name.
 * Javascript running on the browser uses the
 * plugin name to test for presence of the plugin so we need to be able to
 * simulate different plugins by having different plugin objects.
 */
static char magic[MAX_PLUGIN_MAGIC_LEN] = PLACE_HOLDER_STR;

/**
 * Entry point called from browser
 *
 * @param[in] nsTable, the netscape callback function table
 * @param[in] pluginFuncs, the plugin callback function table
 *
 * @return status
 */
NPError NP_Initialize(struct NPNetscapeFuncs_s * nsTable,
                                            struct NPPluginFuncs_s * pluginFuncs)
{
    return NP2_Initialize(magic, nsTable, pluginFuncs);
}

/**
 * Entry point called from browser
 *
 * @param[in] future Not used
 * @param[in] variable The enumerated name of the variable
 * @param[out] value Place to put the answer
 *
 * @return status
 */
NPError NP_GetValue(void * future, NPPVariable variable, void *value)
{
    return NP2_GetValue(magic, variable, value);
}

/**
 * Entry point called from browser
 *
 * @return a string that contains a list of mimetypes and descriptions that
 * this plugin handles. Each mimetype, description entry is separated by a ';'.
 */
const char * NP_GetMIMEDescription(void)
{
    return NP2_GetMIMEDescription(magic);
}

/**
 * Entry point called from browser
 *
 * @return a string that is the pluin version string
 */
const char * NP_GetPluginVersion(void)
{
    return NP2_GetPluginVersion(magic);
}

/**
 * Entry point called from browser
 *
 * @return status
 */
NPError NP_Shutdown(void)
{
    return NP2_Shutdown(magic);
}
