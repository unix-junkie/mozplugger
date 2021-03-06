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

#ifndef npapi_h_
#define npapi_h_

#include <stdint.h>

/*----------------------------------------------------------------------*/
/*                        Plugin Version Constants                      */
/*----------------------------------------------------------------------*/

#define NP_VERSION_MAJOR 0
#define NP_VERSION_MINOR 27

/*----------------------------------------------------------------------*/
/*                       Definition of Basic Types                      */
/*----------------------------------------------------------------------*/

typedef unsigned char NPBool;
typedef int16_t       NPError;
typedef int16_t       NPReason;
typedef char*         NPMIMEType;

/*----------------------------------------------------------------------*/
/*                       Structures and definitions                     */
/*----------------------------------------------------------------------*/

/*
 *  NPP is a plug-in's opaque instance handle
 */
typedef struct _NPP
{
  void* pdata;      /* plug-in private data */
  void* ndata;      /* netscape private data */
} NPP_t;

typedef NPP_t*  NPP;

typedef struct _NPStream
{
  void*    pdata; /* plug-in private data */
  void*    ndata; /* netscape private data */
  const    char* url;
  uint32_t end;
  uint32_t lastmodified;
  void*    notifyData;
  const    char* headers; /* Response headers from host.
                           * Exists only for >= NPVERS_HAS_RESPONSE_HEADERS.
                           * Used for HTTP only; NULL for non-HTTP.
                           * Available from NPP_NewStream onwards.
                           * Plugin should copy this data before storing it.
                           * Includes HTTP status line and all headers,
                           * preferably verbatim as received from server,
                           * headers formatted as in HTTP ("Header: Value"),
                           * and newlines (\n, NOT \r\n) separating lines.
                           * Terminated by \n\0 (NOT \n\n\0). */
} NPStream;

typedef struct _NPByteRange
{
  int32_t  offset; /* negative offset means from the end */
  uint32_t length;
  struct _NPByteRange* next;
} NPByteRange;

typedef struct _NPSavedData
{
  int32_t len;
  void*   buf;
} NPSavedData;

typedef struct _NPRect
{
  uint16_t top;
  uint16_t left;
  uint16_t bottom;
  uint16_t right;
} NPRect;

typedef struct _NPSize
{
  int32_t width;
  int32_t height;
} NPSize;

typedef enum
{
  NPFocusNext = 0,
  NPFocusPrevious = 1
} NPFocusDirection;

/* Return values for NPP_HandleEvent */
#define kNPEventNotHandled 0
#define kNPEventHandled 1
/* Exact meaning must be spec'd in event model. */
#define kNPEventStartIME 2

#if defined(XP_UNIX)
/*
 * Unix specific structures and definitions
 */

/*
 * Callback Structures.
 *
 * These are used to pass additional platform specific information.
 */
enum
{
  NP_SETWINDOW = 1,
  NP_PRINT
};

typedef struct
{
  int32_t type;
} NPAnyCallbackStruct;

typedef struct
{
  int32_t      type;
  Display*     display;
  Visual*      visual;
  Colormap     colormap;
  unsigned int depth;
} NPSetWindowCallbackStruct;

typedef struct
{
  int32_t type;
  FILE* fp;
} NPPrintCallbackStruct;

#endif /* XP_UNIX */


/*
 *   The following masks are applied on certain platforms to NPNV and
 *   NPPV selectors that pass around pointers to COM interfaces. Newer
 *   compilers on some platforms may generate vtables that are not
 *   compatible with older compilers. To prevent older plugins from
 *   not understanding a new browser's ABI, these masks change the
 *   values of those selectors on those platforms. To remain backwards
 *   compatible with different versions of the browser, plugins can
 *   use these masks to dynamically determine and use the correct C++
 *   ABI that the browser is expecting. This does not apply to Windows
 *   as Microsoft's COM ABI will likely not change.
 */

#define NP_ABI_GCC3_MASK  0x10000000
/*
 *   gcc 3.x generated vtables on UNIX and OSX are incompatible with
 *   previous compilers.
 */
#if (defined(XP_UNIX) && defined(__GNUC__) && (__GNUC__ >= 3))
#define _NP_ABI_MIXIN_FOR_GCC3 NP_ABI_GCC3_MASK
#else
#define _NP_ABI_MIXIN_FOR_GCC3 0
#endif

#define _NP_ABI_MIXIN_FOR_MACHO 0

#define NP_ABI_MASK (_NP_ABI_MIXIN_FOR_GCC3 | _NP_ABI_MIXIN_FOR_MACHO)

/*
 * List of variable names for which NPP_GetValue shall be implemented
 */
