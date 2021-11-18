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

#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <utime.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef HAVE_GETPWUID
#include <pwd.h> /* For alternative way to find HOME dir */
#endif

#define XP_UNIX
#include "npapi.h"
#include "npruntime.h"
#include "npn_func_tab.h"
#include "npp_func_tab.h"
#include "npn_funcs.h"
#include "npp_funcs.h"
#include "np_funcs.h"

#include "mozplugger.h"
#include "debug.h"
#include "npn-get-helpers.h"
#include "cmd_flags.h"
#include "scriptable_obj.h"
#include "pipe_msg.h"

#ifndef __GNUC__
#define __inline
#endif

#define AUTO_UPDATE
#define CHUNK_SIZE (8192)

/**
 * Element of linked list of commands created when parsing config file
 */
typedef struct command
{
     int flags;
     const char * cmd;
     const char * winname;
     const char * fmatchStr;

     struct command * pNext;
} command_t;


/**
 * Element of fixed size array created when parsing NPP_New call
 */
typedef struct argument
{
     char *name;
     char *value;
} argument_t;

/**
 * Data associated with an instance of an embed object, can be more than
 * one
 */
typedef struct data
{
     Display * display;
     Window window;
     uint32_t width;
     uint32_t height;
     pid_t pid;
     int commsPipeFd;
     int repeats;
     command_t * command;     /**< command to execute */
     unsigned int mode_flags; /**< flags associated with browser calls */
     char *mimetype;
     char *href;               /**< If QT this is set to handle special case */
     char *url;                /**< The URL */
     char browserCantHandleIt; /**< Is set if browser cant handle protocol */
     char *urlFragment;

     int tmpFileFd;     /**< File descriptor of temp file */
     const char * tmpFileName; /**< Name of the temp file */
     int tmpFileSize;   /**< Size of temp file so far */

     char autostart;
     char autostartNotSeen;
     int num_arguments;
     struct argument *args;
} data_t;

/**
 * Element of linked list of mimetypes created when parsing config file
 */
typedef struct mimetype
{
     const char * type;

     struct mimetype * pNext;
} mimetype_t;

/**
 * Element of linked list of handlers created when parsing config file
 */
typedef struct handle
{
     mimetype_t * types;
     command_t * cmds;

     struct handle * pNext;
} handler_t;

/**
 * Global variables
 */

static char errMsg[512] = {0};
static handler_t * g_handlers = 0;

static const char * g_pluginName = "MozPlugger dummy Plugin";
static const char * g_version = VERSION;
static const char * g_linker = NULL;
static const char * g_controller = NULL;
static const char * g_helper = NULL;

static char staticPool[MAX_STATIC_MEMORY_POOL];
static int staticPoolIdx = 0;

/**
 * Wrapper for putenv(). Instead of writing to the envirnoment, the envirnoment
 * variables are written to a buffer.
 *
 * @param[in,out] buffer The buffer where the environment variables are written
 * @param[in] bufLen The length of the buffer
 * @param[in] offset The current position in the buffer
 * @param[in] var The name of the environment variable
 * @param[in] value The value of the environment variable
 *
 * @return The new offset
 */
static int my_putenv(char *buffer, int bufLen, int offset, const char *var,
                                                 const char *value)
{
     if(value)
     {
          const int len = strlen(var) + strlen(value) + 2;
          if (offset + len >= bufLen)
          {
               D("Buffer overflow in putenv(%s=%s) offset=%i, bufLen=%i\n",
                                                   var, value, offset, bufLen);
          }
          else
          {
               snprintf(&buffer[offset], len, "%s=%s", var, value);
               putenv(&buffer[offset]);
               offset += len;
          }
     }
     else
     {
          D("putenv did nothing, no value for %s\n", var);
     }
     return offset;
}

/**
 * putenv with a unsigned value
 *
 * @param[in,out] buffer The buffer where the environment variables are written
 * @param[in] bufLen The length of the buffer
 * @param[in] offset The current position in the buffer
 * @param[in] var The name of the environment variable
 * @param[in] value The value of the environment variable
 *
 * @return The new offset
 */
static int my_putenv_unsigned(char *buffer, int bufLen, int offset,
                                          const char *var, unsigned long value)
{
     char temp[50];
     snprintf(temp, sizeof(temp), "%lu", value);
     return my_putenv(buffer, bufLen, offset, var, temp);
}

/**
 * putenv with a hex value
 *
 * @param[in,out] buffer The buffer where the environment variables are written
 * @param[in] bufLen The length of the buffer
 * @param[in] offset The current position in the buffer
 * @param[in] var The name of the environment variable
 * @param[in] value The value of the environment variable
 *
 * @return The new offset
 */
static int my_putenv_hex(char *buffer, int bufLen, int offset,
                                          const char *var, unsigned long value)
{
     char temp[50];
     snprintf(temp, sizeof(temp), "0x%lx", value);
     return my_putenv(buffer, bufLen, offset, var, temp);
}

/**
 * putenv with a string
 *
 * @param[in,out] buffer The buffer where the environment variables are written
 * @param[in] bufLen The length of the buffer
 * @param[in] offset The current position in the buffer
 * @param[in] var The name of the environment variable
 * @param[in] value The value of the environment variable
 *
 * @return The new offset
 */
static int my_putenv_signed(char *buffer, int bufLen, int offset,
                                                   const char *var, long value)
{
     char temp[50];
     snprintf(temp, sizeof(temp), "%ld", value);
     return my_putenv(buffer, bufLen, offset, var, temp);
}

/**
 * Report the error
 *
 * @param[in] fmt The format string
 */
static void reportError(NPP instance, const char * fmt, ...)
{
     va_list ap;
     va_start(ap, fmt);

     vsnprintf(errMsg, sizeof(errMsg), fmt, ap);
     va_end(ap);

     if(instance)
     {
          NPN_Status(instance, errMsg);
     }
     fprintf(stderr, "%s\n",errMsg);
     D("%s\n", errMsg);
}

static void clearError(void)
{
     errMsg[0] = 0;
}

static bool haveError(void)
{
     return errMsg[0] != 0;
}

/**
 * Allocate some memory from the static pool. We use a static pool for
 * the database because Mozilla can unload the plugin after use and this
 * would lead to memory leaks if we use the heap.
 *
 * @param[in] size The size of the memory to allocate
 *
 * @return Pointer to the memory allocated or NULL
 */
static void * allocStaticMem(int size)
{
     void * retVal = NULL;
     const int newIdx = staticPoolIdx + size;

     if(newIdx > MAX_STATIC_MEMORY_POOL)
     {
          reportError(NULL, "MozPlugger: config file is too big - delete some handlers/commands or mimetypes");
     }
     else
     {
          retVal = &staticPool[staticPoolIdx];
          staticPoolIdx = newIdx;
     }
     return retVal;
}

/**
 * Make a dynamic string static by copying to static memory.
 * Given a pointer to a string in temporary memory, return the same string
 * but this time stored in permanent (i.e. static) memory. Will only be deleted
 * when the plugin is unloaded by Mozilla.
 *
 * @param[in] str Pointer to the string
 * @param[in] len The length of the string
 *
 * @return Pointer to the copied string
 */
static const char * makeStrStatic(const char * str, int len)
{
     /* plus one for string terminator */
     char * const buf = allocStaticMem(len + 1);

     if(buf)
     {
          strncpy(buf, str, len);
          buf[len] = '\0';
     }
     return buf;
}

/**
 * Search backwards from end of string buffer until first non-white space
 * and then terminate string after that point to perform a trim.
 *
 * @param[in,out] buf The string to be trimmed.
 */
static void trim_trailing_spaces(char * buf)
{
     char * end = &buf[strlen(buf)-1];
     for( ;end >= buf; end--)
     {
          if((*end != '\r') && (*end != '\n') && (*end != '\t') && (*end != ' '))
          {
               end[1] = '\0';
               return;
          }
     }
}

/**
 * Trim the line buffer and if the line is empty or contains just a comment
 * return false
 *
 * @param[in] buffer The line read
 *
 * @return true if not a blank line
 */
static bool chkCfgLine(char * buffer)
{
     if(buffer[0] != '#')
     {
          trim_trailing_spaces(buffer);
          return true;
     }
     return false;
}

static bool is_base_mozplugger(const char * magic)
{
     return (magic[0] == '-');
}

/**
 * Get the home directory
 *
 * @return a pointer to const string that contains home directory
 */
static const char * get_home_dir(void)
{
     const char * home = getenv("HOME");
#ifdef HAVE_GETPWUID
     if(home == NULL)
     {
          struct passwd * pw = getpwuid(getuid());
          home = pw->pw_dir;
     }
#endif
     return home;
}

/**
 * Get the prefix to the path to the config files based on the magic
 *
 * @param[in] magic references for this particular plugin type
 * @param[out] buf The buffer to put the prefix
 * @param[in] bufLen The length of the buffer
 *
 * @return the sizeof the resulting string
 */
static int get_cfg_path_prefix(const char * magic, char * buf, int bufLen)
{
     const char * fmt;
     int prefixLen = 1;
     const char * home;

     if(is_base_mozplugger(magic))
     {
          magic = "0";
     }
     else
     {
          prefixLen = strchr(magic, ':') - magic;
     }

     /* Locations are ...
      * $MOZPLUGGER_HOME/.cache/
      * $XDG_CACHE_HOME/mozplugger/
      * $HOME/.cache/mozplugger/
      */

     if( (home = getenv("MOZPLUGGER_HOME")) != NULL)
     {
          fmt = "%s/.cache/%.*s";
     }
     else if( (home = getenv("XDG_CACHE_HOME")) != NULL)
     {
          fmt = "%s/mozplugger/%.*s";
     }
     else if( (home = get_home_dir()) != NULL)
     {
          fmt = "%s/.cache/mozplugger/%.*s";
     }
     else
     {
          reportError(NULL, "Mozplugger cannot determine HOME directory\n");
          *buf = '\0';
          return 0;
     }
     return snprintf(buf, bufLen, fmt, home, prefixLen, magic);
}

/**
 * Get the paths to the mozplugger helpers.
 *
 * @param[in] magic references for this particular plugin type
 *
 * @return None
 */
