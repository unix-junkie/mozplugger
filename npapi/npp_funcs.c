/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stephen Mak <smak@sun.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <stdio.h>
#include <string.h>

#include "npapi.h"
#include "npruntime.h"
#include "npp_func_tab.h"
#include "npp_funcs.h"

/***********************************************************************
 *
 * Wrapper functions : plugin calling Netscape Navigator
 *
 * These functions let the plugin developer just call the APIs
 * as documented and defined in npapi.h, without needing to know
 * about the function table and call macros in npupp.h.
 *
 ***********************************************************************/

void NPP_Version(int * plugin_major, int * plugin_minor)
{
    *plugin_major = NP_VERSION_MAJOR;
    *plugin_minor = NP_VERSION_MINOR;
}


NPError NPP_InitFuncTable(NPPluginFuncs * pluginFuncs)
{
    NPError err = NPERR_NO_ERROR;

    if (pluginFuncs != NULL)
    {
        /* Create a temporary local copy */
        NPPluginFuncs funcs;

        /* Zero to start */
        memset(&funcs, 0, sizeof(NPPluginFuncs));

        /* First create a local copy of the table */
        funcs.version = (NP_VERSION_MAJOR << 8) + NP_VERSION_MINOR;
        funcs.newp = NPP_New;
        funcs.destroy = NPP_Destroy;
        funcs.setwindow = NPP_SetWindow;
        funcs.newstream = NPP_NewStream;
        funcs.destroystream = NPP_DestroyStream;
        funcs.asfile = NPP_StreamAsFile;
        funcs.writeready = NPP_WriteReady;
        funcs.write = NPP_Write;
        funcs.print = NPP_Print;
        funcs.event = NPP_HandleEvent;
        funcs.urlnotify = NPP_URLNotify;
        funcs.getvalue = (NPP_GetValueProcPtr) NPP_GetValue;
        funcs.setvalue = NPP_SetValue;
        funcs.gotfocus = NPP_GotFocus;
        funcs.lostfocus = NPP_LostFocus;
        funcs.urlredirectnotify = NPP_URLRedirectNotify;
        funcs.clearsitedata = NPP_ClearSiteData;
        funcs.getsiteswithdata = NPP_GetSitesWithData;

        if(pluginFuncs->size > sizeof(NPPluginFuncs))
        {
            /* Zero the fields we dont have anything for */
            memset(&pluginFuncs[sizeof(NPPluginFuncs)], 0,
                                     pluginFuncs->size - sizeof(NPPluginFuncs));
            funcs.size = sizeof(NPPluginFuncs);
        }
        else
        {
            funcs.size = pluginFuncs->size;
        }

        /* Copy across */
        memcpy(pluginFuncs, &funcs, funcs.size);
    }
    else
    {
        err = NPERR_INVALID_FUNCTABLE_ERROR;
    }
    return err;
}