typedef enum
{
  NPPVpluginNameString = 1,
  NPPVpluginDescriptionString,
  NPPVpluginWindowBool,
  NPPVpluginTransparentBool,
  NPPVjavaClass,
  NPPVpluginWindowSize,
  NPPVpluginTimerInterval,
  NPPVpluginScriptableInstance = (10 | NP_ABI_MASK),
  NPPVpluginScriptableIID = 11,

  /* Introduced in Mozilla 0.9.9 */
  NPPVjavascriptPushCallerBool = 12,

  /* Introduced in Mozilla 1.0 */
  NPPVpluginKeepLibraryInMemory = 13,
  NPPVpluginNeedsXEmbed         = 14,

  /* Get the NPObject for scripting the plugin. Introduced in Firefox
   * 1.0 (NPAPI minor version 14).
   */
  NPPVpluginScriptableNPObject  = 15,

  /* Get the plugin value (as \0-terminated UTF-8 string data) for
   * form submission if the plugin is part of a form. Use
   * NPN_MemAlloc() to allocate memory for the string data. Introduced
   * in NPAPI minor version 15.
   */
  NPPVformValue = 16,

  NPPVpluginUrlRequestsDisplayedBool = 17,

  /* Checks if the plugin is interested in receiving the http body of
   * all http requests (including failed ones, http status != 200).
   */
  NPPVpluginWantsAllNetworkStreams = 18,

  /* Browsers can retrieve a native ATK accessibility plug ID via this variable. */
  NPPVpluginNativeAccessibleAtkPlugId = 19,

  /* Checks to see if the plug-in would like the browser to load the "src" attribute. */
  NPPVpluginCancelSrcStream = 20,

  NPPVsupportsAdvancedKeyHandling = 21,

  NPPVpluginUsesDOMForCursorBool = 22



#ifdef MOZ_PLATFORM_HILDON
  , NPPVpluginWindowlessLocalBool = 2002
#endif
} NPPVariable;

/*
 * List of variable names for which NPN_GetValue should be implemented.
 */
typedef enum
{
  NPNVxDisplay = 1,
  NPNVxtAppContext,
  NPNVnetscapeWindow,
  NPNVjavascriptEnabledBool,
  NPNVasdEnabledBool,
  NPNVisOfflineBool,

  /* 10 and over are available on Mozilla builds starting with 0.9.4 */
  NPNVserviceManager = (10 | NP_ABI_MASK),
  NPNVDOMElement     = (11 | NP_ABI_MASK),
  NPNVDOMWindow      = (12 | NP_ABI_MASK),
  NPNVToolkit        = (13 | NP_ABI_MASK),
  NPNVSupportsXEmbedBool = 14,

  /* Get the NPObject wrapper for the browser window. */
  NPNVWindowNPObject = 15,

  /* Get the NPObject wrapper for the plugins DOM element. */
  NPNVPluginElementNPObject = 16,

  NPNVSupportsWindowless = 17,

  NPNVprivateModeBool = 18,

  NPNVsupportsAdvancedKeyHandling = 21,

  NPNVdocumentOrigin = 22

#ifdef MOZ_PLATFORM_HILDON
  , NPNVSupportsWindowlessLocal = 2002
#endif
} NPNVariable;

typedef enum
{
  NPNURLVCookie = 501,
  NPNURLVProxy
} NPNURLVariable;
/*
 * The type of Toolkit the widgets use
 */
typedef enum
{
  NPNVGtk12 = 1,
  NPNVGtk2
} NPNToolkitType;

/*
 * The type of a NPWindow - it specifies the type of the data structure
 * returned in the window field.
 */
typedef enum
{
  NPWindowTypeWindow = 1,
  NPWindowTypeDrawable
} NPWindowType;

typedef struct _NPWindow
{
  void* window;  /* Platform specific window handle */
                 /* OS/2: x - Position of bottom left corner */
                 /* OS/2: y - relative to visible netscape window */
  int32_t  x;      /* Position of top left corner relative */
  int32_t  y;      /* to a netscape page. */
  uint32_t width;  /* Maximum window size */
  uint32_t height;
  NPRect   clipRect; /* Clipping rectangle in port coordinates */
#if defined(XP_UNIX)
  void * ws_info; /* Platform-dependent additional data */
#endif /* XP_UNIX */
  NPWindowType type; /* Is this a window or a drawable? */
} NPWindow;

typedef struct _NPImageExpose
{
  char*    data;       /* image pointer */
  int32_t  stride;     /* Stride of data image pointer */
  int32_t  depth;      /* Depth of image pointer */
  int32_t  x;          /* Expose x */
  int32_t  y;          /* Expose y */
  uint32_t width;      /* Expose width */
  uint32_t height;     /* Expose height */
  NPSize   dataSize;   /* Data buffer size */
  float    translateX; /* translate X matrix value */
  float    translateY; /* translate Y matrix value */
  float    scaleX;     /* scale X matrix value */
  float    scaleY;     /* scale Y matrix value */
} NPImageExpose;

typedef struct _NPFullPrint
{
  NPBool pluginPrinted;/* Set TRUE if plugin handled fullscreen printing */
  NPBool printOne;     /* TRUE if plugin should print one copy to default
                          printer */
  void* platformPrint; /* Platform-specific printing info */
} NPFullPrint;

typedef struct _NPEmbedPrint
{
  NPWindow window;
  void* platformPrint; /* Platform-specific printing info */
} NPEmbedPrint;