static void get_helper_paths(const char * magic)
{
     char fname[200];
     FILE * fp;
     int n;

     if(g_controller || g_linker || g_helper)
         return;

     n = get_cfg_path_prefix(magic, fname, sizeof(fname));
     strncat(fname, ".helpers", sizeof(fname) - n);

     if((fp = fopen(fname, "rb")) != NULL)
     {
          char buffer[512];
          while(fgets(buffer, sizeof(buffer), fp))
          {
               if(chkCfgLine(buffer))
               {
                    char * sep = strchr(buffer, '\t');
                    int pathLen = strlen(&sep[1]);
                    *sep = '\0';
                    if(strcmp(buffer, "linker") == 0)
                    {
                         g_linker = makeStrStatic(&sep[1], pathLen);
                    }
                    else if(strcmp(buffer, "controller") == 0)
                    {
                         g_controller = makeStrStatic(&sep[1], pathLen);
                    }
                    else if(strcmp(buffer, "version") == 0)
                    {
                         g_version = makeStrStatic(&sep[1], pathLen);
                    }
                    else if(strcmp(buffer, "name") == 0)
                    {
                         g_pluginName = makeStrStatic(&sep[1], pathLen);
                    }
                    else if(strcmp(buffer, "helper") == 0)
                    {
                         g_helper = makeStrStatic(&sep[1], pathLen);
                    }
               }
          }
          fclose(fp);
     }
}

/**
 * Get the path to the configuration file.
 *
 * @param[in] magic references for this particular plugin type
 *
 * @return The full path to the configuration file
 */
static char * get_cmds_cfg_path(const char * magic)
{
     char fname[200];

     int n = get_cfg_path_prefix(magic, fname, sizeof(fname));
     strncat(fname, ".cmds", sizeof(fname) - n);

     return strdup(fname);
}

/**
 * Get the path to the mimetypes file.
 *
 * @param[in] magic references for this particular plugin type
 *
 * @return The full path to the configuration file
 */
static char * get_mimetypes_cfg_path(const char * magic)
{
     char fname[200];

     int n = get_cfg_path_prefix(magic, fname, sizeof(fname));
     strncat(fname, ".mimetypes", sizeof(fname) - n);

     return strdup(fname);
}


/**
 * Wrapper for execlp() that calls the helper.
 *
 * WARNING: This function runs in the daughter process so one must assume the
 * daughter uses a copy (including) heap memory of the parent's memory space
 * i.e. any write to memory here does not affect the parent memory space.
 * Since Linux uses copy-on-write, best leave memory read-only and once execlp
 * is called, all daughter memory is wiped anyway (except the stack).
 *
 * @param[in] THIS Pointer to the data associated with this instance of the
 *                     plugin
 * @param[in] file The url of the embedded object
 * @param[in] pipeFd The file descriptor of the pipe to the helper application
 */
static void run(data_t * const THIS, const char *file, int pipeFd)
{
     char buffer[ENV_BUFFER_SIZE];
     int offset = 0;
     int i;
     unsigned int flags = THIS->command->flags;
     int autostart = THIS->autostart;
     const char * launcher = NULL;
     const char * nextHelper = NULL;

     /* If there is no window to draw the controls in then
      * dont use controls -> mozdev bug #18837 */
     if((THIS->window == 0) &&  ((flags & (H_CONTROLS | H_LINKS)) != 0) )
     {
          D("Cannot use controls or link button as no window to draw"
                                                              " controls in\n");
	  flags &= ~(H_CONTROLS | H_LINKS);
     }

     /* If no autostart seen and using controls dont autostart by default */
     if ((flags & (H_CONTROLS | H_LINKS)) && (THIS->autostartNotSeen))
     {
          autostart = 0;
     }

     snprintf(buffer, sizeof(buffer), "%d,%d,%d,%lu,%d,%d",
	      flags,
	      THIS->repeats,
	      pipeFd,
	      (unsigned long int) THIS->window,
	      (int) THIS->width,
	      (int) THIS->height);

     offset = strlen(buffer)+1;

     offset = my_putenv_unsigned(buffer, sizeof(buffer), offset,
                                                        "window", THIS->window);

     offset = my_putenv_hex(buffer, sizeof(buffer), offset,
                                                     "hexwindow", THIS->window);

     offset = my_putenv_signed(buffer, sizeof(buffer), offset,
                                                      "repeats", THIS->repeats);

     offset = my_putenv_unsigned(buffer, sizeof(buffer), offset,
                                                          "width", THIS->width);

     offset = my_putenv_unsigned(buffer, sizeof(buffer), offset,
                                                        "height", THIS->height);

     offset = my_putenv(buffer, sizeof(buffer), offset,
                                                    "mimetype", THIS->mimetype);

     offset = my_putenv(buffer, sizeof(buffer), offset, "file", file);

     offset = my_putenv(buffer, sizeof(buffer), offset,
                                                 "fragment", THIS->urlFragment);

     offset = my_putenv(buffer, sizeof(buffer), offset,
                                            "autostart", autostart ? "1" : "0");

     offset = my_putenv(buffer, sizeof(buffer), offset,
                                             "winname", THIS->command->winname);

     if(THIS->display)
     {
          char * displayname = XDisplayName(DisplayString(THIS->display));
          offset = my_putenv(buffer, sizeof(buffer), offset, "DISPLAY", displayname);
     }

     for (i = 0; i < THIS->num_arguments; i++)
     {
	  offset = my_putenv(buffer, sizeof(buffer), offset,
                                       THIS->args[i].name, THIS->args[i].value);
     }

     if(flags & H_CONTROLS)
     {
          launcher = g_controller;
     }
     else if(flags & H_LINKS)
     {
          launcher = g_linker;
     }
     else if(!autostart && !(flags & H_AUTOSTART) && (THIS->window != 0))
     {
          /* Application doesn't do autostart and autostart is false and
           * we have a window to draw in */
	  nextHelper = g_helper;
          launcher = g_linker;
     }
     else
     {
          launcher = g_helper;
     }

     if(launcher == 0)
     {
          D("No launcher defined");
          _exit(EX_UNAVAILABLE); /* Child exit, that's OK */
     }

     D("Executing helper: %s %s %s %s %s\n",
       launcher,
       buffer,
       file,
       THIS->command->cmd,
       THIS->mimetype);

     execlp(launcher, launcher, buffer, THIS->command->cmd, nextHelper, NULL);

     D("EXECLP FAILED! errno=%i\n", errno);

     _exit(EX_UNAVAILABLE); /* Child exit, that's OK */

}

static char * NP_strdup2(const char * str, int len)
{
     char * dupStr = NPN_MemAlloc(len + 1);
     if(dupStr != NULL)
     {
          strncpy(dupStr, str, len);
          dupStr[len] = '\0';
     }
     else
     {
          D("NPN_MemAlloc failed, size=%i\n", len+1);
     }
     return dupStr;
}


/**
 * String dup function that uses NPN_MemAlloc as opposed to malloc
 *
 * WARNING, this function will not work if directly or indirectly called from
 * NPP_GetMimeDescription.
 *
 * @param[in] str The string to duplicate
 *
 * @return Pointer to the duplicate.
 */
static char * NP_strdup(const char * str)
{
     return NP_strdup2(str, strlen(str));
}


/**
 * Test if the line buffer contains a mimetype
 *
 * @param[in] buffer The line buffer
 *
 * @return true if contains a mimetype
 */
static bool isCfgMimeType(char * buffer)
{
    return !isspace(buffer[0]);
}

/**
 * Parse the mimetypec in the cfg file
 *
 * @param[in] buffer The line read from config file
 *
 * @return a mimetype_t structure if valid mimetype.
 */
static mimetype_t * parseCfgMimeType(char * buffer)
{
     mimetype_t * type = (mimetype_t *) allocStaticMem(sizeof(mimetype_t));
     if(type == 0)
     {
          D("Failed to alloc memory for mimetype\n");
          return NULL;
     }
     memset(type, 0, sizeof(mimetype_t));

     D("New mime type\n");

     /* Cant use NPN_MemAlloc in NPP_GetMimeDescription, use
      * makeStrStatic as opposed to strdup otherwise we get a
      * memory leak */
     type->type = makeStrStatic(buffer, strlen(buffer));

     return (type->type == 0) ? NULL : type;
}

/**
 * Parse the command found in the cfg file
 *
 * @param[in] buffer The line read from config file
 *
 * @return a command structure if command found.
 */
static command_t * parseCfgCmdLine(char * buffer)
{
     char * x = &buffer[1];
     char * sep;

     command_t * cmd = (command_t *) allocStaticMem(sizeof(command_t));
     if(cmd == NULL)
     {
           D("Failed to alloc memory for command\n");
           return NULL;
     }
     memset(cmd, 0, sizeof(command_t));

     D("-- reading cmd line %s\n", x);

     sep = strchr(x, '\t');
     cmd->flags = strtol(x, NULL, 16);
     x = &sep[1];
     sep = strchr(x, '\t');
     if(sep > x)
     {
          cmd->winname = makeStrStatic(x, sep - x);
     }
     x = &sep[1];
     sep = strchr(x, '\t');
     if( sep > x)
     {
          cmd->fmatchStr = makeStrStatic(x, sep - x);
     }
     x = &sep[1];
     cmd->cmd = makeStrStatic(x, strlen(x));
     return cmd;
}

/**
 * Read the configuration file into memory.
 *
 * @param[in] f The FILE pointer
 */
