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

#include "npapi.h"
#include "npruntime.h"
#include "npn_func_tab.h"
#include "npp_func_tab.h"
#include "npn_funcs.h"


#include "debug.h"

static int browserApiMajorVer = 0;
static int browserApiMinorVer = 0;

/**
 * Get information about the browser this plugin is running under
 *
 */
void get_api_version(void)
{
     int pluginApiMajorVer;
     int pluginApiMinorVer;

     NPN_Version(&browserApiMajorVer, &browserApiMinorVer);

     NPP_Version(&pluginApiMajorVer,  &pluginApiMinorVer);

     D("NPN_Version() - API versions plugin=%d.%d Browser=%d.%d\n",
             pluginApiMajorVer, pluginApiMinorVer,
             browserApiMajorVer, browserApiMinorVer);
}


/**
 * Find out if browser has the resize bug
 *
 * @return 1 if so
 */
NPBool does_browser_have_resize_bug(void)
{
     /* Mozilla work around not require for versions > 0.13 */
     if((browserApiMajorVer > 0) || (browserApiMinorVer > 13))
     {
          return 0;
     }
     return 1;
}

/**
 * Find out if browser supports XEmbed
 *
 * @return 1 if so
 */
NPBool does_browser_support_xembed(void)
{
     NPBool value;
     /* Check if browser supports XEmbed and also what toolkit the browser
      * uses */
     NPError err = NPN_GetValue((void *)0, NPNVSupportsXEmbedBool, (void*) &value);
     if(err != NPERR_NO_ERROR)
     {
          D("NPN_GetValue(NPNVSupportsXEmbedBool) - Browser returned err=%i\n",
                          err);
          return 0;
     }


     D("NPN_GetValue(NPNSupportsXEmbedBool) - Browser returned %i\n", value);
     return value;
}

/**
 * Find out which toolkit the browser supports
 *
 * @param[in] instance The instance pointer
 *
 * @return the toolkit type
 */
NPNToolkitType get_browser_toolkit(NPP instance)
{
     NPNToolkitType value;
     /* Get tool kit supported */
     NPError err = NPN_GetValue(instance, NPNVToolkit, (void *) &value);
     if(err != NPERR_NO_ERROR)
     {
          D("NPN_GetValue(NPNVToolkit) - Browser returned err=%i\n", err);
          return 0;
     }

     switch(value)
     {
          case NPNVGtk12:
               D("NPN_GetValue(NPNVToolkit) - Browser supports GTK1.2\n");
               break;

          case NPNVGtk2:
               D("NPN_GetValue(NPNToolkit) - Browser supports GTK2\n");
               break;
     }
     return value;
}


/**
 * Find out if browser supports advanced key handling for this instance of the
 * plugin
 *
 * @param[in] instance The instance pointer
 *
 * @return 1 if so
 */
NPBool does_browser_support_key_handling(NPP instance)
{
     NPBool value;
     /* Get Advanced Keyboard focus */
     NPError err = NPN_GetValue(instance, NPNVsupportsAdvancedKeyHandling,
                                                             (void *) &value);
     if(err != NPERR_NO_ERROR)
     {
          D("NPN_GetValue(NPNVSupportsAdvancedKeyHandling) - "
                                               "Browser returned err=%i\n", err);
          return 0;
     }


     D("NPN_GetValue(NPNVSupportsAdvancedKeyHandling) - "
                                                 "Browser returned %i\n", value);
     return value;
}

/**
 * Convert enum to string
 *
 * @param[in] variable The enum value of the variable
 *
 * @return pointer to const string
 */
const char * NPPVariableToString(NPPVariable variable)
{
     const char * varName;

     switch (variable)
     {
     case NPPVpluginNameString:
	  varName = "NPPVpluginNameString";
	  break;

     case NPPVpluginDescriptionString:
	  varName = "NPPVpluginDescriptionString";
	  break;

     case NPPVpluginNeedsXEmbed:
          varName = "NPPVpluginNeedsXEmbed";
          break;

     case NPPVpluginScriptableNPObject :
	  varName = "NPPVpluginScriptableNPObject";
          break;

     case NPPVpluginWindowBool:
          varName = "NPPVpluginWindowBool";
          break;

     case NPPVpluginTransparentBool:
          varName = "NPPVpluginTransparentBool";
          break;

     case NPPVjavaClass:
	  varName = "NPPVjavaClass";
          break;

     case NPPVpluginWindowSize:
	  varName = "NPPVpluginWindowSize";
          break;

     case NPPVpluginTimerInterval:
	  varName = "NPPVpluginTimerInterval";
          break;

     case NPPVpluginScriptableInstance:
	  varName = "NPPVpluginScriptableInstance";
          break;

     case NPPVpluginScriptableIID:
	  varName = "NPPVpluginScriptableIID";
          break;

     case NPPVjavascriptPushCallerBool:
	  varName = "NPPVjavascriptPushCallerBool";
          break;

     case NPPVpluginKeepLibraryInMemory:
	  varName = "NPPVpluginKeepLibraryInMemory";
          break;

     case NPPVformValue:
	  varName = "NPPVformValue";
          break;

     case NPPVpluginUrlRequestsDisplayedBool:
	  varName = "NPPVpluginUrlRequestsDisplayedBool";
          break;

     case NPPVpluginWantsAllNetworkStreams:
	  varName = "NPPVpluginWantsNetworkStreams";
          break;

     case NPPVpluginNativeAccessibleAtkPlugId:
          varName = "NPPVpluginNativeAccessibleAtkPlugId";
          break;

     case NPPVpluginCancelSrcStream:
          varName = "NPPVpluginCancelSrcStream";
          break;

     case NPPVsupportsAdvancedKeyHandling:
          varName = "NPPVsupportsAdvancedKeyHandling";
          break;

     case NPPVpluginUsesDOMForCursorBool:
          varName = "NPPVpluginUsesDOMForCursorBool";
          break;

     default:
	  varName = "unknown";
          break;
     }
     return varName;
}

