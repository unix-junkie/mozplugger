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

#ifndef _NPP_FUNC_TAB_H_
#define _NPP_FUNC_TAB_H_

//#include "npapi.h"
//#include "npruntime.h"


typedef NPError (*NPP_NewProcPtr)(NPMIMEType pluginType, NPP instance,
                                     uint16_t mode, int16_t argc, char * argn[],
                                            char * argv[], NPSavedData * saved);

typedef NPError (*NPP_DestroyProcPtr)(NPP instance, NPSavedData ** save);

typedef NPError (*NPP_SetWindowProcPtr)(NPP instance, NPWindow * window);

typedef NPError (*NPP_NewStreamProcPtr)(NPP instance, NPMIMEType type,
                          NPStream * stream, NPBool seekable, uint16_t * stype);

typedef NPError (*NPP_DestroyStreamProcPtr)(NPP instance, NPStream * stream,
                                                               NPReason reason);

typedef int32_t (*NPP_WriteReadyProcPtr)(NPP instance, NPStream * stream);

typedef int32_t (*NPP_WriteProcPtr)(NPP instance, NPStream * stream,
                                    int32_t offset, int32_t len, void * buffer);

typedef void (*NPP_StreamAsFileProcPtr)(NPP instance, NPStream * stream,
                                                            const char * fname);

typedef void (*NPP_PrintProcPtr)(NPP instance, NPPrint * platformPrint);

typedef int16_t (*NPP_HandleEventProcPtr)(NPP instance, void * event);

typedef void (*NPP_URLNotifyProcPtr)(NPP instance, const char * url,
                                            NPReason reason, void * notifyData);

typedef NPError (*NPP_GetValueProcPtr)(NPP instance, NPPVariable variable,
                                                              void * retValue);

typedef NPError (*NPP_SetValueProcPtr)(NPP instance, NPNVariable variable,
                                                                  void * value);

typedef NPBool (*NPP_GotFocusPtr)(NPP instance, NPFocusDirection direction);

typedef void (*NPP_LostFocusPtr)(NPP instance);

typedef void (*NPP_URLRedirectNotifyPtr)(NPP instance, const char * url,
                                             int32_t status, void * notifyData);

typedef NPError (*NPP_ClearSiteDataPtr)(const char * site, uint64_t flags,
                                                               uint64_t maxAge);

typedef char ** (*NPP_GetSitesWithDataPtr)(void);


/******************************************************************************/
struct NPPluginFuncs_s
{
    uint16_t size;
    uint16_t version;
    NPP_NewProcPtr newp;
    NPP_DestroyProcPtr destroy;
    NPP_SetWindowProcPtr setwindow;
    NPP_NewStreamProcPtr newstream;
    NPP_DestroyStreamProcPtr destroystream;
    NPP_StreamAsFileProcPtr asfile;
    NPP_WriteReadyProcPtr writeready;
    NPP_WriteProcPtr write;
    NPP_PrintProcPtr print;
    NPP_HandleEventProcPtr event;
    NPP_URLNotifyProcPtr urlnotify;
    void * javaClass;
    NPP_GetValueProcPtr getvalue;
    NPP_SetValueProcPtr setvalue;
    NPP_GotFocusPtr gotfocus;
    NPP_LostFocusPtr lostfocus;
    NPP_URLRedirectNotifyPtr urlredirectnotify;
    NPP_ClearSiteDataPtr clearsitedata;
    NPP_GetSitesWithDataPtr getsiteswithdata;
};

typedef struct NPPluginFuncs_s NPPluginFuncs;

void NPP_Version(int* plugin_major, int* plugin_minor);

NPError NPP_InitFuncTable(NPPluginFuncs * pluginFuncs);

#endif