static void read_config(FILE * f)
{
     int num_handlers = 0;

     handler_t * prev_handler = NULL;
     handler_t * handler = NULL;

     command_t * prev_cmd = NULL;
     mimetype_t * prev_type = NULL;

     char lineBuf[512];
     int lineNum;

     D("read_config\n");

     lineNum = 0;
     while (fgets(lineBuf, sizeof(lineBuf), f))
     {
          lineNum++;
          if(!chkCfgLine(lineBuf))
          {
               continue;
          }

	  D("%5i::|%s|\n", lineNum, lineBuf);

          if(isCfgMimeType(lineBuf))
	  {
	       /* Mime type */
               mimetype_t * type = NULL;

	       if (!handler || handler->cmds)
	       {
		    D("------------ Starting new handler ---------\n");

		    handler = (handler_t *) allocStaticMem(sizeof(handler_t));
                    if(handler == 0)
                    {
                         return;
                    }
                    memset(handler, 0, sizeof(handler_t));

                    prev_type = NULL;
                    prev_cmd = NULL;

                    if(prev_handler)
                    {
                         /* Add to end of linked list of handlers */
                         prev_handler->pNext = handler;
                    }
                    else
                    {
                         /* Add at header of link list of handlers */
                         g_handlers = handler;
                    }
                    /* Remember the current end of linked list */
                    prev_handler = handler;

                    num_handlers++;
	       }

               type = parseCfgMimeType(lineBuf);
               if(!type)
               {
                    return; /* run out of memory! */
               }
               if(prev_type)
               {
                    /* Add to end of linked list of handlers */
                    prev_type->pNext = type;
               }
               else
               {
                    /* Add at header of link list of handlers */
                    handler->types = type;
               }
               /* Remember the current end of linked list */
               prev_type = type;
	  }
	  else
	  {
               command_t * cmd = parseCfgCmdLine(lineBuf);
               if(!cmd)
               {
                    return; /* run out of memory! */
               }
               if(!handler)
               {
                    D("Command before mimetype!\n");
                    return;
               }

               if(prev_cmd)
               {
                    /* Add to end of linked list of handlers */
                    prev_cmd->pNext = cmd;
               }
               else
               {
                    /* Add at header of link list of handlers */
                    handler->cmds = cmd;
               }
               /* Remember the current end of linked list */
               prev_cmd = cmd;
          }
     }
     D("Num handlers: %d\n", num_handlers);
}

/**
 * Find configuration file, helper and controller executables. Call the
 * appropriate xxx_cb function to handle the action (e.g. for configuration
 * file, the parsering and reading into memory).
 *
 * @param[in] magic references for this particular plugin type
 *
 * @return false if failed, true otherwise
 */
static bool do_read_config(const char * magic)
{
     bool retVal = true;
     char * config_fname;
     if (g_handlers)
     {
          return retVal;
     }

     D("do_read_config(%s)\n", magic);

     config_fname = get_cmds_cfg_path(magic);
     get_helper_paths(magic);

     if(config_fname)
     {
          FILE * fd = fopen(config_fname, "rb");
          if(fd)
          {
               read_config(fd);
               fclose(fd);
               D("do_read_config done\n");
          }
          else
          {
               D("Failed to read config %s\n", config_fname);
               retVal = false;
          }
          free(config_fname);
     }
     else
     {
          if(!haveError())
          {
              reportError(NULL, "Mozplugger error - failed to locate %s", config_fname);
          }
	  retVal = false;
     }

     return retVal;
}

/**
 * Check URL is safe. Since href's are passed to an app as an argument, just
 * check for ways that a shell can be tricked into executing a command.
 *
 * @param[in] name The name to check
 * @param[in] isURL Is the name a URL or filename?
 *
 * @return true if OK, false if not
 */
static bool safeName(const char* name, int isURL)
{
     int i = 0;
     const int len = strlen(name);

     if ((name[0] == '/') && (isURL))
     {
          D("safeName() - reject URL '%s' as starts with '/'\n", name);
	  return false;
     }

     for (i = 0; i < len; i++)
     {
	  if (name[i] == '`' || name[i] == ';')
	  {
               D("safeName() - reject '%s' as contains either ';' or '`'\n", name);
	       /* Somebody's trying to do something naughty. */
	       return false;
	  }
     }
     return true;
}

/**
 * Extract the file name from the HTTP Headers (if present). This is passed
 * via the fragment environment variable to the helper application. Copy
 * a suitable filename for the URL to be used for the cached temporay file
 * name.
 *
 * @param[in] THIS Pointer to the instance data
 * @param[in] headers The HTTP headers to parse
 * @param[out] fileName The file Name extracted from the headers
 * @param[in] maxFileNameLen The max length of fileName
 *
 * @return None
 */
static char * parseHeaders(data_t * const THIS, const char * headers, char * fileName)
{
     const char * p = headers;

     if (!headers)
     {
          return fileName;
     }

     while((p = strstr(p, "Content-Disposition:")) != NULL)
     {
          size_t len = strcspn(p, "\n\r");
	  const char * start = strstr(p, "filename=\"");

	  if (len == 0)
          {
	       break;
          }

          if((start ==0) || (start - p > len))
          {
	       p += len;
	       continue;
	  }
 	  start += strlen("filename=\"");
	  len = len - (start - p) - 1;

	  if(len > 0)
          {
                if(fileName)
                {
                     NPN_MemFree(fileName);
                }
                fileName = NP_strdup2(start, len);
	  }
	  p += len;
     }
     return fileName;
}

/**
 * Extract the 'fragment' from the end of the URL if present. This is passed
 * via the fragment environment variable to the helper application. Copy
 * a suitable filename for the URL to be used for the cached temporay file
 * name.
 *
 * @param[in] THIS Pointer to the instance data
 * @param[out] fileName The file Name extracted from the URL
 * @param[in] maxFileNameLen The max length of fileName
 *
 * @return Pointer to allocated memory with fileName
 */
static char * parseURL(data_t * const THIS, int extract_filename)
{
     const char * frag = strchr(THIS->url, '#');

     if(frag)
     {
          if(THIS->urlFragment)
          {
              D("parseURL - replacing previous fragment\n");
              NPN_MemFree(THIS->urlFragment);
          }

          D("parseURL - fragment '%s' found at end of URL\n", &frag[1]);
          THIS->urlFragment = NP_strdup(&frag[1]);
     }

     if(extract_filename)
     {
          const char * end = strrchr(THIS->url, '?');
          const char * start;
          int len;

          /* Find end or url (but dont include variable params or fragment */
          if(!end)
          {
               end = frag ? frag : &THIS->url[strlen(THIS->url)];
          }
          /* Work backwards to the first forward slash */
          start = &end[-1];
          while( (start > THIS->url) && (*start != '/'))
          {
               start--;
          }
          if(*start == '/')
          {
               start++;
          }
          len = end-start;
          return NP_strdup2(start, len);
     }
     return NULL;
}

/**
 * See if the URL matches out match criteria.
 *
 * @param[in] matchStr The string to match
 * @param[in] url The url
 *
 * @return 1(true) if matched, zero otherwise
 */
__inline
static int match_url(const char * matchStr, const char * url)
{
     int matchStrLen;
     const char * end;

     switch (matchStr[0])
     {
     case '*':
          /* Does the URL start with the match String */
          matchStr++; /* Step over the asterisk */
	  return (strncasecmp(matchStr, url, strlen(matchStr)) == 0);

     case '%':
          /* Does the URL end with the match String */
          matchStr++; /* Step over the percent sign */

          /* Need to find the end of the url, before any
           * extra params i.e'?=xxx' or '#yyy' */
          if( (end = strchr(url, '?')) == NULL)
          {
               if( (end = strchr(url, '#')) == NULL)
               {
                    end = &url[strlen(url)];
               }
          }
          matchStrLen = strlen(matchStr);
          if(end - matchStrLen < url)
          {
               return 0;
          }
	  return (strncasecmp(matchStr, end-matchStrLen, matchStrLen) == 0);

     default:
          /* Is the match string anywhere in the URL */
	  return (strstr(url, matchStr) != NULL);
     }
}

/**
 * Go through the commands in the config file and find one that fits our needs.
 *
 * @param[in] THIS Pointer to the data associated with this instance of the
 *                       plugin
 * @param[in] streamOnly If true select entry sith stream set only
 * @param[in] c Pointer to command structure to match against
 *
 * @return 1(true) if match, else zero otherwise
 */
__inline
static int match_command(const data_t * THIS, int streamOnly, const command_t *c)
{
#define MODE_MASK (H_NOEMBED | H_EMBED)

     D("Checking command: %s\n", c->cmd);

     /* If command is specific to a particular mode... */
     if (c->flags & MODE_MASK)
     {
          /* Check it matches the current mode... */
          if ( (THIS->mode_flags & MODE_MASK) != (c->flags & MODE_MASK) )
          {
	       D("Flag mismatch: mode different %x != %x\n",
                     THIS->mode_flags & MODE_MASK,  c->flags & MODE_MASK);
	       return 0;
          }
     }
     if((THIS->mode_flags & H_LINKS) != 0)   /* Requires the links helper? */
     {
          if((c->flags & MODE_MASK) == 0)    /* But command is not */
          {
	       D("Flag mismatch: cmd doesnt do links\n");
	       return 0;
          }
     }

     if ((c->flags & H_LOOP) && (THIS->repeats != INF_LOOPS))
     {
	  D("Flag mismatch: loop\n");
	  return 0;
     }
     if (streamOnly && !(c->flags & H_STREAM))
     {
	  D("Flag mismatch: stream only required\n");
	  return 0;
     }

     if(c->fmatchStr)
     {
          if(!match_url(c->fmatchStr, THIS->url))
          {
               D("fmatch mismatch: url '%s' doesnt have '%s'\n",
                                              THIS->url, c->fmatchStr);
               return 0;
          }
     }
     D("Match found!\n");
     return 1;
}

/**
 * See if mimetype matches.
 *
 * @param[in] reqMimeType pointer to required mimetype
 * @param[in] m Mimetype to match against
 *
 * @return 1(true) if match, else zero otherwise
 */
__inline
static int match_mime_type(const char * reqMimeType, mimetype_t * m)
{
     int retVal;
     if ((strcasecmp(m->type, reqMimeType) != 0) && (strcmp(m->type, "*") != 0))
     {
          retVal = 0;
     }
     else
     {
          retVal = 1;
     }
     D("Checking '%s' ?= '%s', %s\n", m->type, reqMimeType,
                                            retVal == 1 ? "same" : "different");
     return retVal;
}

/**
 * See if handler matches, if so check a command is available and return that
 * command.
 *
 * @param[in] h Pointer to handler to match against
 * @param[in] THIS Pointer to data associated with this instance of plugin
 * @param[in] streamOnly If True select only entry with stream flag
 *
 * @return Pointer to command struct if match or NULL
 */
__inline
static command_t * match_handler(const handler_t * h, const data_t * THIS, int streamOnly)
{
     mimetype_t *m;

     D("-------------------------------------------\n");
     D("Commands for this handle at (%p):\n", h->cmds);

     for(m = h->types; m; m = m->pNext)
     {
	  if (match_mime_type(THIS->mimetype, m))
	  {
               command_t * c;
               for(c = h->cmds; c; c = c->pNext)
	       {
		    if (match_command(THIS, streamOnly, c))
		    {
			 return c;
		    }
	       }
	  }
     }
     return NULL;
}