typedef struct _NPPrint
{
  uint16_t mode;               /* NP_FULL or NP_EMBED */
  union
  {
    NPFullPrint fullPrint;   /* if mode is NP_FULL */
    NPEmbedPrint embedPrint; /* if mode is NP_EMBED */
  } print;
} NPPrint;

typedef void*  NPEvent;

typedef void *NPRegion;

typedef struct _NPNSString NPNSString;
typedef struct _NPNSWindow NPNSWindow;
typedef struct _NPNSMenu   NPNSMenu;

typedef void *NPMenu;

typedef enum
{
  NPCoordinateSpacePlugin = 1,
  NPCoordinateSpaceWindow,
  NPCoordinateSpaceFlippedWindow,
  NPCoordinateSpaceScreen,
  NPCoordinateSpaceFlippedScreen
} NPCoordinateSpace;

/*
 * Values for mode passed to NPP_New:
 */
#define NP_EMBED 1
#define NP_FULL  2

/*
 * Values for stream type passed to NPP_NewStream:
 */
#define NP_NORMAL     1
#define NP_SEEK       2
#define NP_ASFILE     3
#define NP_ASFILEONLY 4

#define NP_MAXREADY (((unsigned)(~0)<<1)>>1)


/*----------------------------------------------------------------------*/
/*       Error and Reason Code definitions                              */
/*----------------------------------------------------------------------*/

/*
 * Values of type NPError:
 */
#define NPERR_BASE                         0
#define NPERR_NO_ERROR                    (NPERR_BASE + 0)
#define NPERR_GENERIC_ERROR               (NPERR_BASE + 1)
#define NPERR_INVALID_INSTANCE_ERROR      (NPERR_BASE + 2)
#define NPERR_INVALID_FUNCTABLE_ERROR     (NPERR_BASE + 3)
#define NPERR_MODULE_LOAD_FAILED_ERROR    (NPERR_BASE + 4)
#define NPERR_OUT_OF_MEMORY_ERROR         (NPERR_BASE + 5)
#define NPERR_INVALID_PLUGIN_ERROR        (NPERR_BASE + 6)
#define NPERR_INVALID_PLUGIN_DIR_ERROR    (NPERR_BASE + 7)
#define NPERR_INCOMPATIBLE_VERSION_ERROR  (NPERR_BASE + 8)
#define NPERR_INVALID_PARAM               (NPERR_BASE + 9)
#define NPERR_INVALID_URL                 (NPERR_BASE + 10)
#define NPERR_FILE_NOT_FOUND              (NPERR_BASE + 11)
#define NPERR_NO_DATA                     (NPERR_BASE + 12)
#define NPERR_STREAM_NOT_SEEKABLE         (NPERR_BASE + 13)
#define NPERR_TIME_RANGE_NOT_SUPPORTED    (NPERR_BASE + 14)
#define NPERR_MALFORMED_SITE              (NPERR_BASE + 15)

/*
 * Values of type NPReason:
 */
#define NPRES_BASE          0
#define NPRES_DONE         (NPRES_BASE + 0)
#define NPRES_NETWORK_ERR  (NPRES_BASE + 1)
#define NPRES_USER_BREAK   (NPRES_BASE + 2)

/*
 * Don't use these obsolete error codes any more.
 */
#define NP_NOERR  NP_NOERR_is_obsolete_use_NPERR_NO_ERROR
#define NP_EINVAL NP_EINVAL_is_obsolete_use_NPERR_GENERIC_ERROR
#define NP_EABORT NP_EABORT_is_obsolete_use_NPRES_USER_BREAK

/*
 * Version feature information
 */
#define NPVERS_HAS_STREAMOUTPUT             8
#define NPVERS_HAS_NOTIFICATION             9
#define NPVERS_HAS_LIVECONNECT              9
#define NPVERS_68K_HAS_LIVECONNECT          11
#define NPVERS_HAS_WINDOWLESS               11
#define NPVERS_HAS_XPCONNECT_SCRIPTING      13
#define NPVERS_HAS_NPRUNTIME_SCRIPTING      14
#define NPVERS_HAS_FORM_VALUES              15
#define NPVERS_HAS_POPUPS_ENABLED_STATE     16
#define NPVERS_HAS_RESPONSE_HEADERS         17
#define NPVERS_HAS_NPOBJECT_ENUM            18
#define NPVERS_HAS_PLUGIN_THREAD_ASYNC_CALL 19
#define NPVERS_HAS_ALL_NETWORK_STREAMS      20
#define NPVERS_HAS_URL_AND_AUTH_INFO        21
#define NPVERS_HAS_PRIVATE_MODE             22
#define NPVERS_MACOSX_HAS_COCOA_EVENTS      23
#define NPVERS_HAS_ADVANCED_KEY_HANDLING    25
#define NPVERS_HAS_URL_REDIRECT_HANDLING    26
#define NPVERS_HAS_CLEAR_SITE_DATA          27

#endif /* npapi_h_ */
