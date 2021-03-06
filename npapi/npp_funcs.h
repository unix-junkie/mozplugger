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

#ifndef _NPP_FUNCS_H_
#define _NPP_FUNCS_H_

#ifdef __cplusplus
extern "C" {
#endif

NPError NPP_New(NPMIMEType pluginType, NPP instance,
                          uint16_t mode, int16_t argc, char* argn[],
                          char* argv[], NPSavedData* saved);

NPError NPP_Destroy(NPP instance, NPSavedData** save);

NPError NPP_SetWindow(NPP instance, NPWindow* window);

NPError NPP_NewStream(NPP instance, NPMIMEType type,
                                NPStream* stream, NPBool seekable, uint16_t* stype);

NPError NPP_DestroyStream(NPP instance, NPStream* stream, NPReason reason);

int32_t NPP_WriteReady(NPP instance, NPStream* stream);

int32_t NPP_Write(NPP instance, NPStream* stream, int32_t offset,
                            int32_t len, void* buffer);

void NPP_StreamAsFile(NPP instance, NPStream* stream, const char* fname);

void NPP_Print(NPP instance, NPPrint* platformPrint);

int16_t NPP_HandleEvent(NPP instance, void* event);

void NPP_URLNotify(NPP instance, const char* url, NPReason reason, void* notifyData);

NPError NPP_GetValue(NPP instance, NPPVariable variable, void *value);

NPError NPP_SetValue(NPP instance, NPNVariable variable, void *value);

NPBool NPP_GotFocus(NPP instance, NPFocusDirection direction);

void NPP_LostFocus(NPP instance);

void NPP_URLRedirectNotify(NPP instance, const char* url, int32_t status, void* notifyData);

NPError NPP_ClearSiteData(const char* site, uint64_t flags, uint64_t maxAge);

char ** NPP_GetSitesWithData(void);

#ifdef __cplusplus
} /* end extern "C" */
#endif

#endif