/**
 * Find the appropriate command
 *
 * @param[in] THIS Pointer to plugin instance data
 * @param[in] streamOnly If true select only the command with stream flag
 *
 * @return Pointer to command struct if match, else NULL otherwise
 */
static command_t * find_command(const data_t * THIS, int streamOnly)
{
     handler_t * h;

     D("find_command...\n");

     for(h = g_handlers; h; h = h->pNext)
     {
          command_t * command = match_handler(h, THIS, streamOnly);
          if(command)
	  {
	       D("Command found.\n");
	       return command;
	  }
     }

     D("No command found.\n");
     return NULL;
}

/**
 * Get the plugin version string
 *
 * @param[in] magic references for this particular plugin type
 *
 * @return Pointer to the version string
 */
const char * NP2_GetPluginVersion(const char * magic)
{
     D("NP_GetPluginVersion(%s)\n", magic);
     if(!is_base_mozplugger(magic))
     {
          get_helper_paths(magic);
     }
     D("NP_GetPluginVersion returning '%s'\n", g_version);
     return g_version;
}

/**
 * Rebuild the cached versions of configuration
 *
 * @return true if success.
 */
static bool mozplugger_update(bool * pDoesntExist)
{
     bool success = true;
     pid_t pid;

     D("Called mozplugger_update\n");
     pid = fork();
     if(pid == -1)
     {
          fprintf(stderr, "Failed to fork\n");
	  exit(EXIT_FAILURE);
     }
     else if(pid == 0)
     {
	  execlp("mozplugger-update", "mozplugger-update", NULL);
          if( errno == EEXIST)
          {
               exit(1000);
          }
          exit(EXIT_FAILURE);
     }
     else
     {
         int status;
         D("Waiting for mozplugger-update\n");
         waitpid(pid, &status, 0);          /* If Application completed is a bad way, then lets give up now */
         if(!WIFEXITED(status))
         {
               D("mozplugger-update dumped core or something...\n");
               success = false;
         }
         else 
         {
               status = WEXITSTATUS(status);
               if(status != EXIT_SUCCESS)
               {
                    D("mozplugger-update exited with status: %d\n", status);
                    success = false;
                    if(status == 1000)
                    {
                         *pDoesntExist = true;
                    }
               }
         }
     }
     D("mozplugger-update done\n");
     if(success)
     {
//         NPN_ReloadPlugins(false);
     }
     return success;
}

/**
 * Check is the local plugin directories exist for various browsers
 * If they do then its likely that the user uses those browsers
 * so check mozplugger0.so exists in that directory
 *
 * @return false if we need to run update
 */
static bool chkValidLocalPluginDirs()
{
     static const char * browsers[] =
     {
          "%s/.mozilla/plugins",
          "%s/.netscape/plugins",
          "%s/.opera/plugins"
     };

     const char * home = get_home_dir();
     int i;

     if(home == NULL)
     {
           reportError(NULL, "Mozplugger cannot determine HOME directory");
           return false;
     }

     for(i = 0; i < sizeof(browsers)/sizeof(const char *); i++)
     {
          struct stat details;
          char fname[256];
          int n = snprintf(fname, sizeof(fname), browsers[i], home);

          if( (mkdir(fname, S_IRWXU) != 0) && (errno != EEXIST))
          {
               continue;
          }

          strncat(fname, "/mozplugger0.so", sizeof(fname) - n);
          if(stat(fname, &details) != 0)
          {
               return false;
          }
     }
     return true;
}

/**
 * Check the last time we updated the cache
 *
 * return -1 too soon, +1 about time to update
 */
static time_t chkTimeToUpdate(bool * update, bool * dont_update)
{
     struct stat details;
     char ts_fname[256];
     time_t ts_ftime = 0;

     get_cfg_path_prefix(".last_update:", ts_fname, sizeof(ts_fname));
     if(stat(ts_fname, &details) == 0)
     {
          time_t now = time(NULL);
          ts_ftime = details.st_mtime;

          if(ts_ftime > now)
          {
               D("Strange .last_update written in the future? %lu s\n", ts_ftime - now); 
          }
          else  
          {
               time_t diff = now - ts_ftime;
               if(diff < 10)
               {
                    D("Dont update, too soon %lu s\n", diff); 
                    *dont_update = true;
               }
#ifdef AUTO_UPDATE
               else if(diff > 7*24*60*60)
               {
                    D("Auto update %lu s\n", diff); 
                    *update = true;
               }
          }
#endif
     }
     return ts_ftime;
}

/**
 * Parse buf for text containing the VERSION of the config file. Check the
 * version matches
 *
 * @param[in] buf The string read from the description file
 *
 * @return false if not matches
 */
static bool chk_version_matches(char * buf)
{
     D("Processed config version = '%s'\n", &buf[1]);
     trim_trailing_spaces(buf);
     if(strcmp(&buf[1],  VERSION) != 0)
     {
          D("Processed config format mismatch should be" VERSION "\n");
          return false;
     }
     return true;
}
 
/**
 * Parse buf for text containing the name of the config file. Check the
 * date and time of that file against the time of the cached file.
 *
 * @param[in] buf The string read from the description file
 *
 * @return false if description file written before timestamp
 */
static bool chk_cached_is_newer(char * buf, time_t cached_ftime)
{
     char * q = strstr(buf, "autogenerated from ");
     if(q)
     {
          struct stat details;
          q += 19;                  /* Skip text part */
          trim_trailing_spaces(q);

          if( (stat(q, &details) == 0) && (details.st_mtime <= cached_ftime))
          {
               return true;
          }
          else
          {
               D("mozpluggerrc = %s %u - %u\n", q, (unsigned) details.st_mtime, (unsigned) cached_ftime);
          }
    }
    return false;
}

static char * read_desc(const char * fname, time_t ts_ftime, bool *update, bool is_base)
{
     char * desc = NULL;
     FILE * fp = fopen(fname, "rb");

     D("Reading '%s'\n", fname);
     if(fp != NULL)
     {
          char linebuf[256];
          
          if( fgets(linebuf, sizeof(linebuf), fp)
                  && chk_version_matches(linebuf) 
                      && fgets(linebuf, sizeof(linebuf), fp)
                          && chk_cached_is_newer(linebuf, ts_ftime))
          {
                while( fgets(linebuf, sizeof(linebuf), fp) && (linebuf[0] == '#'))
                     ;
               
                if(!is_base)
                {
                     struct stat details;

                     fstat(fileno(fp), &details);
                     desc = malloc(details.st_size+1);
                     if(desc)
                     {
                          D("Size '%u'\n", (unsigned) details.st_size);

                         strcpy(desc, linebuf);
                         fgets(&desc[strlen(linebuf)], details.st_size, fp);
                    }
               }
          }
          else
          {
               *update = true;
          }
          fclose(fp);
     }
     else
     {
          D("Failed to read description\n");
#ifdef AUTO_UPDATE
          *update = true;
#endif
     }
     return desc;
}

/**
 * Construct a MIME Description string for netscape so that mozilla shall know
 * when to call us back.
 *
 * @param[in] magic references for this particular plugin type
 *
 * @return Pointer to string containing mime decription for this plugin
 */
const char * NP2_GetMIMEDescription(const char * magic)
{
     char * fname;
     char * desc;
     bool update = false;
     bool dont_update = false;
     bool doesnt_exist = false;
     time_t ts_ftime;

     D("NP_GetMIMEDescription(%s)\n", magic);

     if(!chkValidLocalPluginDirs())
     {
          D("Local plugin dirs not valid");
          update = true;
     }

     /* Check the last time we updated the cache */
     ts_ftime = chkTimeToUpdate( &update, &dont_update);

     if(update && !dont_update)
     {
          mozplugger_update(&doesnt_exist);
          ts_ftime = time(NULL);
          dont_update = true;
          update = false;
     }     

     fname = get_mimetypes_cfg_path(magic);
     desc = read_desc(fname, ts_ftime, &update, is_base_mozplugger(magic));

     if(update && !dont_update)
     {
          mozplugger_update(&doesnt_exist);
          ts_ftime = time(NULL);
          update = false;

          free(desc);
          desc = read_desc(fname, ts_ftime, &update, is_base_mozplugger(magic));

     }
     free(fname);

     if(!desc && update && !doesnt_exist && !haveError())
     {
          reportError(NULL, "Please close browser and run mozplugger-update");
     }

     if(haveError())
     {
          desc = realloc(desc, 512);
          snprintf(desc, 511, "dummy/dummy:*.dummy:%s", errMsg);
     }
     D("Getmimedescription done: %.100s ...\n", desc);
     return (const char *)desc;
}

/**
 * Is the plugin playing
 *
 * @return True if got property
 */
bool is_playing(NPP instance)
{
     data_t * THIS = instance->pdata;
     if (THIS)
     {
          if((THIS->commsPipeFd >= 0) || (THIS->pid > -1))
          {
               int status;
               if(waitpid(THIS->pid, &status, WNOHANG) == 0)
               {
                    /* If no status available from child then child
                                  * must still be running!? */
                    return true;
               }
          }
     }
     return false;
}

/**
 * Get the name of the plugin
 *
 * @param[in] magic references for this particular plugin type
 */
static const char * getPluginName(const char * magic)
{
     if(!is_base_mozplugger(magic))
     {
          get_helper_paths(magic);
     }
     return g_pluginName;
}

/**
 * Get plugin Description
 *
 * @param[in] magic references for this particular plugin type
 *
 * @return Returns the Description of the plugin
 */
