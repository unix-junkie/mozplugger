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

#include <string.h>

#include "npapi.h"
#include "npruntime.h"
#include "npn_funcs.h"

#include "mozplugger.h"
#include "debug.h"
#include "scriptable_obj.h"


typedef struct
{
     struct NPObject objHead;
     NPP assocInstance;
} our_NPObject_t;

/**
 * Global variables
 */

static NPClass pluginClass;

/**
 * Helper debug function fro printing scriptable object name.
 *
 * @param[in] name The Name identifier
 */
static void debugLogIdentifier(const char * func_name, NPIdentifier name)
{
     if(NPN_IdentifierIsString(name))
     {
          char * str = NPN_UTF8FromIdentifier(name);
          D("%s(%s)\n", func_name, str ? (char *)str : "NULL");
          NPN_MemFree(str);
     }
     else
     {
          D("%s(%i)\n", func_name, NPN_IntFromIdentifier(name));
     }
}

/**
 * Provides plugin HasMethod that can be called from broswer
 *
 * @param[in] npobj The scriptable object
 * @param[in] name The method name
 *
 * @return True if has that method
 */
bool NPP_HasMethod(NPObject *npobj, NPIdentifier name)
{
     bool retVal = 0;
     char * str;
     debugLogIdentifier("NPP_HasMethod", name);

     if( (str = NPN_UTF8FromIdentifier(name)) != NULL)
     {
          if ((strcasecmp("getvariable", str) == 0))
	  {
               retVal = 1;
	  }
          NPN_MemFree(str);
     }
     return retVal;
}

/**
 * Provides plugin Invoke that can be called from broswer
 *
 * @return True if method was invoked
 */
bool NPP_Invoke(NPObject *npobj, NPIdentifier name,
                   const NPVariant *args, uint32_t argCount, NPVariant *result)
{
     debugLogIdentifier("NPP_Invoke", name);
     D("Arg-count=%u\n", (unsigned) argCount); 
//     if( (str = NPN_UTF8FromIdentifier(name)) != NULL)
//     {
//     } 
     return 0;
}

/**
 * Provides plugin HasProperty that can be called from broswer
 *
 * @return True if has property
 */
bool NPP_HasProperty(NPObject *npobj, NPIdentifier name)
{
     bool retVal = 0;
     char * str;

     debugLogIdentifier("NPP_HasProperty", name);

     if( (str = NPN_UTF8FromIdentifier(name)) != NULL)
     {
          if ((strcasecmp("isplaying", str) == 0) ||
              (strcasecmp("__nosuchmethod__", str) == 0))
	  {
               retVal = 1;
	  }
          NPN_MemFree(str);
     }
     return retVal;
}

/**
 * Provides plugin GetProperty that can be called from broswer
 *
 * @return True if got property
 */
bool NPP_GetProperty(NPObject *npobj, NPIdentifier name, NPVariant *result)
{
     bool retVal = 0;
     char * str;

     debugLogIdentifier("NPP_GetProperty", name);

     if( (str = NPN_UTF8FromIdentifier(name)) != NULL)
     {
          if (strcasecmp("isplaying", str) == 0)
          {
               NPP instance = ((our_NPObject_t *)npobj)->assocInstance;

	       result->type = NPVariantType_Bool;
               result->value.boolValue = 0;
               retVal = 1;

               if(instance)
               {
                    result->value.boolValue = is_playing(instance);
               }
          }
          NPN_MemFree(str);
     }
     return retVal;
}

/**
 * Provides plugin SetProperty that can be called from broswer
 *
 * @return True if set property
 */
bool NPP_SetProperty(NPObject *npobj, NPIdentifier name, const NPVariant *value)
{
     debugLogIdentifier("NPP_SetProperty", name);

     return 0;
}

/**
 * Create a JavaScript object
 *
 * @return Pointer to created object
 */
static NPObject * NPP_AllocateObj(NPP instance, NPClass * aClass)
{
    our_NPObject_t * pObj = NPN_MemAlloc(sizeof(our_NPObject_t));
    pObj->assocInstance = instance;
    return (NPObject *) pObj;
}

/**
 * Get plugin Scritable Object
 *
 * @return Returns True if Xembed required
 */
NPObject * getPluginScritableObject(NPP instance, NPError * pErr)
{
     if (instance == 0)
     {
          *pErr = NPERR_GENERIC_ERROR;
          return 0;
     }

     D("Scritable object created..\n");
     memset(&pluginClass, 0, sizeof(NPClass));

     pluginClass.structVersion = NP_CLASS_STRUCT_VERSION;

     pluginClass.allocate = NPP_AllocateObj;
     pluginClass.hasMethod = NPP_HasMethod;
     pluginClass.invoke = NPP_Invoke;
     pluginClass.hasProperty = NPP_HasProperty;
     pluginClass.getProperty = NPP_GetProperty;
     pluginClass.setProperty = NPP_SetProperty;

     return NPN_CreateObject(instance, &pluginClass);
}

