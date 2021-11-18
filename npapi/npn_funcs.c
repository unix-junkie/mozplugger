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
#include "npn_func_tab.h"
#include "npn_funcs.h"

/***********************************************************************
 *
 * Globals
 *
 ***********************************************************************/

static NPNetscapeFuncs gNetscapeFuncs; /* Netscape Function table */


/***********************************************************************
 *
 * Wrapper functions : plugin calling Netscape Navigator
 *
 * These functions let the plugin developer just call the APIs
 * as documented and defined in npapi.h, without needing to know
 * about the function table and call macros in npupp.h.
 *
 ***********************************************************************/

void NPN_Version(int * netscape_major, int * netscape_minor)
{
    /* Major version is in high byte */
    *netscape_major = gNetscapeFuncs.version >> 8;
    /* Minor version is in low byte */
    *netscape_minor = gNetscapeFuncs.version & 0xFF;
}

/******************************************************************************/
NPError NPN_GetValue(NPP instance, NPNVariable variable, void *r_value)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_GetValueProcPtr func = gNetscapeFuncs.getvalue;
    if(func)
    {
        retVal = (*func)(instance, variable, r_value);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_SetValue(NPP instance, NPPVariable variable, void *value)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_SetValueProcPtr func = gNetscapeFuncs.setvalue;
    if(func)
    {
        retVal = (*func)(instance, variable, value);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_GetURL(NPP instance, const char* url, const char* window)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_GetURLProcPtr func = gNetscapeFuncs.geturl;
    if(func)
    {
        retVal = (*func)(instance, url, window);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_GetURLNotify(NPP instance, const char* url, const char* window,
                                                               void* notifyData)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_GetURLNotifyProcPtr func = gNetscapeFuncs.geturlnotify;
    if(func)
    {
        retVal = (*func)(instance, url, window, notifyData);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_PostURL(NPP instance, const char* url, const char* window,
                                     uint32_t len, const char* buf, NPBool file)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_PostURLProcPtr func = gNetscapeFuncs.posturl;
    if(func)
    {
        retVal = (*func)(instance, url, window, len, buf, file);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_PostURLNotify(NPP instance, const char* url, const char* window,
                   uint32_t len, const char* buf, NPBool file, void* notifyData)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_PostURLNotifyProcPtr func = gNetscapeFuncs.posturlnotify;
    if(func)
    {
        retVal = (*func)(instance, url, window, len, buf, file, notifyData);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_RequestRead(NPStream* stream, NPByteRange* rangeList)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_RequestReadProcPtr func = gNetscapeFuncs.requestread;
    if(func)
    {
        retVal = (*func)(stream, rangeList);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_NewStream(NPP instance, NPMIMEType type, const char *window,
                                                          NPStream** stream_ptr)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_NewStreamProcPtr func = gNetscapeFuncs.newstream;
    if(func)
    {
        retVal = (*func)(instance,  type, window, stream_ptr);
    }
    return retVal;
}

/******************************************************************************/
int32_t NPN_Write(NPP instance, NPStream* stream, int32_t len, void* buffer)
{
    int32_t retVal = 0;
    NPN_WriteProcPtr func = gNetscapeFuncs.write;
    if(func)
    {
        retVal = (*func)(instance, stream, len, buffer);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_DestroyStream(NPP instance, NPStream* stream, NPError reason)
{
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    NPN_DestroyStreamProcPtr func = gNetscapeFuncs.destroystream;
    if(func)
    {
        retVal = (*func)(instance, stream, reason);
    }
    return retVal;
}

/******************************************************************************/
void NPN_Status(NPP instance, const char* message)
{
    NPN_StatusProcPtr func = gNetscapeFuncs.status;
    if(func)
    {
        (*func)(instance, message);
    }
}

/******************************************************************************/
const char* NPN_UserAgent(NPP instance)
{
    const char * retVal = 0;
    NPN_UserAgentProcPtr func = gNetscapeFuncs.uagent;
    if(func)
    {
        retVal = (*func)(instance);
    }
    return retVal;
}

/******************************************************************************/
void* NPN_MemAlloc(uint32_t size)
{
    void * retVal = 0;
    NPN_MemAllocProcPtr func = gNetscapeFuncs.memalloc;
    if(func)
    {
        retVal = (*func)(size);
    }
    return retVal;
}

/******************************************************************************/
void NPN_MemFree(void* ptr)
{
    NPN_MemFreeProcPtr func = gNetscapeFuncs.memfree;
    if(func)
    {
        (*func)(ptr);
    }
}

/******************************************************************************/
uint32_t NPN_MemFlush(uint32_t size)
{
    uint32_t retVal = 0;
    NPN_MemFlushProcPtr func = gNetscapeFuncs.memflush;
    if(func)
    {
        retVal = (*func)(size);
    }
    return retVal;
}

/******************************************************************************/
void NPN_ReloadPlugins(NPBool reloadPages)
{
    NPN_ReloadPluginsProcPtr func = gNetscapeFuncs.reloadplugins;
    if(func)
    {
        (*func)(reloadPages);
    }
}

/******************************************************************************/
void NPN_InvalidateRect(NPP instance, NPRect *invalidRect)
{
    NPN_InvalidateRectProcPtr func = gNetscapeFuncs.invalidaterect;
    if(func)
    {
        (*func)(instance, invalidRect);
    }
}

/******************************************************************************/
void NPN_InvalidateRegion(NPP instance, NPRegion invalidRegion)
{
    NPN_InvalidateRegionProcPtr func = gNetscapeFuncs.invalidateregion;
    if(func)
    {
        (*func)(instance, invalidRegion);
    }
}

/******************************************************************************/
void NPN_ForceRedraw(NPP instance)
{
    NPN_ForceRedrawProcPtr func = gNetscapeFuncs.forceredraw;
    if(func)
    {
        (*func)(instance);
    }
}

/******************************************************************************/
void NPN_PushPopupsEnabledState(NPP instance, NPBool enabled)
{
    NPN_PushPopupsEnabledStateProcPtr func
                                       = gNetscapeFuncs.pushpopupsenabledstate;
    if(func)
    {
        (*func)(instance, enabled);
    }
}

/******************************************************************************/
void NPN_PopPopupsEnabledState(NPP instance)
{
    NPN_PopPopupsEnabledStateProcPtr func
                                         = gNetscapeFuncs.poppopupsenabledstate;
    if(func)
    {
        (*func)(instance);
    }
}

/******************************************************************************/
NPIdentifier NPN_GetStringIdentifier(const NPUTF8 *name)
{
    NPIdentifier retVal = 0;
    NPN_GetStringIdentifierProcPtr func = gNetscapeFuncs.getstringidentifier;
    if(func)
    {
        retVal = (*func)(name);
    }
    return retVal;
}

/******************************************************************************/
void NPN_GetStringIdentifiers(const NPUTF8 **names, int32_t nameCount,
                                                      NPIdentifier *identifiers)
{
    NPN_GetStringIdentifiersProcPtr func = gNetscapeFuncs.getstringidentifiers;
    if(func)
    {
        (*func)(names, nameCount, identifiers);
    }
}

/******************************************************************************/
NPIdentifier NPN_GetIntIdentifier(int32_t intid)
{
    NPIdentifier retVal = 0;
    NPN_GetIntIdentifierProcPtr func = gNetscapeFuncs.getintidentifier;
    if(func)
    {
        retVal = (*func)(intid);
    }
    return retVal;
}

/******************************************************************************/
bool NPN_IdentifierIsString(NPIdentifier identifier)
{
    bool retVal = false;
    NPN_IdentifierIsStringProcPtr func = gNetscapeFuncs.identifierisstring;
    if(func)
    {
        retVal = (*func)(identifier);
    }
    return retVal;
}

/******************************************************************************/
NPUTF8 *NPN_UTF8FromIdentifier(NPIdentifier identifier)
{
    NPUTF8 * retVal = 0;
    NPN_UTF8FromIdentifierProcPtr func = gNetscapeFuncs.utf8fromidentifier;
    if(func)
    {
        retVal = (*func)(identifier);
    }
    return retVal;
}

/******************************************************************************/
int32_t NPN_IntFromIdentifier(NPIdentifier identifier)
{
    int32_t retVal = 0;
    NPN_IntFromIdentifierProcPtr func = gNetscapeFuncs.intfromidentifier;
    if(func)
    {
        retVal = (*func)(identifier);
    }
    return retVal;
}

/******************************************************************************/
NPObject *NPN_CreateObject(NPP npp, NPClass *aClass)
{
    NPObject * retVal = 0;
    NPN_CreateObjectProcPtr func = gNetscapeFuncs.createobject;
    if(func)
    {
        retVal = (*func)( npp, aClass);
    }
    return retVal;
}

/******************************************************************************/
NPObject *NPN_RetainObject(NPObject *obj)
{
    NPObject * retVal = 0;
    NPN_RetainObjectProcPtr func = gNetscapeFuncs.retainobject;
    if(func)
    {
        retVal = (*func)( obj);
    }
    return retVal;
}

/******************************************************************************/
void NPN_ReleaseObject(NPObject *obj)
{
    NPN_ReleaseObjectProcPtr func = gNetscapeFuncs.releaseobject;
    if(func)
    {
        (*func)(obj);
    }
}

/******************************************************************************/
bool NPN_Invoke(NPP npp, NPObject* obj, NPIdentifier methodName,
                    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
    bool retVal = false;
    NPN_InvokeProcPtr func = gNetscapeFuncs.invoke;
    if(func)
    {
        retVal = (*func)(npp, obj, methodName,  args, argCount, result);
    }
    return retVal;
}

/******************************************************************************/
bool NPN_InvokeDefault(NPP npp, NPObject* obj, const NPVariant *args,
                                           uint32_t argCount, NPVariant *result)
{
    bool retVal = false;
    NPN_InvokeDefaultProcPtr func = gNetscapeFuncs.invokeDefault;
    if(func)
    {
        retVal = (*func)(npp, obj,  args, argCount, result);
    }
    return retVal;
}

/******************************************************************************/
bool NPN_Evaluate(NPP npp, NPObject* obj, NPString *script, NPVariant *result)
{
    bool retVal = false;
    NPN_EvaluateProcPtr func = gNetscapeFuncs.evaluate;
    if(func)
    {
        retVal = (*func)(npp, obj, script, result);
    }
    return retVal;
}

/******************************************************************************/
bool NPN_GetProperty(NPP npp, NPObject* obj, NPIdentifier propertyName,
                                                              NPVariant *result)
{
    bool retVal = false;
    NPN_GetPropertyProcPtr func = gNetscapeFuncs.getproperty;
    if(func)
    {
       retVal = (*func)(npp, obj, propertyName, result);
    }
    return retVal;
}

/******************************************************************************/
bool NPN_SetProperty(NPP npp, NPObject* obj, NPIdentifier propertyName,
                                                         const NPVariant *value)
{
    bool retVal = false;
    NPN_SetPropertyProcPtr func = gNetscapeFuncs.setproperty;
    if(func)
    {
        retVal = (*func)(npp, obj, propertyName, value);
    }
    return retVal;
}

/******************************************************************************/
bool NPN_RemoveProperty(NPP npp, NPObject* obj, NPIdentifier propertyName)
{
    bool retVal = false;
    NPN_RemovePropertyProcPtr func = gNetscapeFuncs.removeproperty;
    if(func)
    {
        retVal = (*func)(npp, obj, propertyName);
    }
    return retVal;
}

/******************************************************************************/
bool NPN_HasProperty(NPP npp, NPObject* obj, NPIdentifier propertyName)
{
    bool retVal = false;
    NPN_HasPropertyProcPtr func = gNetscapeFuncs.hasproperty;
    if(func)
    {
        retVal = (*func)(npp, obj, propertyName);
    }
    return retVal;
}

/******************************************************************************/
bool NPN_HasMethod(NPP npp, NPObject* obj, NPIdentifier methodName)
{
    bool retVal = false;
    NPN_HasMethodProcPtr func = gNetscapeFuncs.hasmethod;
    if(func)
    {
        retVal = (*func)(npp, obj, methodName);
    }
    return retVal;
}

/******************************************************************************/
void NPN_ReleaseVariantValue(NPVariant *variant)
{
    NPN_ReleaseVariantValueProcPtr func = gNetscapeFuncs.releasevariantvalue;
    if(func)
    {
        (*func)(variant);
    }
}

/******************************************************************************/
void NPN_SetException(NPObject* obj, const NPUTF8 *message)
{
    NPN_SetExceptionProcPtr func = gNetscapeFuncs.setexception;
    if(func)
    {
        (*func)(obj, message);
    }
}

/******************************************************************************/
bool NPN_Enumerate(NPP npp, NPObject *obj, NPIdentifier **identifier,
                                                                uint32_t *count)
{
    NPN_EnumerateProcPtr func = gNetscapeFuncs.enumerate;
    bool retVal = false;
    if(func)
    {
        retVal = (*func)(npp, obj, identifier, count);
    }
    return retVal;
}

/******************************************************************************/
void NPN_PluginThreadAsyncCall(NPP instance, void (*func)(void *),
                                                                 void *userData)
{
    NPN_PluginThreadAsyncCallProcPtr func2
                                         = gNetscapeFuncs.pluginthreadasynccall;
    if(func2)
    {
        (*func2)(instance, func, userData);
    }
}

/******************************************************************************/
bool NPN_Construct(NPP npp, NPObject* obj, const NPVariant *args,
                                           uint32_t argCount, NPVariant *result)
{
    NPN_ConstructProcPtr func = gNetscapeFuncs.construct;
    bool retVal= false;
    if(func)
    {
        retVal = (*func)(npp, obj, args, argCount, result);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_GetValueForURL(NPP npp, NPNURLVariable variable, const char *url,
                                                    char **value, uint32_t *len)
{
    NPN_GetValueForURLPtr func = gNetscapeFuncs.getvalueforurl;
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    if(func)
    {
        retVal = (*func)(npp, variable, url, value, len);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_SetValueForURL(NPP npp, NPNURLVariable variable, const char *url,
                                                const char *value, uint32_t len)
{
    NPN_SetValueForURLPtr func = gNetscapeFuncs.setvalueforurl;
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    if(func)
    {
        retVal = (*func)(npp, variable, url, value, len);
    }
    return retVal;
}

/******************************************************************************/
NPError NPN_GetAuthenticationInfo(NPP npp, const char *protocol,
                                     const char *host, int32_t port,
                                     const char *scheme, const char *realm,
                                     char **username, uint32_t *ulen,
                                     char **password, uint32_t *plen)
{
    NPN_GetAuthenticationInfoPtr func = gNetscapeFuncs.getauthenticationinfo;
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    if(func)
    {
        retVal = (*func)(npp, protocol, host, port, scheme, realm, username,
                                                          ulen, password, plen);
    }
    return retVal;
}

/******************************************************************************/
uint32_t NPN_ScheduleTimer(NPP instance, uint32_t interval, NPBool repeat,
                                   void (*timerFunc)(NPP npp, uint32_t timerID))
{
    NPN_ScheduleTimerPtr func = gNetscapeFuncs.scheduletimer;
    uint32_t retVal = 0;
    if(func)
    {
        retVal = (*func)(instance, interval, repeat, timerFunc);
    }
    return retVal;
}

/******************************************************************************/
void NPN_UnscheduleTimer(NPP instance, uint32_t timerID)
{
    NPN_UnscheduleTimerPtr func = gNetscapeFuncs.unscheduletimer;
    if(func)
    {
        (*func)(instance, timerID);
    }
}

/******************************************************************************/
NPError NPN_PopUpContextMenu(NPP instance, NPMenu* menu)
{
    NPN_PopUpContextMenuPtr func = gNetscapeFuncs.popupcontextmenu;
    NPError retVal = NPERR_INVALID_FUNCTABLE_ERROR;
    if(func)
    {
        retVal = (*func)(instance, menu);
    }
    return retVal;
}

/******************************************************************************/
NPBool NPN_ConvertPoint(NPP instance, double sourceX, double sourceY,
                                   NPCoordinateSpace sourceSpace, double *destX,
                                     double *destY, NPCoordinateSpace destSpace)
{
    NPN_ConvertPointPtr func = gNetscapeFuncs.convertpoint;
    NPBool retVal = false;
    if(func)
    {
        retVal = (*func)(instance, sourceX, sourceY, sourceSpace, destX,
                                                              destY, destSpace);
    }
    return retVal;
}

/******************************************************************************/
NPBool NPN_HandleEvent(NPP instance, void *event, NPBool handled)
{
    NPN_HandleEventPtr func = gNetscapeFuncs.handleevent;
    NPBool retVal = false;
    if(func)
    {
        retVal = (*func)(instance, event, handled);
    }
    return retVal;
}

/******************************************************************************/
NPBool NPN_UnfocusInstance(NPP instance, NPFocusDirection direction)
{
    NPN_UnfocusInstancePtr func = gNetscapeFuncs.unfocusinstance;
    NPBool retVal = false;
    if(func)
    {
        retVal = (*func)(instance, direction);
    }
    return retVal;
}


/******************************************************************************/
void NPN_URLRedirectResponse(NPP instance, void* notifyData, NPBool allow)
{
    NPN_URLRedirectResponsePtr func = gNetscapeFuncs.urlredirectresponse;
    if(func)
    {
        (*func)(instance, notifyData, allow);
    }
}

/******************************************************************************/
NPError NPN_InitFuncTable(const NPNetscapeFuncs * nsTable)
{
    NPError err = NPERR_NO_ERROR;

    /* Zero everything */
    memset(&gNetscapeFuncs, 0, sizeof(gNetscapeFuncs));

    /* validate input parameters */
    if(nsTable != NULL)
    {
        uint32_t size;

        /*
        * Check the major version passed in Netscape's function table.
        */

        if ((nsTable->version >> 8) > NP_VERSION_MAJOR)
        {
            err = NPERR_INCOMPATIBLE_VERSION_ERROR;
        }

        if (nsTable->size > sizeof(NPNetscapeFuncs))
        {
            /* Looks like more functions provided than we know about..*/
            size = sizeof(NPNetscapeFuncs);
        }
        else
        {
            /* Copy across only those entries that were passed in (i.e. size) */
            size = nsTable->size;
        }

        memcpy(&gNetscapeFuncs, nsTable, size);
        gNetscapeFuncs.size = size;
    }
    else
    {
        err = NPERR_INVALID_FUNCTABLE_ERROR;
    }
    return err;
}