static const char * getPluginDescription(const char * magic)
{
     static char desc_buffer[8192];
     const char * dbgPath = get_debug_path();
     char * config_fname = get_cmds_cfg_path(magic);
     struct stat details;

     if(is_base_mozplugger(magic) || (!config_fname) || (stat(config_fname, &details) != 0))
     {
          snprintf(desc_buffer, sizeof(desc_buffer),
		   "MozPlugger version " VERSION
                   " Refresh required, please close browser and run mozplugger-update, "
		   "for documentation on mozplugger see the man page."
                  );
     }
     else
     {
          const char * home = get_home_dir();
          char * pCfg = NULL;
          struct stat details;
          int i;

          details.st_mtime = 0;
          stat(config_fname, &details);

          /* removed cmds and replace with '*' */
          i = strlen(config_fname)-4;
          config_fname[i++] = '*';
          config_fname[i] = '\0';

          /* Hide the user's name!? */
          i = strlen(home);
          if(strncmp(home, config_fname, i) == 0)
          {
               pCfg = &config_fname[i-1];
               *pCfg = '~';
          }
          else
          {
               pCfg = config_fname;
          }

          snprintf(desc_buffer, sizeof(desc_buffer),
		   "MozPlugger version "
		   VERSION
#ifdef GCOV
                   "(gcov)"
#endif
		   ", for documentation on mozplugger see the man page. "
		   "<table>"
		   "<tr><td>Cached config files:</td><td>%s</td><td>%s</td></tr>"
                   "%s%s%s"
		   " </table>"
		   "<br clear=all>",
                   pCfg, asctime(localtime(&details.st_mtime)),
                   dbgPath ? "<tr><td>Debug file:</td><td>" : "",
                   dbgPath ? dbgPath : "",
                   dbgPath ? "/" DEBUG_FILENAME "</td><td></td></tr>" : ""
                  );
     }
     free(config_fname);
     return (const char *)desc_buffer;
}

/**
 * Get plugin needs Xembed
 *
 * @return Returns True if Xembed required
 */
static NPBool getPluginNeedsXembed(NPP instance, NPError *pErr)
{
     NPBool retVal = 0;

     if (instance == NULL)
     {
          *pErr = NPERR_GENERIC_ERROR;
     }
     else
     {
          data_t * this = instance->pdata;
          if((this == NULL) || (this->command == NULL))
          {
               *pErr = NPERR_GENERIC_ERROR;
          }

          else if( ((this->command->flags & H_NEEDS_XEMBED) != 0)
                                              && does_browser_support_xembed())
          {
               D("Plugin needs XEmbed\n");
               retVal = 1;
          }
          else
          {
               D("Plugin does not need XEmbed\n");
          }
     }
     return retVal;
}

/**
 * Let Mozilla know things about mozplugger. This one is called without an
 * instance pointer when loading the plugin.
 *
 * @param[in] magic references for this particular plugin type
 * @param[in] variable Name of variable to get (enum)
 * @param[out] value The value got
 *
 * @return Returns error code if problem
 */
NPError NP2_GetValue(const char * magic, NPPVariable variable, void *value)
{
     NPError err = NPERR_NO_ERROR;

     D("NP_GetValue(%.20s, %s)\n", magic, NPPVariableToString(variable));

     switch (variable)
     {
     case NPPVpluginNameString:
	  *((const char **)value) = getPluginName(magic);
	  break;

     case NPPVpluginDescriptionString:
	  *((const char **)value) = getPluginDescription(magic);
	  break;

     default:
	  D("NP_GetValue('%s' - %d) not implemented\n",
                                      NPPVariableToString(variable), variable);
	  err = NPERR_GENERIC_ERROR;
          break;
     }
     return err;
}

/**
 * Let Mozilla know things about this instance.
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] variable Name of variable to get (enum)
 * @param[out] value The value got
 *
 * @return Returns error code if problem
 */
NPError NPP_GetValue(NPP instance, NPPVariable variable, void *value)
{
     NPError err = NPERR_NO_ERROR;

     D("NPP_GetValue(%s)\n", NPPVariableToString(variable));

     switch (variable)
     {
     case NPPVpluginDescriptionString:
	  *((const char **)value) = getPluginDescription("");
	  break;

     case NPPVpluginNeedsXEmbed:
#ifdef ALWAYS_NEEDS_XEMBED
          /* For Chromium always return 1 */
          *((NPBool *)value) = 1;
#else
          *((NPBool *)value) = getPluginNeedsXembed(instance, &err);
#endif
          break;

     case NPPVpluginScriptableNPObject :
          *((NPObject **)value) = getPluginScritableObject(instance, &err);
          break;

     default :
	  D("NPP_GetValue('%s' - %d) not implemented\n",
                                      NPPVariableToString(variable), variable);
	  err = NPERR_GENERIC_ERROR;
          break;
     }
     return err;
}

/**
 * Let Mozilla set things on mozplugger.
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] variable Name of variable to get (enum)
 * @param[in] value The value to set
 *
 * @return Returns error code if problem
 */
NPError NPP_SetValue(NPP instance, NPNVariable variable, void *value)
{
     NPError err = NPERR_NO_ERROR;

     switch (variable)
     {
     default:
          D("NPP_SetValue( %d)  not implemented\n", variable);
	  err = NPERR_GENERIC_ERROR;
          break;
     }
     return err;
}


/**
 * Convert a string to an integer.
 * The string can be true, false, yes or no.
 *
 * @param[in] s String to convert
 * @param[in] my_true The value associated with true
 * @param[in] my_false The value associated with false
 *
 * @return The value
 */
static int my_atoi(const char *s, int my_true, int my_false)
{
     switch (s[0])
     {
     case 't': case 'T': case 'y': case 'Y':
	  return my_true;
     case 'f': case 'F': case 'n': case 'N':
	  return my_false;
     case '0': case '1': case '2': case '3': case '4':
     case '5': case '6': case '7': case '8': case '9':
	  return atoi(s);
     }
     return -1;
}

/**
 * Initialize another instance of mozplugger. It is important to know
 * that there might be several instances going at one time.
 *
 * @param[in] pluginType Type of embedded object (mime type)
 * @param[in] instance Pointer to plugin instance data
 * @param[in] mode Embedded or not
 * @param[in] argc The number of associated tag attributes
 * @param[in] argn Array of attribute names
 * @param[in] argv Array of attribute values#
 * @param[in] saved Pointer to any previously saved data
 *
 * @return Returns error code if problem
 */
NPError NPP_New(NPMIMEType pluginType, NPP instance, uint16_t mode,
		int16_t argc, char* argn[], char* argv[], NPSavedData* saved)
{
     int e;

     int src_idx = -1;
     int href_idx = -1;
     int data_idx = -1;
     int alt_idx = -1;
     int autostart_idx = -1;
     int autohref_idx = -1;
     int target_idx = -1;
     data_t * THIS;

     char *url = NULL;

     D("NPP_New(%s) - instance=%p\n", pluginType, instance);

     if (!instance)
     {
	  return NPERR_INVALID_INSTANCE_ERROR;
     }

     if (!pluginType)
     {
	  return NPERR_INVALID_INSTANCE_ERROR;
     }

     THIS = NPN_MemAlloc(sizeof(data_t));
     if (THIS == NULL)
     {
          return NPERR_OUT_OF_MEMORY_ERROR;
     }
     instance->pdata = THIS;

     memset((void *)THIS, 0, sizeof(data_t));

     /* Only initialise the non-zero fields */
     THIS->pid = -1;
     THIS->commsPipeFd = -1;
     THIS->repeats = 1;
     THIS->autostart = 1;
     THIS->autostartNotSeen = 1;
     THIS->tmpFileFd = -1;

     if(mode == NP_EMBED)
     {
         THIS->mode_flags = H_EMBED;
     }
     else
     {
         THIS->mode_flags = H_NOEMBED;
     }

     if (!(THIS->mimetype = NP_strdup(pluginType)))
     {
	  return NPERR_OUT_OF_MEMORY_ERROR;
     }

     THIS->num_arguments = argc;
     if(argc == 0)
     {
        return NPERR_NO_ERROR;
     }

     if (!(THIS->args = (argument_t *)NPN_MemAlloc(
                                          (uint32_t)(sizeof(argument_t) * argc))))
     {
	  return NPERR_OUT_OF_MEMORY_ERROR;
     }

     for (e = 0; e < argc; e++)
     {
	  if (strcasecmp("loop", argn[e]) == 0)
	  {
	       THIS->repeats = my_atoi(argv[e], INF_LOOPS, 1);
	  }
          /* realplayer also uses numloop tag */
          /* windows media player uses playcount */
          else if((strcasecmp("numloop", argn[e]) == 0) ||
                  (strcasecmp("playcount", argn[e]) == 0))
          {
	       THIS->repeats = atoi(argv[e]);
          }
	  else if((strcasecmp("autostart", argn[e]) == 0) ||
	          (strcasecmp("autoplay", argn[e]) == 0))
	  {
               autostart_idx = e;
	  }
	  /* get the index of the src attribute if this is a 'embed' tag */
	  else if (strcasecmp("src", argn[e]) == 0)
	  {
	       src_idx = e;
	  }
	  /* get the index of the data attribute if this is a 'object' tag */
          else if (strcasecmp("data", argn[e]) == 0)
          {
               data_idx = e;
          }
          /* Special case for quicktime. If there's an href or qtsrc attribute,
           * remember it for now */
          else if((strcasecmp("href", argn[e]) == 0) ||
	            (strcasecmp("qtsrc", argn[e]) == 0))
          {
               if(href_idx == -1)
               {
                    href_idx = e;
               }
          }
          else if((strcasecmp("filename", argn[e]) == 0) ||
	            (strcasecmp("url", argn[e]) == 0) ||
	            (strcasecmp("location", argn[e]) == 0))
          {
               if(alt_idx == -1)
               {
                    alt_idx = e;
               }
          }
          /* Special case for quicktime. If there's an autohref or target
           * attributes remember them for now */
          else if (strcasecmp("target", argn[e]) == 0)
          {
               target_idx = e;
          }
	  else if(strcasecmp("autohref", argn[e]) == 0)
	  {
               autohref_idx = e;
	  }

	  /* copy the flag to put it into the environment later */
	  D("VAR_%s=%s\n", argn[e], argv[e]);
          {
               const int len = strlen(argn[e]) + 5;

    	       if (!(THIS->args[e].name = (char *)NPN_MemAlloc(len)))
               {
	            return NPERR_OUT_OF_MEMORY_ERROR;
               }
	       snprintf(THIS->args[e].name, len, "VAR_%s", argn[e]);
 	       THIS->args[e].value = argv[e] ? NP_strdup(argv[e]) : NULL;
          }
     }

     if (src_idx >= 0)
     {
          url = THIS->args[src_idx].value;
          /* Special case for quicktime. If there's an href or qtsrc
           * attribute, we want that instead of src but we HAVE to
           * have a src first. */
          if (href_idx >= 0)
          {
	       D("Special case QT detected\n");
	       THIS->href = THIS->args[href_idx].value;

               autostart_idx = autohref_idx;

               if(target_idx >= 0)
               {
                   /* One of those clickable Quicktime linking objects! */
                   THIS->mode_flags &= ~(H_EMBED | H_NOEMBED);
                   THIS->mode_flags |= H_LINKS;
               }
          }
     }
     else if (data_idx >= 0)
     {
          D("Looks like an object tag with data attribute\n");
          url = THIS->args[data_idx].value;
     }
     else if (alt_idx >= 0)
     {
          D("Fall-back use alternative tags\n");
          url = THIS->args[alt_idx].value;
     }

     /* Do the autostart check here, AFTER we have processed the QT special
      * case which can change the autostart attribute */
     if(autostart_idx > 0)
     {
	  THIS->autostart = !!my_atoi(argv[autostart_idx], 1, 0);
	  THIS->autostartNotSeen = 0;
     }

     if (url)
     {
          THIS->url = url;

          /* Mozilla does not support the following protocols directly and
           * so it never calls NPP_NewStream for these protocols */
	  if(   (strncmp(url, "mms://", 6) == 0)
             || (strncmp(url, "mmsu://", 7) == 0)    /* MMS over UDP */
             || (strncmp(url, "mmst://", 7) == 0)    /* MMS over TCP */
             || (strncmp(url, "rtsp://", 7) == 0)
             || (strncmp(url, "rtspu://", 8) == 0)   /* RTSP over UDP */
             || (strncmp(url, "rtspt://", 8) == 0))  /* RTSP over TCP */
	  {
	       D("Detected MMS -> url=%s\n", url);

               THIS->browserCantHandleIt = true;
               THIS->command = find_command(THIS,1); /* Needs to be done early! so xembed
                                                         flag is correctly set*/


               /* The next call from browser will be NPP_SetWindow() &
                * NPP_NewStream will never be called */
	  }
          else
          {
               THIS->command = find_command(THIS,0); /* Needs to be done early so xembed
                                                       flag is correctly set*/

               /* For protocols that Mozilla does support, sometimes
                * the browser will call NPP_NewStream straight away, some
                * times it wont (depends on the nature of the tag). So that
                * it works in all cases call NPP_GetURL, this may result
                * in NPP_NewStream() being called twice (i.e. if this is an
                * embed tag with src attribute or object tag with data
                * attribute) */
               if (mode == NP_EMBED)
               {
                    const NPError retVal = NPN_GetURL(instance, url, 0);
                    if(retVal != NPERR_NO_ERROR)
                    {
                         D("NPN_GetURL(%s) failed with %i\n", url, retVal);

	                 fprintf(stderr, "MozPlugger: Warning: Couldn't get"
                                 "%s\n", url);
                         return NPERR_GENERIC_ERROR;
                    }
               }
          }
     }

     D("New finished\n");

     return NPERR_NO_ERROR;
}

