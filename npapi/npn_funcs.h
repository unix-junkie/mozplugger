/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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

#ifndef _NPN_FUNCS_H_
#define _NPN_FUNCS_H_

#include <stdint.h>
//#include "npapi.h"


#ifdef __cplusplus
extern "C" {
#endif

NPError NPN_GetURLNotify(NPP instance, const char* url,
                                       const char* target, void* notifyData);

NPError NPN_GetURL(NPP instance, const char* url, const char* target);

NPError NPN_PostURLNotify(NPP instance, const char* url,
                                        const char* target, uint32_t len,
                                        const char* buf, NPBool file,
                                        void* notifyData);

NPError NPN_PostURL(NPP instance, const char* url,
                                  const char* target, uint32_t len,
                                  const char* buf, NPBool file);

NPError NPN_RequestRead(NPStream* stream, NPByteRange* rangeList);

NPError NPN_NewStream(NPP instance, NPMIMEType type,
                                    const char* target, NPStream** stream);

int32_t NPN_Write(NPP instance, NPStream* stream, int32_t len,
                                void* buffer);

NPError NPN_DestroyStream(NPP instance, NPStream* stream,
                                        NPReason reason);

void NPN_Status(NPP instance, const char* message);

const char * NPN_UserAgent(NPP instance);

void * NPN_MemAlloc(uint32_t size);

void NPN_MemFree(void* ptr);

uint32_t NPN_MemFlush(uint32_t size);

void NPN_ReloadPlugins(NPBool reloadPages);

NPError NPN_GetValue(NPP instance, NPNVariable variable, void * value);

NPError NPN_SetValue(NPP instance, NPPVariable variable, void * value);

void NPN_InvalidateRect(NPP instance, NPRect * invalidRect);

void NPN_InvalidateRegion(NPP instance, NPRegion invalidRegion);

void NPN_ForceRedraw(NPP instance);

void NPN_PushPopupsEnabledState(NPP instance, NPBool enabled);

void NPN_PopPopupsEnabledState(NPP instance);

void NPN_PluginThreadAsyncCall(NPP instance, void (*func) (void *),
                                                               void * userData);

NPError NPN_GetValueForURL(NPP instance, NPNURLVariable variable,
                                         const char *url, char **value,
                                         uint32_t *len);

NPError NPN_SetValueForURL(NPP instance, NPNURLVariable variable,
                                         const char *url, const char *value,
                                         uint32_t len);

NPError NPN_GetAuthenticationInfo(NPP instance,
                                                const char *protocol,
                                                const char *host, int32_t port,
                                                const char *scheme,
                                                const char *realm,
                                                char **username, uint32_t *ulen,
                                                char **password,
                                                uint32_t *plen);

uint32_t NPN_ScheduleTimer(NPP instance, uint32_t interval, NPBool repeat,
                                  void (*timerFunc)(NPP npp, uint32_t timerID));

void NPN_UnscheduleTimer(NPP instance, uint32_t timerID);

NPError NPN_PopUpContextMenu(NPP instance, NPMenu* menu);

NPBool NPN_ConvertPoint(NPP instance, double sourceX, double sourceY,
                                  NPCoordinateSpace sourceSpace, double *destX,
                                   double *destY, NPCoordinateSpace destSpace);

NPBool NPN_HandleEvent(NPP instance, void *event, NPBool handled);

NPBool NPN_UnfocusInstance(NPP instance, NPFocusDirection direction);

void NPN_URLRedirectResponse(NPP instance, void* notifyData, NPBool allow);

#ifdef __cplusplus
}  /* end extern "C" */
#endif

#endif
