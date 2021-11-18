/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
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
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef _NPN_FUNC_TAB_H_
#define _NPN_FUNCS_TAB_H_

//#include "npapi.h"
//#include "npruntime.h"


typedef NPError (*NPN_GetValueProcPtr)(NPP instance, NPNVariable variable,
                                                              void * retValue);

typedef NPError (*NPN_SetValueProcPtr)(NPP instance, NPPVariable variable,
                                                                   void *value);

typedef NPError (*NPN_GetURLNotifyProcPtr)(NPP instance, const char* url,
                                          const char* window, void* notifyData);

typedef NPError (*NPN_PostURLNotifyProcPtr)(NPP instance, const char* url,
                              const char* window, uint32_t len, const char* buf,
                                                 NPBool file, void* notifyData);

typedef NPError (*NPN_GetURLProcPtr)(NPP instance, const char* url,
                                                            const char* window);

typedef NPError (*NPN_PostURLProcPtr)(NPP instance, const char* url,
                const char* window, uint32_t len, const char* buf, NPBool file);

typedef NPError (*NPN_RequestReadProcPtr)(NPStream* stream,
                                                        NPByteRange* rangeList);

typedef NPError (*NPN_NewStreamProcPtr)(NPP instance, NPMIMEType type,
                                         const char* window, NPStream** stream);

typedef int32_t (*NPN_WriteProcPtr)(NPP instance, NPStream* stream,
                                                     int32_t len, void* buffer);

typedef NPError (*NPN_DestroyStreamProcPtr)(NPP instance, NPStream* stream,
                                                               NPReason reason);

typedef void (*NPN_StatusProcPtr)(NPP instance, const char * message);

typedef const char * (*NPN_UserAgentProcPtr)(NPP instance);

typedef void * (*NPN_MemAllocProcPtr)(uint32_t size);

typedef void (*NPN_MemFreeProcPtr)(void * ptr);

typedef uint32_t (*NPN_MemFlushProcPtr)(uint32_t size);

typedef void (*NPN_ReloadPluginsProcPtr)(NPBool reloadPages);

typedef void* (*NPN_GetJavaEnvProcPtr)(void);

typedef void* (*NPN_GetJavaPeerProcPtr)(NPP instance);

typedef void (*NPN_InvalidateRectProcPtr)(NPP instance, NPRect * rect);

typedef void (*NPN_InvalidateRegionProcPtr)(NPP instance, NPRegion region);

typedef void (*NPN_ForceRedrawProcPtr)(NPP instance);

typedef NPIdentifier (*NPN_GetStringIdentifierProcPtr)(const NPUTF8 * name);

typedef void (*NPN_GetStringIdentifiersProcPtr)(const NPUTF8 ** names,
                                  int32_t nameCount, NPIdentifier* identifiers);

typedef NPIdentifier (*NPN_GetIntIdentifierProcPtr)(int32_t intid);

typedef bool (*NPN_IdentifierIsStringProcPtr)(NPIdentifier identifier);

typedef NPUTF8* (*NPN_UTF8FromIdentifierProcPtr)(NPIdentifier identifier);

typedef int32_t (*NPN_IntFromIdentifierProcPtr)(NPIdentifier identifier);

typedef NPObject* (*NPN_CreateObjectProcPtr)(NPP npp, NPClass * aClass);

typedef NPObject* (*NPN_RetainObjectProcPtr)(NPObject * obj);

typedef void (*NPN_ReleaseObjectProcPtr)(NPObject * obj);

typedef bool (*NPN_InvokeProcPtr)(NPP npp, NPObject * obj,
                               NPIdentifier methodName, const NPVariant * args,
                                         uint32_t argCount, NPVariant * result);

typedef bool (*NPN_InvokeDefaultProcPtr)(NPP npp, NPObject * obj,
                 const NPVariant * args, uint32_t argCount, NPVariant * result);

typedef bool (*NPN_EvaluateProcPtr)(NPP npp, NPObject * obj, NPString * script,
                                                            NPVariant * result);

typedef bool (*NPN_GetPropertyProcPtr)(NPP npp, NPObject * obj,
                                 NPIdentifier propertyName, NPVariant * result);

typedef bool (*NPN_SetPropertyProcPtr)(NPP npp, NPObject * obj,
                            NPIdentifier propertyName, const NPVariant * value);

typedef bool (*NPN_RemovePropertyProcPtr)(NPP npp, NPObject * obj,
                                                     NPIdentifier propertyName);

typedef bool (*NPN_HasPropertyProcPtr)(NPP npp, NPObject * obj,
                                                     NPIdentifier propertyName);

typedef bool (*NPN_HasMethodProcPtr)(NPP npp, NPObject * obj,
                                                     NPIdentifier propertyName);

typedef void (*NPN_ReleaseVariantValueProcPtr)(NPVariant * variant);

typedef void (*NPN_SetExceptionProcPtr)(NPObject * obj, const NPUTF8 * message);

typedef void (*NPN_PushPopupsEnabledStateProcPtr)(NPP npp, NPBool enabled);

typedef void (*NPN_PopPopupsEnabledStateProcPtr)(NPP npp);

typedef bool (*NPN_EnumerateProcPtr)(NPP npp, NPObject * obj,
                                  NPIdentifier ** identifier, uint32_t * count);

typedef void (*NPN_PluginThreadAsyncCallProcPtr)(NPP instance,
                                         void (*func)(void *), void * userData);