/**
 * Send the SHUTDOWN_MSG to the child process
 *
 * @param[in] pipeFd The pipe fd
 * @param[in] pip The process ID
 *
 */
void sendShutdownMsg(int pipeFd, pid_t pid)
{
     if(pipeFd >= 0)
     {
          PipeMsg_t msg;
          ssize_t ret;

          msg.msgType = SHUTDOWN_MSG;

          D("Writing SHUTDOWN_MSG to fd %d\n", pipeFd);
          ret = write(pipeFd, (char *) &msg, sizeof(msg));
          if(ret == sizeof(msg))
          {
               if(pid >= 0)
               {
                    int i;
                    for(i = 0; i < 5; i++)
                    {
                         int status;
                         if(waitpid(pid, &status, WNOHANG) != 0)
                         {
                              pid = 0;
                              break;
                         }
                         usleep(100000);
                     }
               }
          }
          else
          {
               D("Writing to comms pipe failed\n");
          }
          close(pipeFd); /* this will cause a broken pipe in the helper! */
     }

     /* By this point the child should be dead, but just in case...*/
     if(pid > 0)
     {
          int status;
          if(kill(pid, SIGTERM) == 0)
          {
               usleep(100000);
               kill(pid, SIGKILL);
          }
          waitpid(pid, &status, 0);
     }
}


/**
 * Free data, kill processes, it is time for this instance to die.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[out] save Pointer to any data to be saved (none in this case)
 *
 * @return Returns error code if a problem
 */
NPError NPP_Destroy(NPP instance, NPSavedData** save)
{
     data_t * THIS;

     D("NPP_Destroy(%p)\n", instance);

     if (!instance)
     {
	  return NPERR_INVALID_INSTANCE_ERROR;
     }

     THIS = instance->pdata;
     if (THIS)
     {
          sendShutdownMsg(THIS->commsPipeFd, THIS->pid);
          if(THIS->tmpFileFd >= 0)
          {
               close(THIS->tmpFileFd);
          }
          if(THIS->tmpFileName != 0)
          {
               char * p;
               D("Deleting temp file '%s'\n", THIS->tmpFileName);

               chmod(THIS->tmpFileName, 0600);
               unlink(THIS->tmpFileName);
               p = strrchr(THIS->tmpFileName, '/');
               if(p)
               {
                    *p = '\0';
                    D("Deleting temp dir '%s'\n", THIS->tmpFileName);
                    rmdir(THIS->tmpFileName);
               }
               NPN_MemFree((char *)THIS->tmpFileName);
          }
          if(THIS->args)
          {
               int e;
	       for (e = 0; e < THIS->num_arguments; e++)
	       {
	            NPN_MemFree((char *)THIS->args[e].name);
	            NPN_MemFree((char *)THIS->args[e].value);
	       }
	       NPN_MemFree((char *)THIS->args);
          }

          if(THIS->mimetype)
          {
	       NPN_MemFree(THIS->mimetype);
          }

          if(THIS->urlFragment)
          {
               NPN_MemFree(THIS->urlFragment);
          }

	  NPN_MemFree(instance->pdata);
	  instance->pdata = NULL;
     }

     D("Destroy finished\n");

     return NPERR_NO_ERROR;
}

/**
 * Check that no child is already running before forking one.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] fname The filename of the embedded object
 * @param[in] isURL Is the filename a URL?
 *
 * @return Nothing
 */
static void new_child(NPP instance, const char* fname, int isURL)
{
     int commsPipe[2];
     data_t * THIS;
     sigset_t set;
     sigset_t oset;

     D("NEW_CHILD(%s)\n", fname ? fname : "NULL");

     if(fname == NULL)
     {
          return;
     }

     THIS = instance->pdata;

     if (THIS->pid != -1)
     {
          D("Child already running\n");
	  return;
     }

     /* Guard against spawning helper if no command! */
     if(THIS->command == 0)
     {
          D("Child has no command\n");
          return;
     }

     if(!safeName(fname, isURL)
             || (THIS->urlFragment && !safeName(THIS->urlFragment, 0)))
     {
	  reportError(instance, "MozPlugger: Detected unsafe URL aborting!");
	  return;
     }

     if (socketpair(AF_UNIX, SOCK_STREAM, 0, commsPipe) < 0)
     {
	  reportError(instance, "MozPlugger: Failed to create a pipe!");
	  return;
     }

     /* Mask all the signals to avoid being interrupted by a signal */
     sigfillset(&set);
     sigprocmask(SIG_SETMASK, &set, &oset);

     D(">>>>>>>>Forking<<<<<<<<\n");

     THIS->pid = fork();
     if(THIS->pid == 0)
     {
          int signum;
          int i;
          int maxFds;
          const int commsFd = commsPipe[1];

	  alarm(0);

	  for (signum=0; signum < NSIG; signum++)
          {
	       signal(signum, SIG_DFL);
          }

	  close_debug();

  /* Close all those File descriptors inherited from the
   * parent, except the pipes and stdin, stdout, stderr */

          maxFds = sysconf(_SC_OPEN_MAX);
          for(i = 3; i < maxFds; i++)
          {
              if(i != commsFd)
              {
                   close(i);
              }
          }
          D("Closed up to %i Fds, except %i\n", maxFds, commsFd);

          /* Restore the signal mask */
          sigprocmask(SIG_SETMASK, &oset, &set);

	  run(THIS, fname, commsFd);

	  _exit(EX_UNAVAILABLE); /* Child exit, that's OK */
     }

     /* Restore the signal mask */
     sigprocmask(SIG_SETMASK, &oset, &set);

     if(THIS->pid == -1)
     {
	  reportError(instance, "MozPlugger: Failed to fork helper!");
     }

     D("Child running with pid=%d\n", THIS->pid);

     THIS->commsPipeFd = commsPipe[0];
     close(commsPipe[1]);
}

/**
 * Whilst creating a pdf watch out for characters that may
 * cause issues...
 *
 * @param[in,out] string the string to be escaped
 */
static void escapeBadChars(char * string)
{
     for(;*string; string++)
     {
          char ch = *string;
          if((ch == ';') || (ch == '`') || (ch == '&') ||
                                      (ch == ' ') || (ch == '\t'))
          {
               *string = '_';
          }
     }
}

/**
 * Guess a temporary file name
 *
 * Creates a temporary file name based on the fileName provided. Checks that
 * the filename created does not include any dangereous or awkward characters.
 *
 * @param[in] fileName Pointer to url string
 *
 * @return file descriptor
 */
static int guessTmpFile(const char * fileName, int soFar,
                                     char * tmpFilePath, int maxTmpFilePathLen)
{
     int i = 0;
     int fd = -1;
     int spaceLeft = maxTmpFilePathLen - soFar - 1;
     const int maxNameLen = pathconf(tmpFilePath, _PC_NAME_MAX);
     const int fileNameLen = strlen(fileName);

     if(spaceLeft > maxNameLen)
     {
         spaceLeft = maxNameLen;
     }
     tmpFilePath[soFar++] = '/';

     while(1)
     {
          if(i < 100)
	  {
               int n = 0;
               int pos = 0;
               if(i > 0)
               {
	            n = snprintf(&tmpFilePath[soFar], spaceLeft, "%03i-", i);
               } 
	       if(fileNameLen > (spaceLeft-n))
               {
                    pos = fileNameLen - (spaceLeft-n);
               }
	       strcpy(&tmpFilePath[soFar+n], &fileName[pos]);
          }
	  else
	  {
               strncpy(&tmpFilePath[soFar], "XXXXXX", spaceLeft);
               fd = mkstemp(tmpFilePath);
	       break;
	  }

          escapeBadChars(&tmpFilePath[soFar]);

	  fd = open(tmpFilePath, O_CREAT | O_EXCL | O_WRONLY,
			  S_IRUSR | S_IWUSR);
	  if(fd >= 0)
	  {
	       break;
	  }
          i++;
     }

     return fd;
}

/**
 * From the url create a temporary file to hold a copy of th URL contents.
 *
 * @param[in] fileName Pointer to url string
 * @param[out] tmpFileName Pointer to place tmp file name string
 * @param[in] maxTmpFileLen
 *
 * @return -1 on error or file descriptor
 */
static int createTmpFile(char ** pFileName)
{
     char tmpFilePath[512];
     int fd = -1;
     const char * root;
     const pid_t pid = getpid();

     D("Creating temp file for '%s'\n", *pFileName);

     root = getenv("MOZPLUGGER_TMP");
     if(root)
     {
          int soFar;

          strncpy(tmpFilePath, root, sizeof(tmpFilePath)-1);
          soFar = strlen(tmpFilePath);

	  soFar += snprintf(&tmpFilePath[soFar], sizeof(tmpFilePath)-soFar,
                                                                "/tmp-%i", pid);
          if( (mkdir(tmpFilePath, S_IRWXU) == 0) || (errno == EEXIST))
          {
               D("Creating temp file in '%s'\n", tmpFilePath);

	       fd = guessTmpFile(*pFileName, soFar, tmpFilePath, sizeof(tmpFilePath)-1);
          }
     }

     if(fd < 0)
     {
          root = getenv("TMPDIR");
          if(!root)
          {
               root = "/tmp";
          }

          snprintf(tmpFilePath, sizeof(tmpFilePath), "%s/mozplugger-%i",
                                                                     root, pid);
          if((mkdir(tmpFilePath, S_IRWXU) == 0) || (errno == EEXIST))
          {
               int soFar = strlen(tmpFilePath);

               D("Creating temp file in '%s'\n", tmpFilePath);

	       fd = guessTmpFile(*pFileName, soFar, tmpFilePath, sizeof(tmpFilePath)-1);
          }
     }
     NPN_MemFree(*pFileName);

     if(fd >= 0)
     {
          D("Opened temporary file '%s'\n", tmpFilePath);
          *pFileName = NP_strdup(tmpFilePath);
     }
     else
     {
          *pFileName = NULL;
     }
     return fd;
}

/**
 * Open a new stream.
 * Each instance can only handle one stream at a time.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] type The mime type
 * @param[in] stream Pointer to the stream data structure
 * @param[in] seekable Flag to say if stream is seekable
 * @param[out] stype How the plugin will handle the stream
 *
 * @return Returns error code if a problem
 */
NPError NPP_NewStream(NPP instance, NPMIMEType type, NPStream *stream,
		      NPBool seekable, uint16_t *stype)
{
     char * fileName = NULL;
     char * savedMimetype = NULL;
     data_t * THIS;
     char refind_command = 0;

     D("NPP_NewStream(%p)\n", instance);

     if (instance == NULL)
     {
	  return NPERR_INVALID_INSTANCE_ERROR;
     }

     THIS = instance->pdata;

     /* Looks like browser can handle this stream so we can clear the flag */
     THIS->browserCantHandleIt = 0;

     if((THIS->pid != -1) || (THIS->tmpFileFd >= 0))
     {
          D("NewStream() exiting process already running\n");
	  return NPERR_GENERIC_ERROR;
     }

     /*  Replace the stream's URL with the URL in THIS->href if it
      *  exists. */
     if(THIS->href != NULL)
     {
	  D("Replacing SRC with HREF... \n");

          if((THIS->url == 0) || (strcmp(THIS->href, THIS->url) != 0))
          {
               /* URL has changed */
               D("URL has changed to %s\n", THIS->href);
               THIS->url = THIS->href;
               refind_command = 1;
          }
     }
     else if((THIS->url == 0) || (strcmp(stream->url, THIS->url) != 0))
     {
          /* URL has changed */
          D("URL has changed to %s\n", stream->url);
          THIS->url = (char *) stream->url;
          refind_command = 1;
     }

     D("Url is %s (seekable=%d)\n", THIS->url, seekable);

     /* Ocassionally the MIME type here is different to that passed to the
      * NEW function - this is because of either badly configure web server
      * who's HTTP response content-type does not match the mimetype in the
      * preceding embebbed, object or link tag. Or badly constructed embedded
      * tag or ambiguity in the file extension to mime type mapping. Lets
      * first assume the HTTP response was correct and if not fall back to
      * the original tag in the mime type. */
     if(strcmp(type, THIS->mimetype) != 0)
     {
          D("Mismatching mimetype reported, originally was \'%s\' now '\%s' "
                          "for url %s\n", THIS->mimetype, type, THIS->url);
          savedMimetype = THIS->mimetype;
          THIS->mimetype = NP_strdup(type);

          if(!(THIS->command = find_command(THIS, 0)))
          {
               NPN_MemFree(THIS->mimetype);
               THIS->mimetype = savedMimetype;
               THIS->command = find_command(THIS, 0);
          }
          else
          {
               NPN_MemFree(savedMimetype);
          }
     }
     else if(refind_command)
     {
          THIS->command = find_command(THIS, 0);
          D("Mime type %s\n", type);
     }

     if(THIS->command == 0)
     {
	  reportError(instance, "MozPlugger: No appropriate application found.");
	  return NPERR_GENERIC_ERROR;
     }

     /* Extract from the URL the various additional information */
     fileName = parseURL(THIS, 1);
     D("fileName (pre-header parse) = %s\n", fileName);

     /* Extract the fileName from HTTP headers, overide URL fileName */
     fileName = parseHeaders(THIS, stream->headers, fileName);
     D("fileName = %s\n", fileName);

     if( (THIS->command->flags & H_STREAM) == 0)
     {
          THIS->tmpFileFd = createTmpFile(&fileName);

          if(THIS->tmpFileFd < 0)
          {
	       reportError(instance, "MozPlugger: Failed to create tmp file");
	       return NPERR_GENERIC_ERROR;
          }
          else
          {
               /* Make file read only by us only */
               fchmod(THIS->tmpFileFd, 0400);
               THIS->tmpFileName = fileName;
               THIS->tmpFileSize = 0;
          }
     }
     else
     {
          NPN_MemFree(fileName);
          new_child(instance, THIS->url, 1);
     }

     *stype = NP_NORMAL;
     return NPERR_NO_ERROR;
}

/**
 * Called after NPP_NewStream if *stype = NP_ASFILEONLY.
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] stream Pointer to the stream data structure
 * @param[in] fname Name of the file to stream
 *
 * @return none
 */
void NPP_StreamAsFile(NPP instance, NPStream *stream, const char* fname)
{
     D("NPP_StreamAsFile(%p, %p, %s)\n", instance, stream, fname);

     if (instance != NULL)
     {
          new_child(instance, fname, 0);
     }
}

/**
 * The browser should have resized the window for us, but this function was
 * added because of a bug in Mozilla 1.7 (see Mozdev bug #7734) and
 * https://bugzilla.mozilla.org/show_bug.cgi?id=201158
 *
 * Bug was fixed in Mozilla CVS repositary in version 1.115 of
 * ns4xPluginInstance.cpp (13 Nov 2003), at the time version was 0.13.
 * version 0.14 happened on 14th July 2004
 *
 * @param[in] dpy The display pointer
 * @param[in] win The window ID
 * @param[in] width The width of the window
 * @param[in] height The height of the window
 *
 * @return none
 */
static void resize_window(Display * dpy, Window win, unsigned width, unsigned height)
{
     if(does_browser_have_resize_bug() && ((unsigned)win != 0))
     {
          XSetWindowAttributes attrib;

          attrib.override_redirect = True;
          XChangeWindowAttributes(dpy, win, (unsigned long) CWOverrideRedirect, &attrib);

          D("Bug #7734 work around - resizing WIN 0x%x to %ux%u!?\n",
                (unsigned) win, width, height);

          XResizeWindow(dpy, win, width, height);
     }
}

/**
 * Send the WINDOW_MSG to the child process
 *
 * @param[in] THIS The instance
 * @param[in] Window The window ID
 * @param[in] width The new window width
 * @param[in] height The new window height
 *
 */
void sendWindowMsg(data_t * THIS)
{
     if(THIS->commsPipeFd >= 0)
     {
          PipeMsg_t msg;
          ssize_t ret;

          msg.msgType = WINDOW_MSG;
          msg.window_msg.window = THIS->window;
          msg.window_msg.width = THIS->width;
          msg.window_msg.height = THIS->height;

          D("Sending WIN MSG to helper (win=0x%x - %u x %u)\n",
                                  (unsigned) THIS->window, THIS->width, THIS->height);

          ret = write(THIS->commsPipeFd, (char *) &msg, sizeof(msg));
          if(ret < sizeof(msg))
          {
               D("Writing to comms pipe failed\n");
               close(THIS->commsPipeFd);
               THIS->commsPipeFd = -1;
          }
    }
}


/**
 * The browser calls NPP_SetWindow after creating the instance to allow drawing
 * to begin. Subsequent calls to NPP_SetWindow indicate changes in size or
 * position. If the window handle is set to null, the window is destroyed. In
 * this case, the plug-in must not perform any additional graphics operations
 * on the window and should free any associated resources.
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] window Pointer to NPWindow data structure
 *
 * @return Returns error code if problem
 */