typedef bool (*NPN_ConstructProcPtr)(NPP npp, NPObject * obj,
                 const NPVariant * args, uint32_t argCount, NPVariant * result);

typedef NPError (*NPN_GetValueForURLPtr)(NPP npp, NPNURLVariable variable,
                               const char * url, char ** value, uint32_t * len);

typedef NPError (*NPN_SetValueForURLPtr)(NPP npp, NPNURLVariable variable,
                            const char * url, const char * value, uint32_t len);

typedef NPError (*NPN_GetAuthenticationInfoPtr)(NPP npp, const char * protocol,
                          const char * host, int32_t port, const char * scheme,
                         const char * realm, char ** username, uint32_t * ulen,
                                             char ** password, uint32_t * plen);

typedef uint32_t (*NPN_ScheduleTimerPtr)(NPP instance, uint32_t interval,
                   NPBool repeat, void (*timerFunc)(NPP npp, uint32_t timerID));

typedef void (*NPN_UnscheduleTimerPtr)(NPP instance, uint32_t timerID);

typedef NPError (*NPN_PopUpContextMenuPtr)(NPP instance, NPMenu * menu);

typedef NPBool (*NPN_ConvertPointPtr)(NPP instance, double sourceX,
                     double sourceY, NPCoordinateSpace sourceSpace,
                   double * destX, double * destY, NPCoordinateSpace destSpace);

typedef NPBool (*NPN_HandleEventPtr)(NPP instance, void * event, NPBool handled);

typedef NPBool (*NPN_UnfocusInstancePtr)(NPP instance, NPFocusDirection direction);

typedef void (*NPN_URLRedirectResponsePtr)(NPP instance, void * notifyData,
                                                                  NPBool allow);

/******************************************************************************/
struct NPNetscapeFuncs_s
{
    uint16_t size;
    uint16_t version;
    NPN_GetURLProcPtr geturl;
    NPN_PostURLProcPtr posturl;
    NPN_RequestReadProcPtr requestread;
    NPN_NewStreamProcPtr newstream;
    NPN_WriteProcPtr write;
    NPN_DestroyStreamProcPtr destroystream;
    NPN_StatusProcPtr status;
    NPN_UserAgentProcPtr uagent;
    NPN_MemAllocProcPtr memalloc;
    NPN_MemFreeProcPtr memfree;
    NPN_MemFlushProcPtr memflush;
    NPN_ReloadPluginsProcPtr reloadplugins;
    NPN_GetJavaEnvProcPtr getJavaEnv;
    NPN_GetJavaPeerProcPtr getJavaPeer;
    NPN_GetURLNotifyProcPtr geturlnotify;
    NPN_PostURLNotifyProcPtr posturlnotify;
    NPN_GetValueProcPtr getvalue;
    NPN_SetValueProcPtr setvalue;
    NPN_InvalidateRectProcPtr invalidaterect;
    NPN_InvalidateRegionProcPtr invalidateregion;
    NPN_ForceRedrawProcPtr forceredraw;
    NPN_GetStringIdentifierProcPtr getstringidentifier;
    NPN_GetStringIdentifiersProcPtr getstringidentifiers;
    NPN_GetIntIdentifierProcPtr getintidentifier;
    NPN_IdentifierIsStringProcPtr identifierisstring;
    NPN_UTF8FromIdentifierProcPtr utf8fromidentifier;
    NPN_IntFromIdentifierProcPtr intfromidentifier;
    NPN_CreateObjectProcPtr createobject;
    NPN_RetainObjectProcPtr retainobject;
    NPN_ReleaseObjectProcPtr releaseobject;
    NPN_InvokeProcPtr invoke;
    NPN_InvokeDefaultProcPtr invokeDefault;
    NPN_EvaluateProcPtr evaluate;
    NPN_GetPropertyProcPtr getproperty;
    NPN_SetPropertyProcPtr setproperty;
    NPN_RemovePropertyProcPtr removeproperty;
    NPN_HasPropertyProcPtr hasproperty;
    NPN_HasMethodProcPtr hasmethod;
    NPN_ReleaseVariantValueProcPtr releasevariantvalue;
    NPN_SetExceptionProcPtr setexception;
    NPN_PushPopupsEnabledStateProcPtr pushpopupsenabledstate;
    NPN_PopPopupsEnabledStateProcPtr poppopupsenabledstate;
    NPN_EnumerateProcPtr enumerate;
    NPN_PluginThreadAsyncCallProcPtr pluginthreadasynccall;
    NPN_ConstructProcPtr construct;
    NPN_GetValueForURLPtr getvalueforurl;
    NPN_SetValueForURLPtr setvalueforurl;
    NPN_GetAuthenticationInfoPtr getauthenticationinfo;
    NPN_ScheduleTimerPtr scheduletimer;
    NPN_UnscheduleTimerPtr unscheduletimer;
    NPN_PopUpContextMenuPtr popupcontextmenu;
    NPN_ConvertPointPtr convertpoint;
    NPN_HandleEventPtr handleevent;
    NPN_UnfocusInstancePtr unfocusinstance;
    NPN_URLRedirectResponsePtr urlredirectresponse;
};

typedef struct NPNetscapeFuncs_s NPNetscapeFuncs;

void NPN_Version(int * netscape_major, int * netscape_minor);

NPError NPN_InitFuncTable(const NPNetscapeFuncs * nsTable);

#endif