NPError NPP_SetWindow(NPP instance, NPWindow* window)
{
     data_t * THIS;
     D("NPP_SetWindow(%p)\n", instance);

     if(!instance)
     {
          D("NPP_SetWindow, ERROR NULL instance\n");
	  return NPERR_INVALID_INSTANCE_ERROR;
     }

     if(!window)
     {
          D("NPP_SetWindow, WARN NULL window\n");
	  return NPERR_NO_ERROR;
     }

     THIS = instance->pdata;

     if(!window->ws_info)
     {
          D("NPP_SetWindow, WARN NULL display\n");
	  return NPERR_NO_ERROR;
     }

     if(!window->window)
     {
          D("NPP_SetWindow, WARN zero window ID\n");
     }

     THIS->display = ((NPSetWindowCallbackStruct *)window->ws_info)->display;

     THIS->window = (Window) window->window;
     THIS->width = window->width;
     THIS->height = window->height;

     if ((THIS->url) && (THIS->browserCantHandleIt))
     {
          if(THIS->command == 0)
          {
               /* Can only use streaming commands, as Mozilla cannot handle
                * these types (mms) of urls */
               if (!(THIS->command = find_command(THIS, 1)))
               {
                    if(haveError())
                    {
                         NPN_Status(instance, errMsg);
                         clearError();
                    }
                    else
                    {
	                 reportError(instance, "MozPlugger: No appropriate application found.");
                    }
 	            return NPERR_GENERIC_ERROR;
               }
          }

          /* Extract from the URL the various additional information */
          (void) parseURL(THIS, 0);

	  new_child(instance, THIS->url, 1);
          THIS->url = NULL; /* Stops new_child from being called again */
	  return NPERR_NO_ERROR;
     }

     sendWindowMsg(THIS);

     resize_window(THIS->display, THIS->window, THIS->width, THIS->height);

     /* In case Mozilla would call NPP_SetWindow() in a loop. */
     usleep(4000);

//     get_browser_toolkit(instance);
//     does_browser_support_key_handling(instance);

     return NPERR_NO_ERROR;
}

/**
 * Called from browser when there is an event to be passed to the plugin. Only applicabe for
 * windowless plugins
 *
 * @param[in] instance The instance pointer
 * @param[in] event The event
 *
 * @return ??
 */
int16_t NPP_HandleEvent(NPP instance, void* event)
{
     D("NPP_HandleEvent(%p)\n", instance);
     return 0;
}

/**
 * Send progress message to helper
 */
static void sendProgressMsg(data_t * THIS)
{
     if(THIS->commsPipeFd >= 0)
     {
          int ret;
          PipeMsg_t msg;

          msg.msgType = PROGRESS_MSG;
          msg.progress_msg.done = (THIS->tmpFileFd < 0);
          msg.progress_msg.bytes = THIS->tmpFileSize;

          ret = write(THIS->commsPipeFd, (char *) &msg, sizeof(msg));
          if(ret < sizeof(msg))
          {
               D("Writing to comms pipe failed\n");
               close(THIS->commsPipeFd);
               THIS->commsPipeFd = -1;
          }
     }
}

/**
 * Called from the Browser when the streaming has been completed by the Browser
 * (the reason code indicates whether this was due to a User action, Network
 * issue or that streaming has been done.
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] stream Pointer to the stream data structure
 * @param[in] reason Reason for stream being destroyed
 *
 * @return Returns error code if a problem
 */
NPError NPP_DestroyStream(NPP instance, NPStream *stream, NPError reason)
{
     data_t * THIS;
     D("NPP_DestroyStream(%p, %p, %i)\n", instance, stream, reason);

     if (!instance)
     {
          return NPERR_INVALID_INSTANCE_ERROR;
     }

     THIS = instance->pdata;

     if(THIS->tmpFileFd >= 0)
     {
          close(THIS->tmpFileFd);
          THIS->tmpFileFd = -1;

          if( THIS->tmpFileName != NULL)
          {
               D("Closing Temporary file \'%s\'\n", THIS->tmpFileName);
               if(THIS->commsPipeFd < 0)   /* is no helper? */
               {
                    new_child(instance, THIS->tmpFileName, 0);
               }
          }

          sendProgressMsg(THIS);
     }
     return NPERR_NO_ERROR;
}

/**
 * The browser calls this function only once; when the plug-in is loaded,
 * before the first instance is created. NPP_Initialize tells the plug-in that
 * the browser has loaded it.
 *
 * @param[in] magic references for this particular plugin type
 * @param[in] nsTable The table of NPN functions
 * @param[out] pluginFuncs On return contains the NPP functions
 *
 * @return Returns error code if a problem
 */
NPError NP2_Initialize(const char * magic,
                  const NPNetscapeFuncs * nsTable, NPPluginFuncs * pluginFuncs)
{
     NPError err;
     D("NP_Initialize(%.20s)\n", magic);

     if( (err = NPN_InitFuncTable(nsTable)) == NPERR_NO_ERROR)
     {
          if( (err = NPP_InitFuncTable(pluginFuncs)) == NPERR_NO_ERROR)
          {
                get_api_version();

                if(!do_read_config(magic))
                {
                     err = NPERR_GENERIC_ERROR;
                }
                else
                {
                     const int free = MAX_STATIC_MEMORY_POOL - staticPoolIdx;
                     D("Static Pool used=%i, free=%i\n", staticPoolIdx, free);
                }
          }
     }
     return err;
}

/**
 * The browser calls this function just before it unloads the plugin from
 * memory. So this function should do any tidy up - in this case nothing is
 * required.
 *
 * @param[in] magic references for this particular plugin type
 *
 * @return none
 */
NPError NP2_Shutdown(const char * magic)
{
     D("NP_Shutdown(%.20s)\n", magic);
     return NPERR_NO_ERROR;
}

/**
 * Called when user as requested to print the webpage that contains a visible
 * plug-in. For mozplugger this is ignored.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] printInfo Pointer to the print info data structure
 *
 * @return none
 */
void NPP_Print(NPP instance, NPPrint* printInfo)
{
     D("NPP_Print(%p)\n", instance);
}

/**
 * Called when the Browser wishes to deliver a block of data from a stream to
 * the plugin. Since streaming is handled directly by the application specificed
 * in the configuration file, mozplugger has no need for this data. Here it
 * just pretends the data has been taken by returning 'len' to indicate all
 * bytes consumed. Actaully this function should never be called by the
 * Browser.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] stream Pointer to the stream data structure
 * @param[in] offset Where the data starts in 'buf'
 * @param[in] len The amount of data
 * @param[in] buf The data to be delivered
 *
 * @return Always returns value of passed in 'len'
 */
int32_t NPP_Write(NPP instance, NPStream *stream, int32_t offset, int32_t len,
		  void * buf)
{
     D("NPP_Write(%p, %p, %d, %d)\n", instance, stream, offset, (int) len);
     if(instance)
     {
          data_t * const THIS = instance->pdata;

          if(THIS->tmpFileFd >= 0)   /* is tmp file open? */
          {
               if(offset != THIS->tmpFileSize)
               {
                   D("Strange, there's a gap?\n");
               }
               len = write(THIS->tmpFileFd, buf, len);
               THIS->tmpFileSize += len;
               D("Temporary file size now=%i\n", THIS->tmpFileSize);
          }

          sendProgressMsg(THIS);
     }
     return len;
}

/**
 * Browser calls this function before calling NPP_Write to see how much data
 * the plugin is ready to accept.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] stream Pointer to the stream data structure
 *
 * @return CHUNK_SIZE or zero
 */
int32_t NPP_WriteReady(NPP instance, NPStream *stream)
{
     int32_t size = 0;

     D("NPP_WriteReady(%p, %p)\n", instance, stream);
     if (instance != 0)
     {
          data_t * const THIS = instance->pdata;

          if(THIS->tmpFileFd >= 0)  /* is tmp file is open? */
          {
               size = CHUNK_SIZE;
          }
          else
          {
               D("Nothing to do - Application will handle stream\n");
	       /* Tell the browser that it can finish with the stream
   	          (actually we just wanted the name of the stream!)
	          And not the stream data. */
               NPN_DestroyStream(instance, stream, NPRES_DONE);
          }
     }
     return size;
}

/**
 * Browser calls this function to notify when a GET or POST has completed
 * Currently not used by mozplugger
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] url The URL that was GET or POSTed
 * @param[in] reason The reason for the notify event
 * @param[in] notifyData Data that was passed in the original call to Get or Post URL
 *
 * @return none
 */
void NPP_URLNotify(NPP instance, const char * url, NPReason reason, void * notifyData)
{
     D("NPP_URLNotify(%p, %s, %i)\n", instance, url, reason);
}

/**
 * Called by the browser when the browser intends to focus an instance.
 * Instance argument indicates the instance getting focus.
 * Direction argument indicates the direction in which focus advanced to the instance.
 * Return value indicates whether or not the plugin accepts focus.
 * Currently not used by mozplugger
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] direction The advancement direction
 *
 * @return True or False
 */
NPBool NPP_GotFocus(NPP instance, NPFocusDirection direction)
{
     D("NPP_GotFocus(%p, %i)\n", instance, direction);
     return false;
}

/**
 * Called by the browser when the browser intends to take focus.
 * Instance argument indicates the instances losing focus.
 * There is no return value, plugins will lose focus when this is called.
 * Currently not used by mozplugger
 *
 * @param[in] instance Pointer to the plugin instance data
 *
 * @return True or False
 */
void NPP_LostFocus(NPP instance)
{
     D("NPP_LostFocus(%p)\n", instance);
}

/**
 * Currently not used by mozplugger
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] url The URL that was GET or POSTed
 * @param[in] status ??
 * @param[in] notifyData Data that was passed in the original call to Get or Post URL
 *
 * @return None
 */
void NPP_URLRedirectNotify(NPP instance, const char * url, int32_t status, void * noifyData)
{
     D("NPP_URLRedirectNotify(%p, %s, %i)\n", instance, url, status);
}

/**
 * Clear site data held by plugin (should this clear tmp files?)
 * Currently not used by mozplugger
 *
 * @param[in] site The site name
 * @param[in] flags
 * @param[in] maxAge
 *
 * @return Error status
 */
NPError NPP_ClearSiteData(const char * site, uint64_t flags, uint64_t maxAge)
{
     D("NPP_ClearSiteData(%s)\n", site);
     return NPERR_NO_ERROR;
}

/**
 * Get list of sites plugin has data for (should this be list of tmp files?)
 * Currently not used by mozplugger
 *
 * @return List of sites plugin has data for.
 */
char ** NPP_GetSitesWithData(void)
{
     D("NPP_GetSitesWithData()\n");
     return 0;
}
