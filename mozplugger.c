/*****************************************************************************
 *
 * Original author: Fredrik Hübinette <hubbe@hubbe.net>
 *
 * Current Authors: Louis Bavoil <bavoil@cs.utah.edu>
 *                  Peter Leese <hubbe@hubbe.net>
 *
 * This code is based on and is a branch of plugger written 
 * by Fredrik Hübinette <hubbe@hubbe.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/
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

#include <npapi.h>
#include <npruntime.h>

#include "mozplugger.h"
#include "debug.h"
#include "npn-get-helpers.h"

#ifndef __GNUC__
#define __inline
#endif

#define CHUNK_SIZE   (8196)

/*****************************************************************************
 * Type declarations
 *****************************************************************************/

/** Element of fixed size array created when parsing NPP_New call */
typedef struct argument
{
     char *name;
     char *value;
} argument_t;

/** Data associated with an instance of an embed object, can be more than
 * one */
typedef struct data
{
     Display *display;
     char *displayname;
     NPWindow windata;
     pid_t pid;
     int commsPipeFd;
     int repeats;
     unsigned int cmd_flags;     /**< flags associated with command selected */
     const char *command;        /**< command to execute */
     const char *winname;        /**< Window name used by app */
     unsigned int mode_flags;    /**< flags associated with browser calls */
     char *mimetype;
     char *href;                 /**< If QT this is set to handle special case */
     char *url;                  /**< The URL */ 
     char browserCantHandleIt;   /**< Is set if browser cant handle protocol */
     char *urlFragment;

     int          tmpFileFd;     /**< File descriptor of temp file */
     const char * tmpFileName;   /**< Name of the temp file */
     int          tmpFileSize;   /**< Size of temp file so far */

     char autostart;
     char autostartNotSeen;
     int num_arguments;
     struct argument *args;
} data_t;

/** Element of linked list of mimetypes created when parsing config file */
typedef struct mimetype
{
     const char * type;

     struct mimetype * pNext;
} mimetype_t;

/** Element of linked list of commands created when parsing config file */
typedef struct command
{
     int flags;
     const char * cmd;
     const char * winname;
     const char * fmatchStr;

     struct command * pNext;
} command_t;

/** Element of linked list of handlers created when parsing config file */
typedef struct handle
{
     mimetype_t * types;
     command_t * cmds;

     struct handle * pNext;
} handler_t;

typedef struct 
{
     char name[SMALL_BUFFER_SIZE];
     short exists;
} cacheEntry_t;

typedef struct
{
    struct NPObject objHead;
    NPP             assocInstance;
} our_NPObject_t;

/*****************************************************************************
 * Global variables
 *****************************************************************************/
static const char *errMsg = NULL;
static const char *config_fname;
static const char *helper_fname;
static const char *controller_fname;
static const char *linker_fname;
static handler_t * handlers = 0;

static NPBool         browserSupportsXEmbed;

static char staticPool[MAX_STATIC_MEMORY_POOL];
static int  staticPoolIdx = 0;

static NPClass pluginClass;

/*****************************************************************************/
/**
 * Wrapper for putenv(). Instead of writing to the envirnoment, the envirnoment
 * variables are written to a buffer.
 *
 * @param[in,out] buffer The buffer where the envirnoment variables are written
 * @param[in,out] offset The current position in the buffer
 * @param[in] var        The name of the environment variable
 * @param[in] value      The value of the environment variable
 *
 * @return none
 *
 *****************************************************************************/
static void my_putenv(char *buffer, int *offset, const char *var, 
                                                 const char *value)
{
     if(value)
     {
          const int l = strlen(var) + strlen(value) + 2;
          if (*offset + l >= ENV_BUFFER_SIZE)
          {
	       D("Buffer overflow in putenv(%s=%s)\n", var, value);
	       return;
          }
     
          snprintf(buffer+*offset, l, "%s=%s", var, value);
          putenv(buffer+*offset);
          *offset += l;
     }
     else
     {
          D("putenv did nothing, no value for %s\n", var);
     }
}

/*****************************************************************************/
/**
 * Wrapper for execlp() that calls the helper.
 *
 * WARNING: This function runs in the daughter process so one must assume the
 * daughter uses a copy (including) heap memory of the parent's memory space
 * i.e. any write to memory here does not affect the parent memory space. 
 * Since Linux uses copy-on-write, best leave memory read-only and once execlp
 * is called, all daughter memory is wiped anyway (except the stack).
 *
 * @param[in] THIS     Pointer to the data associated with this instance of the
 *                     plugin
 * @param[in] file     The url of the embedded object
 * @param[in] pipeFd       The file descriptor of the pipe to the helper application
 *
 * @return none
 *
 *****************************************************************************/
static void run(data_t * const THIS, const char *file, int pipeFd)
{
     char buffer[ENV_BUFFER_SIZE];
     char foo[SMALL_BUFFER_SIZE];
     int offset = 0;
     int i;
     unsigned int flags = THIS->cmd_flags;
     int autostart = THIS->autostart;
     const char * launcher = NULL;
     const char * nextHelper = NULL;

     /* If there is no window to draw the controls in then
      * dont use controls -> mozdev bug #18837 */
     if(THIS->windata.window == 0)      
     {
          if(flags & (H_CONTROLS | H_LINKS))
          {
               D("Cannot use controls or link button as no window to draw"
                                                              " controls in\n");
	       flags &= ~(H_CONTROLS | H_LINKS);
          }
     }

     /* If no autostart seen and using controls dont autostart by default */
     if ((flags & (H_CONTROLS | H_LINKS)) && (THIS->autostartNotSeen))
     {
          autostart = 0;
     }

     snprintf(buffer, sizeof(buffer), "%d,%d,%d,%lu,%d,%d,%d,%d",
	      flags,
	      THIS->repeats,
	      pipeFd,
	      (unsigned long int) THIS->windata.window,
	      (int) THIS->windata.x,
	      (int) THIS->windata.y,
	      (int) THIS->windata.width,
	      (int) THIS->windata.height);

     offset = strlen(buffer)+1;

     snprintf(foo, sizeof(foo), "%lu", (long unsigned)THIS->windata.window);
     my_putenv(buffer, &offset, "window", foo);

     snprintf(foo, sizeof(foo), "0x%lx", (long unsigned)THIS->windata.window);
     my_putenv(buffer, &offset, "hexwindow", foo);

     snprintf(foo, sizeof(foo), "%ld", (long)THIS->repeats);
     my_putenv(buffer, &offset, "repeats", foo);

     snprintf(foo, sizeof(foo), "%ld", (long)THIS->windata.width);
     my_putenv(buffer, &offset, "width", foo);

     snprintf(foo, sizeof(foo), "%ld", (long)THIS->windata.height);
     my_putenv(buffer, &offset, "height", foo);

     my_putenv(buffer, &offset, "mimetype", THIS->mimetype);
    
     my_putenv(buffer, &offset, "file", file);

     my_putenv(buffer, &offset, "fragment", THIS->urlFragment);

     my_putenv(buffer, &offset, "autostart", autostart ? "1" : "0");
      
     my_putenv(buffer, &offset, "winname", THIS->winname);

     my_putenv(buffer, &offset, "DISPLAY", THIS->displayname);

     for (i = 0; i < THIS->num_arguments; i++)
     {
	  my_putenv(buffer, &offset, THIS->args[i].name, THIS->args[i].value);
     }

     if(flags & H_CONTROLS)
     {
          launcher = controller_fname;
     }
     else if(flags & H_LINKS)
     {
          launcher = linker_fname;
     }
     else if(!autostart && !(flags & H_AUTOSTART) 
                        && (THIS->windata.window != 0))
     {
          /* Application doesn't do autostart and autostart is false and
           * we have a window to draw in */
	  nextHelper = helper_fname;
          launcher = linker_fname;
     }
     else
     {
          launcher = helper_fname;
     }

     if(launcher == 0)
     {
          D("No launcher defined");
          _exit(EX_UNAVAILABLE);       /* Child exit, that's OK */
     }

     D("Executing helper: %s %s %s %s %s %s\n",
       launcher,
       buffer,
       file,
       THIS->displayname,
       THIS->command,
       THIS->mimetype);

     if(nextHelper)
     {
          execlp(launcher, launcher, buffer, THIS->command, nextHelper, NULL);
     }
     else
     {
          execlp(launcher, launcher, buffer, THIS->command, NULL);
     }

     D("EXECLP FAILED!\n");
     
     _exit(EX_UNAVAILABLE);       /* Child exit, that's OK */

}

/*****************************************************************************/
/**
 * Check if 'file' is somewhere to be found in the user PATH.  Performs a stat
 * of the path/file to see if the file exists
 *
 * @param[in] path The path part of the whole path/file name
 * @param[in] file The file part of the whole path/file name
 *
 * @return 1(true) if in path, 0(false) if not
 *
 *****************************************************************************/
static int inpath(const char *path, const char *file)
{
     int start, i;
     const int fileLen = strlen(file);

     start = i = 0;
     do
     {
          /* Reached separator or end of $PATH? */
	  if((path[i] == ':') || (path[i] == '\0'))
	  {
               /* Split out the individual path */
               const int pathLen = i-start;
               if((pathLen > 0) && (pathLen + fileLen + 2 < 1024))
               {
                    char buf[1024];
                    struct stat filestat;

                    strncpy(buf, &path[start], pathLen);

                    /* if individual path doesnt end with slash, add one */
                    if(buf[pathLen-1] != '/')
                    {
                         buf[pathLen] = '/';
                         strcpy(&buf[pathLen+1], file);
                    }
                    else
                    {
                         strcpy(&buf[pathLen], file);
                    }

                    /* Check if file exists */
                    if (stat(buf, &filestat) == 0)
	            {
                         D("stat(%s) = yes\n", buf);
	                 return 1;
	            }
	            D("stat(%s) = no\n", buf);
               }
               if(path[i] == '\0')
               {
                   return 0;
               }
               start = i+1;    /* Move start forward, skipping the ':' */
          }
          i++;
     }
     while(1);
}

/*****************************************************************************/
/**
 * String dup function that correctly uses NPN_MemAlloc as opposed to malloc
 *
 * WARNING, this function will not work if directly or indirectly called from
 * NPP_GetMimeDescription.
 *
 * @param[in] str The string to duplicate
 *
 * @return Pointer to the duplicate.
 *
 *****************************************************************************/
static char * NP_strdup(const char * str)
{
    const int len = strlen(str);
    char * dupStr = NPN_MemAlloc(len + 1);
    if(dupStr != NULL)
    {
        strcpy(dupStr, str);
    }
    else
    {
        D("NPN_MemAlloc failed, size=%i\n", len+1);
    }
    return dupStr;
}

/*****************************************************************************/
/**
 * Check if 'file' exists. Uses a cache of previous finds to avoid performing
 * a stat call on the same file over and over again.
 *
 * @param[in] file The file to find
 *
 * @return 1(true) if in path, 0(false) if not
 *
 *****************************************************************************/
static int find(const char *file)
{
     static cacheEntry_t cache[FIND_CACHE_SIZE];
     static int cacheSize = 0;
     static int cacheHead = 0;

     struct stat filestat;
     int i;
     int exists = 0;

     D("find(%s)\n", file);

     for(i = 0; i < cacheSize; i++)
     {
          if( strcmp(cache[i].name, file) == 0)
          {
               const int exists = cache[i].exists;
               D("find cache hit exists = %s\n", exists ? "yes" : "no");
               return exists;
          }
     }

     if (file[0] == '/')
     {
	  exists = (stat(file, &filestat) == 0);
     }
     else
     {
          /* Get the environment variable PATH */
          const char *env_path = getenv("PATH");

          if(env_path == NULL)
          {
	       D("No $PATH\n");
	       exists = 0;
          }
          else
          {
               exists = inpath(env_path, file);
          }
     }

     strncpy(cache[cacheHead].name, file, SMALL_BUFFER_SIZE);
     cache[cacheHead].name[SMALL_BUFFER_SIZE-1] = '\0';
     cache[cacheHead].exists = exists;

     cacheHead++;
     if(cacheHead > cacheSize)
     {
          cacheSize = cacheHead;
     }
     if(cacheHead >= FIND_CACHE_SIZE)      /* Wrap around the head */
     {
          cacheHead = 0;
     }
     return exists;
}

/*****************************************************************************/
/**
 * Allocate some memory from the static pool. We use a static pool for
 * the database because Mozilla can unload the plugin after use and this
 * would lead to memory leaks if we use the heap.
 *
 * @param[in] size The size of the memory to allocate
 *
 * @return Pointer to the memory allocated or NULL
 *
 *****************************************************************************/
static void * allocStaticMem(int size)
{
     void * retVal; 
     const int newIdx = staticPoolIdx + size;

     if(newIdx > MAX_STATIC_MEMORY_POOL)
     {
          D("Out of static memory");

	  errMsg = "MozPlugger: config file mozpluggerrc is too big - delete" \
                                        "some handlers/commands or mimetypes";
	  fprintf(stderr, "%s\n", errMsg);
	  return NULL;
     }

     retVal = &staticPool[staticPoolIdx];
     staticPoolIdx = newIdx;
     return retVal;
}

/*****************************************************************************/
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
 *
 *****************************************************************************/
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


/*****************************************************************************/
/**
 * get parameter from mozpluggerrc.  Match a word to a flag that takes a
 * parameter e.g. swallow(name).
 *
 * @param[in] x    Pointer to position in text being parsed
 * @param[in] flag Pointer to flag being checked for
 * @param[out] c   Pointer to the found parameter
 *
 * @return Pointer to new position in text being parsed or NULL if error
 *
 *****************************************************************************/
static char *get_parameter(char *x, const char *flag, const char **c)
{
     char *end;
     int len;
 
     /* skip spaces or tabs between flag name and parameter */
     while ((*x == ' ' || *x == '\t'))
     {
          x++;
     }

     if (*x != '(')
     {
          D("Config error - expected '(' after '%s'\n", flag);

	  errMsg = "MozPlugger: syntax error in mozpluggerrc config file";
	  fprintf(stderr, "%s - expected '(' after '%s'\n", errMsg, flag);
	  return NULL;
     }
     x++;
     end = strchr(x,')');
     if (end)
     {
	  len = end - x;
          /* Cant use NPN_MemAlloc in NPP_GetMimeDescription, use 
           * makeStrStatic as opposed to strdup otherwise we get a 
           * memory leak */
	  *c = makeStrStatic(x, len);
          /* No need to test if NULL as test done else where */
	  x = end+1;
     }
     else
     {
          D("Config error - expected ')'\n");

	  errMsg = "MozPlugger: syntax error in mozpluggerrc config file";
	  fprintf(stderr, "%s - expected ')' found nothing\n", errMsg);
	  return NULL;
     }
     return x;
}

/*****************************************************************************/
/**
 * Checks if 'line' starts with 'kw' (keyword), if 'line' is longer than 'kw', 
 * checks that there is not an additional alpha-numeric letter in the 'line'
 * after the 'kw'.
 *
 * @param[in] line The input text line being parsed
 * @param[in] kw   The keyword to match against
 *
 * @return 1(true) if match, 0(false) if not
 *
 *****************************************************************************/
__inline
static int match_word(const char *line, const char *kw)
{
     const unsigned kwLen = strlen(kw);

     return (strncasecmp(line, kw, kwLen) == 0) && !isalnum(line[kwLen]);
}

/*****************************************************************************/
/** 
 * Parse for flags. Scan a line for all the possible flags.
 *
 * @param [in,out] x       The position in the line that is being parsed
 *                         (before & after)
 * @param[in,out] commandp The data structure to hold the details found
 *
 * @return 1(true) if sucessfully parsed, 0(false) otherwise
 *
 *****************************************************************************/
static int parse_flags(char **x, command_t *commandp)
{
     typedef struct
     {
          const char *name;
          unsigned int value;
     } flag_t;

     const static flag_t flags[] = 
     {
	  { "repeat", 		H_REPEATCOUNT 	},
	  { "loop", 		H_LOOP 		},
	  { "stream", 		H_STREAM 	},
	  { "ignore_errors",	H_IGNORE_ERRORS },
	  { "exits", 		H_DAEMON 	},
	  { "nokill", 		H_DAEMON 	},
	  { "maxaspect",	H_MAXASPECT 	},
	  { "fill",		H_FILL 		},
	  { "noisy",		H_NOISY 	},
	  { "embed",            H_EMBED         },
	  { "noembed",          H_NOEMBED       },
          { "links",            H_LINKS         },
          { "controls",         H_CONTROLS      },
          { "swallow",          H_SWALLOW       },
          { "fmatch",           H_FMATCH        },
          { "autostart",        H_AUTOSTART     },
          { "needs_xembed",     H_NEEDS_XEMBED  },
	  { "hidden",           0               },  /* Deprecated */
	  { NULL, 		0 		}
     };

     const flag_t *f;

     for (f = flags; f->name; f++)
     {
	  if (match_word(*x, f->name))
	  {
	       *x += strlen(f->name);
	       commandp->flags |= f->value;

               /* Get parameter for thoses commands with parameters */
               if(f->value & H_SWALLOW)
               {
                    char * p = get_parameter(*x, f->name, &commandp->winname);
	            if(p)
                    {
                         *x = p;
                         return 1;
                    }
               }
               else if(f->value & H_FMATCH)
               {
	            char * p = get_parameter(*x, f->name, &commandp->fmatchStr);
                    if(p)
                    {
                         *x = p;
                         return 1;
                    }
               }
               else
               {
	            return 1;
               }
	  }
     }
     return 0;
}

/*****************************************************************************/
/**
 * Read the configuration file into memory.
 *
 * @param[in] f The FILE pointer
 *
 *****************************************************************************/
static void read_config(FILE *f)
{
     int num_handlers = 0;

     handler_t * prev_handler = NULL;
     handler_t * handler = NULL;

     command_t * cmd = NULL;
     command_t * prev_cmd = NULL;

     mimetype_t * type = NULL;
     mimetype_t * prev_type = NULL;
    
     char buffer[LARGE_BUFFER_SIZE];
     char file[SMALL_BUFFER_SIZE];
     int seen_commands = 0;            /* Force the creation of first handler */

     D("read_config\n");

     handler = (handler_t *) allocStaticMem(sizeof(handler_t));
     if(handler == NULL)
     {
         return;
     }
     memset(handler, 0, sizeof(handler_t));

     while (fgets(buffer, sizeof(buffer), f))
     {
	  if (buffer[0] == '\n' || buffer[0] == '\0')
          {
	       continue;
          }

	  D("::: %s", buffer);

	  if (buffer[0] == '#')
          {
	       continue;
          }

	  if (buffer[strlen(buffer)-1] == '\n')
          {
	       buffer[strlen(buffer)-1] = 0;
          }

	  if (!isspace(buffer[0]))
	  {
	       /* Mime type */

	       if (seen_commands)
	       {
                    /* Store details of previous handler, if it has something
                     * to offer */

                    if(handler->cmds && handler->types)
                    {
                        /* Add at end of link list so this is the last */

                        if(prev_handler)
                        {
                            /* Add to end of linked list of handlers */
                            prev_handler->pNext = handler;
                        }
                        else
                        {
                            /* Add at header of link list of handlers */
                            handlers = handler;
                        }
                        /* Remember the current end of linked list */
                        prev_handler = handler;

                        num_handlers++;
                    }
                    else
                    {
                        /* else - notice we dont free the static memory this is so
                         * we always reserve same amount of memory irrespect of
                         * what applications are currently installed */
                        D("previous handler has no commands, so dropped\n");
                    }

		    D("------------ Starting new handler ---------\n");

		    handler = (handler_t *) allocStaticMem(sizeof(handler_t));
                    if(handler == 0)
                    {
                        return;
                    }
                    memset(handler, 0, sizeof(handler_t));

                    prev_type = NULL;
                    prev_cmd = NULL;

		    seen_commands = 0;
	       }

	       D("New mime type\n");

               type = (mimetype_t *) allocStaticMem(sizeof(mimetype_t));
               if(type == 0)
               {
                    continue;
               }
               memset(type, 0, sizeof(mimetype_t));

               /* Cant use NPN_MemAlloc in NPP_GetMimeDescription, use 
                * makeStrStatic as opposed to strdup otherwise we get a 
                * memory leak */
	       type->type = makeStrStatic(buffer, strlen(buffer));

               if(type->type == 0)
               {
                    continue;
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
	       /* Command */

	       char *x = buffer + 1;
	       while (isspace(*x))
               {
                    x++;
               }

	       if (!*x)
	       {
		    D("Empty command.\n");
		    seen_commands = 1;
		    continue;
	       }

	       D("New command\n");

	       seen_commands = 1;

               cmd = (command_t *) allocStaticMem(sizeof(command_t));
               if(cmd == NULL)
               {
                    continue;
               }
	       memset(cmd, 0, sizeof(command_t));
               

	       D("Parsing %s\n", x);

	       while (*x != ':' && *x)
	       {
		    if (*x == ',' || *x == ' ' || *x == '\t')
		    {
			 x++;
		    }
		    else if (!parse_flags(&x, cmd))
		    {
                         D("Config error - unknown directive %s\n", x);

	                 errMsg = "MozPlugger: syntax error in mozpluggerrc config file";
		         fprintf(stderr, "%s - unknown directive: %s\n", errMsg,  x);
		         x += strlen(x);
		    }
	       }
	       if (*x == ':')
	       {
	            x++;
		    while (isspace(*x))
                    {
                         x++;
                    }

		    if (sscanf(x, "if %"SMALL_BUFFER_SIZE_STR"s", file) != 1
			     && sscanf(x, "%"SMALL_BUFFER_SIZE_STR"s", file) != 1)
                    {
		         continue;
                    }

		    if (!find(file))
                    {
		         continue;
                    }

                    /* Use makeStrStatic as opposed to strdup otherwise we get a 
                     * memory leak */
		    cmd->cmd = makeStrStatic(x, strlen(x));

                    if(cmd->cmd == 0)
                    {
                         continue;
                    }
	       }
	       else
	       {
	            D("No column? (%s)\n", x);
                    continue;
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

     if(handler->cmds && handler->types)
     {
          if(prev_handler)
          {
               prev_handler->pNext = handler;
          }
          else
          {
               handlers = handler;
          }
          num_handlers++;
     }
     else
     {
          /* else - notice we dont free the static memory this is so
           * we always reserve same amount of memory irrespect of
           * what applications are currently installed */
          D("previous handler has no commands, so dropped\n");
     }

     D("Num handlers: %d\n", num_handlers);
}

/*****************************************************************************/
/**
 * Find the config file or the helper file in function of the function cb.
 *
 * @param[in] basename File or app name (without path)
 * @param[in] cb       Function pointer to function to call when helper found
 *
 * @return 1(true) if found, 0(false) if not
 *
 *****************************************************************************/
static int find_helper_file(const char *basename, int (*cb)(const char *))
{
     char fname[LARGE_BUFFER_SIZE];
     char *tmp;

     D("find_helper_file '%s'\n", basename);

     if ((tmp = getenv("MOZPLUGGER_HOME")))
     {
          snprintf(fname, sizeof(fname), "%s/%s", tmp, basename);
	  if (cb(fname)) 
          {
               return 1;
          }
     }

     if ((tmp = getenv("HOME")))
     {
	  snprintf(fname, sizeof(fname), "%s/.mozplugger/%s", tmp, basename);
	  if (cb(fname)) 
          {
               return 1;
          }

	  snprintf(fname, sizeof(fname), "%s/.netscape/%s", tmp, basename);
	  if (cb(fname)) 
          {
               return 1;
          }

	  snprintf(fname, sizeof(fname), "%s/.mozilla/%s", tmp, basename);
	  if (cb(fname)) 
          {
               return 1;
          }

	  snprintf(fname, sizeof(fname), "%s/.opera/%s", tmp, basename);
	  if (cb(fname)) 
          {
               return 1;
          }
     }

     if ((tmp = getenv("MOZILLA_HOME")))
     {
	  snprintf(fname, sizeof(fname), "%s/%s", tmp, basename);
	  if (cb(fname)) 
          {
               return 1;
          }
     }

     if ((tmp = getenv("OPERA_DIR")))
     {
	  snprintf(fname, sizeof(fname), "%s/%s", tmp, basename);
	  if (cb(fname)) 
          {
               return 1;
          }
     }

#ifdef SYSCONFDIR
     snprintf(fname, sizeof(fname), SYSCONFDIR "/%s", basename);
     if (cb(fname)) 
     {
          return 1;
     }
#else
     snprintf(fname, sizeof(fname), "/etc/%s", basename);
     if (cb(fname)) 
     {
          return 1;
     }

     snprintf(fname, sizeof(fname), "/usr/etc/%s", basename);
     if (cb(fname)) 
     {
          return 1;
     }
#endif

     snprintf(fname, sizeof(fname), "/usr/local/mozilla/%s", basename);
     if (cb(fname)) 
     {
          return 1;
     }

     snprintf(fname, sizeof(fname), "/usr/local/netscape/%s", basename);
     if (cb(fname)) 
     {
          return 1;
     }

     if (cb(basename)) 
     {
          return 1;
     }
 
     return 0;
}

/*****************************************************************************/
/**
 * Read config callback function. Open and run the configuration file (fname)
 * through M4 and then call read_config() to parse the output
 *
 * @param[in] fname Full name (and path) of configuration file
 *
 * @return 1(true) if success, 0(false) if not
 *
 ****************************************************************************/
static int read_config_cb(const char *fname)
{
     int m4out[2];
     pid_t pid;
     int fd;
     FILE *fp;

     fd = open(fname, O_RDONLY);
     if (fd < 0)
     {
          /* Not an error! */
          D("READ_CONFIG(%s) - could not open\n", fname);
          return 0;
     }
     D("READ_CONFIG(%s) - file opened\n", fname);

     if (pipe(m4out) < 0)
     {
          D("Failed to create pipe\n");
	  perror("pipe");
	  return 0;
     }

     if ((pid = fork()) == -1)
     {
          D("Failed to fork\n");
	  return 0;
     }

     if (!pid)
     {
	  close(m4out[0]);

	  dup2(m4out[1], 1);
	  close(m4out[1]);

	  dup2(fd, 0);
	  close(fd);

	  execlp("m4", "m4", NULL);
	  fprintf(stderr, "MozPlugger: Error: Failed to execute m4.\n");
	  exit(1);        /* Child exit, that's OK */

     }
     else
     {
	  close(m4out[1]);
	  close(fd);
	  
	  fp = fdopen(m4out[0], "r");
	  if (!fp)
          {
               D("Failed to open pipe\n");
               return 0;
          }
	  read_config(fp);
	  fclose(fp);
	  
          int status;
	  waitpid(pid, &status, 0);
          D("M4 exit status was %i\n", WEXITSTATUS(status));

          if(WEXITSTATUS(status) != 0)
          {
               errMsg = "Mozplugger: M4 parsing of config generated error";
               fprintf(stderr, "%s\n",errMsg);
               D("%s\n", errMsg);
          }

          /* Use makeStrStatic as opposed to strdup otherwise we get a 
           * memory leak */
	  config_fname = makeStrStatic(fname, strlen(fname));
          /* No need to test config_fname is NULL, done elsewhere */
     }
     return 1;
}

/*****************************************************************************/
/**
 * Find mozplugger helper app callback function.
 * Check if the helper executable exists (using stat), if so save the filename
 * to the global 'helper_fname' variable
 *
 * @param[in] fname Full application name (including path)
 *
 * @return 1(true) if success, 0(false) if not
 *
 *****************************************************************************/
static int find_plugger_helper_cb(const char *fname)
{
     D("FIND_HELPER(%s)\n", fname);

     struct stat buf;
     if (stat(fname, &buf) != 0) 
     {
          return 0;
     }
     /* Use makeStrStatic as opposed to strdup otherwise we get a 
      * memory leak */
     helper_fname = makeStrStatic(fname, strlen(fname));
     /* No need to test helper_fname is NULL, done elsewhere */
     return 1;
}

/*****************************************************************************/
/**
 * Find mozplugger helper app callback function.
 * Check if the helper executable exists (using stat), if so save the filename
 * to the global 'controller_fname' variable
 *
 * @param[in] fname Full application name (including path)
 *
 * @return 1(true) if success, 0(false) if not
 *
 *****************************************************************************/
static int find_plugger_controller_cb(const char *fname)
{
     struct stat buf;
     if (stat(fname, &buf) != 0) 
     {
          return 0;
     }

     /* Use makeStrStatic as opposed to strdup otherwise we get a 
      * memory leak */
     controller_fname = makeStrStatic(fname, strlen(fname));
     /* No need to test controller_fname is NULL, done elsewhere */
     return 1;
}

/*****************************************************************************/
/**
 * Find mozplugger helper app callback function.
 * Check if the helper executable exists (using stat), if so save the filename
 * to the global 'linker_fname' variable
 *
 * @param[in] fname Full application name (including path)
 *
 * @return 1(true) if success, 0(false) if not
 *
 *****************************************************************************/
static int find_plugger_linker_cb(const char *fname)
{
     struct stat buf;
     if (stat(fname, &buf) != 0) 
     {
          return 0;
     }
     /* Use makeStrStatic as opposed to strdup otherwise we get a 
      * memory leak */
     linker_fname = makeStrStatic(fname, strlen(fname));
     /* No need to test linker_fname is NULL, done elsewhere */
     return 1;
}

/*****************************************************************************/
/** 
 * Find configuration file, helper and controller executables. Call the
 * appropriate xxx_cb function to handle the action (e.g. for configuration
 * file, the parsering and reading into memory).
 *
 * @return none
 *
 *****************************************************************************/
static void do_read_config(void)
{
     if (handlers)
     {
          return;
     }

     D("do_read_config\n");

     if (!find_helper_file("mozpluggerrc", read_config_cb))
     {
          errMsg = "Mozplugger: Installation error - failed to locate mozpluggerrc";
          fprintf(stderr, "%s\n",errMsg);
          D("%s\n", errMsg);
	  return;
     }

     if (!find_helper_file("mozplugger-helper", find_plugger_helper_cb))
     {
	  if (find("mozplugger-helper")) 
          {
	       helper_fname = "mozplugger-helper";
	  } 
          else 
          { 
               errMsg = "Mozplugger: Installation error - failed to locate mozplugger-helper";
               fprintf(stderr, "%s\n",errMsg);
               D("%s\n", errMsg);
	       return;
	  }
     }

     if (!find_helper_file("mozplugger-controller", find_plugger_controller_cb))
     {
	  if (find("mozplugger-controller")) 
          {
	       controller_fname = "mozplugger-controller";
	  } 
          else 
          {
               errMsg = "Mozplugger: Installation error - failed to locate mozplugger-controller";
               fprintf(stderr, "%s\n",errMsg);
               D("%s\n", errMsg);
	       return;
	  }
     }

     if (!find_helper_file("mozplugger-linker", find_plugger_linker_cb))
     {
	  if (find("mozplugger-linker")) 
          {
	       linker_fname = "mozplugger-linker";
	  } 
          else 
          {
               errMsg = "Mozplugger: Installation error - failed to locate mozplugger-linker";
               fprintf(stderr, "%s\n",errMsg);
               D("%s\n", errMsg);
	       return;
	  }
     }

     D("do_read_config done\n");
}

/*****************************************************************************/
/**
 * Check URL is safe. Since href's are passed to an app as an argument, just 
 * check for ways that  a shell can be tricked into executing a command.
 *
 * @param[in] name   The name to check
 * @param[in] isURL  Is the name a URL or filename?
 *
 * @return 1(true) if OK, 0(false) if not
 *
 *****************************************************************************/
static int safeName(const char* name, int isURL)
{
     int  i = 0; 
     const int len = strlen(name);
    
     if ((name[0] == '/') && (isURL))
     {
          D("safeName() - reject URL '%s' as starts with '/'\n", name);
	  return 0;
     }

     for (i = 0; i < len; i++)
     {
	  if (name[i] == '`' || name[i] == ';')
	  {
               D("safeName() - reject '%s' as contains either ';' or '`'\n", name);
	       /* Somebody's trying to do something naughty. */
	       return 0;
	  }
     }
     return 1;
}

/*****************************************************************************/
/**
 * Extract the file name from the HTTP Headers (if present). This is passed
 * via the fragment environment variable to the helper application. Copy
 * a suitable filename for the URL to be used for the cached temporay file
 * name.
 *
 * @param[in] THIS        Pointer to the instance data
 * @param[in] headers         The HTTP headers to parse
 * @param[out] fileName   The file Name extracted from the headers
 *
 * @return None
 *
 *****************************************************************************/
static void parseHeaders(data_t * const THIS, const char * headers, char * fileName,
                                                                 int maxFileNameLen)
{
     const char * p = headers;

     if (!headers)
     {
          return;
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
		strncpy(fileName, start, len);
		fileName[len] = '\0';
	  }
	  p += len;
     }
}

/*****************************************************************************/
/**
 * Extract the 'fragment' from the end of the URL if present. This is passed
 * via the fragment environment variable to the helper application. Copy
 * a suitable filename for the URL to be used for the cached temporay file
 * name.
 *
 * @param[in] THIS        Pointer to the instance data
 * @param[in] url         The URL to parse
 * @param[out] fileName   The file Name extracted from the URL
 *
 * @return None
 *
 *****************************************************************************/
static void parseURL(data_t * const THIS, const char * url, char * fileName,
                                                         int maxFileNameLen)
{
     const char * frag = strchr(url, '#');

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

     if(fileName)
     {
          const char * end = strrchr(url, '?');
          const char * start;
          int len;

          /* Find end or url (but dont include variable params or fragment */
          if(!end)
          {
               if(frag)
               {
                    end = frag;
               }
               else
               {
                    end = &url[strlen(url)];
               }
          }
          /* Work backwards to the first forward slash */
          start = &end[-1];
          while( (start > url) && (*start != '/'))
          {
               start--;
          }
          if(*start == '/')
          {
              start++;
          }
          len = end-start;
          if(len > maxFileNameLen)
          {
               start = end - maxFileNameLen;
               len = maxFileNameLen;
          }
          strncpy(fileName, start, len);
          fileName[len] = '\0';
     }
}

/*****************************************************************************/
/**
 * See if the URL matches out match criteria.
 *
 * @param[in] matchStr The string to match
 * @param[in] url      The url
 *
 * @return 1(true) if matched, zero otherwise
 *
 *****************************************************************************/
__inline
static int match_url(const char * matchStr, const char * url)
{
     int matchStrLen;
     const char * end;

     switch (matchStr[0])
     {
     case '*':
          /* Does the URL start with the match String */
          matchStr++;     /* Step over the asterisk */
	  return (strncasecmp(matchStr, url, strlen(matchStr)) == 0);

     case '%':
          /* Does the URL end with the match String */
          matchStr++;     /* Step over the percent sign */

          /* Need to find the end of the url, before any 
           * extra params i.e'?=xxx' or '#yyy' */
          end = strchr(url, '?');
          if(end == NULL)
          {
               end = strchr(url, '#');
               if(end == NULL)
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

/*****************************************************************************/
/**
 * Go through the commands in the config file and find one that fits our needs.
 *
 * @param[in] THIS       Pointer to the data associated with this instance of the
 *                       plugin
 * @param[in] streamOnly If true select entry sith stream set only
 * @param[in] c          Pointer to command structure to match against
 *
 * @return 1(true) if match, else zero otherwise
 *
 *****************************************************************************/
__inline
static int match_command(data_t * const THIS, int streamOnly, command_t *c)
{
#define MODE_MASK (H_NOEMBED | H_EMBED | H_LINKS)

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

     if ((c->flags & H_LOOP) && (THIS->repeats != MAXINT))
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

/*****************************************************************************/
/**
 * See if mimetype matches.
 *
 * @param[in] THIS     pointer to data associated with this instance of the 
 *                     plugin
 * @param[in] m        Mimetype to match against
 *
 * @return 1(true) if match, else zero otherwise
 *
 *****************************************************************************/
__inline
static int match_mime_type(data_t * const THIS, mimetype_t *m)
{
     char mimetype[SMALL_BUFFER_SIZE];
     sscanf(m->type, "%"SMALL_BUFFER_SIZE_STR"[^:]", mimetype);
     sscanf(mimetype, "%s", mimetype);

     int retVal;
     if ((strcasecmp(mimetype, THIS->mimetype) != 0) 
                                                 && (strcmp(mimetype,"*") != 0))
     {
          retVal = 0;
     }
     else
     {
          retVal = 1;
     }
     D("Checking '%s' ?= '%s', %s\n", mimetype, THIS->mimetype, 
                                            retVal == 1 ? "same" : "different");
     return retVal;
}

/*****************************************************************************/
/**
 * See if handler matches.
 *
 * @param[in] h          Pointer to handler to match against
 * @param[in] THIS       Pointer to data associated with this instance of plugin
 * @param[in] streamOnly If True select only entry with stream flag
 *
 * @return 1(true) if match, else zero otherwise
 *
 *****************************************************************************/
__inline
static int match_handler(handler_t *h, data_t * const THIS, int streamOnly)
{
     mimetype_t *m;
     command_t *c;

     D("-------------------------------------------\n");
     D("Commands for this handle at (%p):\n", h->cmds);

     m = h->types;
     while(m)
     {
	  if (match_mime_type(THIS, m))
	  {
               c = h->cmds;
               while(c)
	       {
		    if (match_command(THIS, streamOnly, c))
		    {
			 THIS->cmd_flags = c->flags;
			 THIS->command = c->cmd;
			 THIS->winname = c->winname;
			 return 1;
		    }
                    c = c->pNext;
	       }
	  }
          m = m->pNext;
     }
     return 0;
}

/*****************************************************************************/
/**
 * Find the appropriate command
 *
 * @param[in] THIS        Pointer to plugin instance data
 * @param[in] streamOnly  If true select only the command with stream flag
 *
 * @return 1(true) if match, else zero otherwise
 *
 *****************************************************************************/
static int find_command(data_t * const THIS, int streamOnly)
{
     handler_t * h;

     D("find_command...\n");

     /* Reset any previous command we may have found */
     THIS->command = 0;
     THIS->cmd_flags = 0;
     THIS->winname = 0;

     do_read_config();

     h = handlers;
     while(h)
     {
	  if (match_handler(h, THIS, streamOnly))
	  {
	       D("Command found.\n");
	       return 1;
	  }
          h = h->pNext;
     }

     D("No command found.\n");
     return 0;
}

/*****************************************************************************/
/** 
 * NPP get Mime decription
 * Construct a MIME Description string for netscape so that mozilla shall know
 * when to call us back.
 *
 * @return Pointer to string containing mime decription for this plugin
 *
 *****************************************************************************/
const char *NPP_GetMIMEDescription(void)
{
     char *x,*y;
     handler_t *h;
     mimetype_t *m;
     int size_required;

     D("NPP_GetMIMEDescription()\n");

     do_read_config();

     {
          const int free = MAX_STATIC_MEMORY_POOL - staticPoolIdx;
          D("Static Pool used=%i, free=%i\n", staticPoolIdx, free);
     }

     size_required = 0;

     h = handlers;
     while(h)
     {
          m = h->types;
          while(m)
	  {
	       size_required += strlen(m->type)+1;
               m = m->pNext;
	  }
          h = h->pNext;
     }

     D("Size required=%d\n", size_required);

     if (!(x = (char *)malloc(size_required+1)))
     {
	  return 0;
     }

     D("Malloc did not fail\n");

     y = x;

     h = handlers;
     while(h)
     {
          m = h->types;
          while(m)
	  {
	       memcpy(y,m->type,strlen(m->type));
	       y+=strlen(m->type);
	       *(y++)=';';
               m = m->pNext;
	  }
          h = h->pNext;
     }
     if (x != y) 
     {
          y--;
     }
     *(y++) = 0;

     D("Getmimedescription done: %s\n", x);

     return (const char *)x;
}

/*****************************************************************************/
/**
 * Provides plugin HasMethod that can be called from broswer
 *
 * @return True if has that method
 *
 *****************************************************************************/
static void debugLogIdentifier(NPIdentifier name)
{
     if(NPN_IdentifierIsString(name))
     {
         char * str = NPN_UTF8FromIdentifier(name);
         D("NPIdentifier = %s\n", str ? (char *)str : "NULL");
         NPN_MemFree(str);
     }
     else
     {
         D("NPIdentifier = %i\n", NPN_IntFromIdentifier(name));
     }
}

/*****************************************************************************/
/**
 * Provides plugin HasMethod that can be called from broswer
 *
 * @return True if has that method
 *
 *****************************************************************************/
bool NPP_HasMethod(NPObject *npobj, NPIdentifier name)
{
     D("NPP_HasMethod called\n");
     debugLogIdentifier(name);

     return 0;
}

/*****************************************************************************/
/**
 * Provides plugin Invoke that can be called from broswer
 *
 * @return True if method was invoked
 *
 *****************************************************************************/
bool NPP_Invoke(NPObject *npobj, NPIdentifier name,
                   const NPVariant *args, uint32_t argCount, NPVariant *result)
{
     D("NPP_Invoke called\n");
     debugLogIdentifier(name);

     return 0;
}

/*****************************************************************************/
/**
 * Provides plugin HasProperty that can be called from broswer
 *
 * @return True if has property
 *
 *****************************************************************************/
bool NPP_HasProperty(NPObject *npobj, NPIdentifier name)
{
     bool retVal = 0;

     D("NPP_HasProperty called\n");
     debugLogIdentifier(name);

     char * str = NPN_UTF8FromIdentifier(name);
     if(str)
     {
          if (strcasecmp("isplaying", str) == 0)
	  {
               retVal = 1;
	  }
          NPN_MemFree(str);
     } 
     return retVal;
}

/*****************************************************************************/
/**
 * Provides plugin GetProperty that can be called from broswer
 *
 * @return True if got property
 *
 *****************************************************************************/
bool NPP_GetProperty(NPObject *npobj, NPIdentifier name, NPVariant *result)
{
     bool retVal = 0;

     D("NPP_GetProperty called\n");
     debugLogIdentifier(name);

     char * str = NPN_UTF8FromIdentifier(name);
     if(str)
     {
          if (strcasecmp("isplaying", str) == 0)
          {
	       result->type  = NPVariantType_Bool;
               result->value.boolValue = 0;
               retVal = 1;
 	  
               NPP instance = ((our_NPObject_t *)npobj)->assocInstance;
               if(instance)
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
                                 result->value.boolValue = 1;
                             }
                         }
                    }
               }
          }
          NPN_MemFree(str);
     } 
     return retVal;
}

/*****************************************************************************/
/**
 * Provides plugin SetProperty that can be called from broswer
 *
 * @return True if set property
 *
 *****************************************************************************/
bool NPP_SetProperty(NPObject *npobj, NPIdentifier name,
                                                         const NPVariant *value)
{
     D("NPP_SetProperty called\n");
     debugLogIdentifier(name);

     return 0;
}

/*****************************************************************************/
/**
 * Get plugin Name
 *
 * @return Returns the Name of the plugin
 *
 *****************************************************************************/
static const char * getPluginName(void)
{
     return  "MozPlugger "  VERSION
	       " handles QuickTime and Windows Media Player Plugin";
	
}

/*****************************************************************************/
/**
 * Get plugin Description
 *
 * @return Returns the Description of the plugin
 *
 *****************************************************************************/
static const char * getPluginDescription(void)
{
     static char desc_buffer[8192];
     const char * dbgPath = get_debug_path();

     snprintf(desc_buffer, sizeof(desc_buffer),
		   "MozPlugger version "
		   VERSION
#ifdef GCOV
                   "(gcov)"
#endif
		   ", maintained by Louis Bavoil and Peter Leese, a fork of plugger"
                   " written by Fredrik H&uuml;binette.<br>"
		   "For documentation on how to configure mozplugger, "
		   "check the man page. (type <tt>man&nbsp;mozplugger</tt>)"
		   " <table> "
		   " <tr><td>Configuration file:</td><td>%s</td></tr> "
		   " <tr><td>Helper binary:</td><td>%s</td></tr> "
		   " <tr><td>Controller binary:</td><td>%s</td></tr> "
		   " <tr><td>Link launcher binary:</td><td>%s</td></tr> "
                   "%s%s%s"
		   " </table> %s"
		   "<br clear=all>",
		   config_fname ? config_fname : "Not found!",
		   helper_fname ? helper_fname : "Not found!",
		   controller_fname ? controller_fname : "Not found!",
		   linker_fname ? linker_fname : "Not found!",
                   dbgPath ? " <tr><td>Debug file:</td><td>" : "",
                   dbgPath ? dbgPath : "",
                   dbgPath ? "/" DEBUG_FILENAME "</td></tr> " : "",
                   errMsg ? errMsg : ""
                  );
     errMsg = NULL;
     return (const char *)desc_buffer;
}

/*****************************************************************************/
/**
 * Get plugin needs Xembed
 *
 * @return Returns True if Xembed required
 *
 *****************************************************************************/
static NPBool getPluginNeedsXembed(NPP instance, NPError *pErr)
{
     data_t * this;

     if (instance == NULL)
     {
          *pErr = NPERR_GENERIC_ERROR;
          return 0;
     }

     this = instance->pdata;
     if(this == NULL)
     {
          *pErr = NPERR_GENERIC_ERROR;
          return 0;
     }

     if( ((this->cmd_flags & H_NEEDS_XEMBED) != 0) && browserSupportsXEmbed)
     {
          D("Plugin needs XEmbed\n"); 
          return 1;
     }
     else
     {
           D("Plugin does not need XEmbed\n"); 
           return 0;
     }
}

/*****************************************************************************/
/**
 * Create a JavaScript object
 *
 * @return Pointer to created object
 *
 *****************************************************************************/
static NPObject * NPP_AllocateObj(NPP instance, NPClass * aClass)
{
    our_NPObject_t * pObj = NPN_MemAlloc(sizeof(our_NPObject_t));
    pObj->assocInstance = instance;
    return (NPObject *) pObj;
}

/*****************************************************************************/
/**
 * Get plugin Scritable Object
 *
 * @return Returns True if Xembed required
 *
 *****************************************************************************/
static NPObject * getPluginScritableObject(NPP instance, NPError * pErr)
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

/*****************************************************************************/
/**
 * NPP Get value
 * Let Mozilla know things about mozplugger. This single function actually 
 * covers two interface functions NP_GetValue and NPP_GetValue. The first is
 * called without an instance pointer  (instance = future = null) and the second
 * with an instance pointer. The mozilla documentation is a bit confusing in this
 * respect.
 *
 * @param[in] instance Pointer to plugin instance data (or null if call came 
 *                     though the NP_GetValue interface (as opposed to 
 *                     NPP_GetValue) see NPAPI code if you need more details. 
 * @param[in] variable Name of variable to get (enum)
 * @param[out] value   The value got
 *
 * @return Returns error code if problem
 *
 *****************************************************************************/
NPError NPP_GetValue(void *instance, NPPVariable variable, void *value)
{
     NPError err = NPERR_NO_ERROR;
  
     switch (variable)
     {
     case NPPVpluginNameString:
	  D("NP_GetValue(NPPVpluginNameString)\n");
	  *((const char **)value) = getPluginName();
	  break;

     case NPPVpluginDescriptionString:
	  D("NP_GetValue(NPPVpluginDescriptionString)\n");
	  *((const char **)value) = getPluginDescription();
	  break;

     case NPPVpluginNeedsXEmbed:
          D("NPP_GetValue(NPPVpluginNeedsXEmbed)\n");
#ifdef ALWAYS_NEEDS_XEMBED 
          /* For Chromium always return 1 */
          *((NPBool *)value) = 1;
#else
          *((NPBool *)value) = getPluginNeedsXembed(instance, &err);
#endif
          break; 

     case NPPVpluginScriptableNPObject :
	  D("NP_GetValue(NPPVpluginScriptableNPObject\n");
          *((NPObject **)value) = getPluginScritableObject(instance, &err);
          break;

     default :
	  {
               /* The following I've never seen in a NPP_GetValue call */
               const char * varName;
	       switch(variable)
               {
               case NPPVpluginWindowBool:
                    varName = "NPPVpluginWindowBool";  break; 
               case NPPVpluginTransparentBool:
                    varName = "NPPVpluginTransparentBool"; break; 
	       case NPPVjavaClass: 
	            varName = "NPPVjavaClass"; break;
               case NPPVpluginWindowSize:        
	            varName = "NPPVpluginWindowSize"; break;
               case NPPVpluginTimerInterval:
	            varName = "NPPVpluginTimerInterval"; break;
               case NPPVpluginScriptableInstance:
	            varName = "NPPVpluginScriptableInstance"; break;
               case NPPVpluginScriptableIID:
	            varName = "NPPVpluginScriptableIID"; break;
               case NPPVjavascriptPushCallerBool:
	            varName = "NPPVjavascriptPushCallerBool"; break;
               case NPPVpluginKeepLibraryInMemory:
	            varName = "NPPVpluginKeepLibraryInMemory"; break;
               case NPPVformValue:
	            varName = "NPPVformValue"; break;
               case NPPVpluginUrlRequestsDisplayedBool:
	            varName = "NPPVpluginUrlRequestsDisplayedBool"; break;
               case NPPVpluginWantsAllNetworkStreams:
	            varName = "NPPVpluginWantsNetworkStreams"; break;
               case NPPVpluginNativeAccessibleAtkPlugId:
                    varName = "NPPVpluginNativeAccessibleAtkPlugId"; break;
               case NPPVpluginCancelSrcStream:
                    varName = "NPPVpluginCancelSrcStream"; break;
               case NPPVsupportsAdvancedKeyHandling: 
                    varName = "NPPVsupportsAdvancedKeyHandling"; break;
               case NPPVpluginUsesDOMForCursorBool:
                    varName = "NPPVpluginUsesDOMForCursorBool"; break;
	       default:
		    varName = ""; break;
	       }
	       D("NPP_GetValue('%s' - %d) not implemented\n", varName, variable);
          }
	  err = NPERR_GENERIC_ERROR;
          break;
     }
     return err;
}

/*****************************************************************************/
/** 
 * NPP Set value
 * Let Mozilla set things on mozplugger. 
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] variable Name of variable to get (enum)
 * @param[in] value    The value to set
 *
 * @return Returns error code if problem
 *
 *****************************************************************************/
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


/*****************************************************************************/
/**
 * Convert a string to an integer.
 * The string can be true, false, yes or no.
 *
 * @param[in] s        String to convert
 * @param[in] my_true  The value associated with true
 * @param[in] my_false The value associated with false
 *
 * @return The value
 *
 *****************************************************************************/
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

/*****************************************************************************/
/**
 * NPP New
 * Initialize another instance of mozplugger. It is important to know
 * that there might be several instances going at one time.
 *
 * @param[in] pluginType Type of embedded object (mime type)
 * @param[in] instance   Pointer to plugin instance data
 * @param[in] mode       Embedded or not
 * @param[in] argc       The number of associated tag attributes
 * @param[in] argn       Array of attribute names
 * @param[in] argv       Array of attribute values#
 * @param[in] saved      Pointer to any previously saved data
 *
 * @return Returns error code if problem
 *
 *****************************************************************************/
NPError NPP_New(NPMIMEType pluginType,
		NPP instance,
		uint16_t mode,
		int16_t argc,
		char* argn[],
		char* argv[],
		NPSavedData* saved)
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
     if (!(THIS->args = (argument_t *)NPN_MemAlloc(
                                          (uint32_t)(sizeof(argument_t) * argc))))
     {
	  return NPERR_OUT_OF_MEMORY_ERROR;
     }

     for (e = 0; e < argc; e++)
     {
	  if (strcasecmp("loop", argn[e]) == 0)
	  {
	       THIS->repeats = my_atoi(argv[e], MAXINT, 1);
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
          else if (((strcasecmp("href", argn[e]) == 0) ||
	            (strcasecmp("qtsrc", argn[e]) == 0)) &&
	            href_idx == -1)
          {
               href_idx = e;
          }
          else if (((strcasecmp("filename", argn[e]) == 0) ||
	            (strcasecmp("url", argn[e]) == 0) ||
	            (strcasecmp("location", argn[e]) == 0)) &&
                    alt_idx == -1)
          {
               alt_idx = e;
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
               find_command(THIS,1);            /* Needs to be done early! so xembed 
                                                       flag is correctly set*/


               /* The next call from browser will be NPP_SetWindow() & 
                * NPP_NewStream will never be called */
	  }
          else 
          {
               find_command(THIS,0);           /* Needs to be done early so xembed
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

/*****************************************************************************/
/**
 * NPP Destroy
 * Free data, kill processes, it is time for this instance to die.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[out] save    Pointer to any data to be saved (none in this case)
 * 
 * @return Returns error code if a problem
 *
 *****************************************************************************/
NPError NPP_Destroy(NPP instance, NPSavedData** save)
{
     int e;
     data_t * THIS;

     D("NPP_Destroy() - instance=%p\n", instance);

     if (!instance)
     {
	  return NPERR_INVALID_INSTANCE_ERROR;
     }

     THIS = instance->pdata;
     if (THIS)
     {
	  /* Kill the mozplugger-helper process and his sons */
 	  if (THIS->pid > 0)
          {
	       my_kill(-THIS->pid);
               THIS->pid = 0;
          }
	  if (THIS->commsPipeFd >= 0)
          {
	       close(THIS->commsPipeFd);
               THIS->commsPipeFd = -1;
          }
          if(THIS->tmpFileFd >= 0)
          {
               close(THIS->tmpFileFd);
               THIS->tmpFileFd = -1;
          }
          if(THIS->tmpFileName != 0)
          {
               char * p;
               D("Deleting temp file '%s'\n", THIS->tmpFileName);

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
	  for (e = 0; e < THIS->num_arguments; e++)
	  {
	       NPN_MemFree((char *)THIS->args[e].name);
	       NPN_MemFree((char *)THIS->args[e].value);
	  }

	  NPN_MemFree((char *)THIS->args);

	  NPN_MemFree(THIS->mimetype);
	  THIS->href = NULL;
	  THIS->url = NULL;

          NPN_MemFree(THIS->urlFragment);
          THIS->urlFragment = NULL;

	  NPN_MemFree(instance->pdata);
	  instance->pdata = NULL;
     }

     D("Destroy finished\n");
  
     return NPERR_NO_ERROR;
}

/*****************************************************************************/
/**
 * Check that no child is already running before forking one.
 *
 * @param[in] THIS     Pointer to the plugin instance data
 * @param[in] fname    The filename of the embedded object
 * @param[in] isURL    Is the filename a URL?
 *
 * @return Nothing
 *
 *****************************************************************************/
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
	  NPN_Status(instance, "MozPlugger: Detected unsafe URL aborting!");
	  return;
     }

     if (socketpair(AF_UNIX, SOCK_STREAM, 0, commsPipe) < 0)
     {
	  NPN_Status(instance, "MozPlugger: Failed to create a pipe!");
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

	  if (!(THIS->cmd_flags & H_DAEMON))
          {
	       setsid();
          }

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

	  _exit(EX_UNAVAILABLE);       /* Child exit, that's OK */
     }

     /* Restore the signal mask */
     sigprocmask(SIG_SETMASK, &oset, &set);

     if(THIS->pid == -1)
     {
	  NPN_Status(instance, "MozPlugger: Failed to fork helper!");
     }

     D("Child running with pid=%d\n", THIS->pid);

     THIS->commsPipeFd = commsPipe[0];
     close(commsPipe[1]);
}

/*****************************************************************************/
/**
 * Guess a temporary file name
 *
 * Creates a temporary file name based on the fileName provided. Checks that
 * the filename created does not include any dangereous or awkward characters.
 *
 * @return file descriptor
 *
 *****************************************************************************/
static int guessTmpFile(const char * fileName, int soFar,
                                     char * tmpFilePath, int maxTmpFilePathLen)
{
     int i;
     int fd = -1;
     const int spaceLeft = maxTmpFilePathLen - soFar;

     for(i = 0; i <= 100; i++)
     {
          if(i == 0)
	  {
               /* Whilst creating a pdf watch out for characters that may
                * cause issues... */

               char * dst = &tmpFilePath[soFar];
               const char * src = fileName;
               int left = spaceLeft;
               while(left > 0)
               {
                    char ch = *src++;
                    if((ch == ';') || (ch == '`') || (ch == '&') ||
                                      (ch == ' ') || (ch == '\t'))
                    {
                        ch = '_';
                    }
                    else if(ch == '\0')
                    {
                        break;
                    }
                    *dst++ = ch;
               }
	       strncpy(&tmpFilePath[soFar], fileName, spaceLeft);
          }
	  else if (i == 100)
	  {
               strncpy(&tmpFilePath[soFar], "XXXXXX", spaceLeft);
               fd = mkstemp(tmpFilePath);
	       break;
	  }
	  else
	  {
	       snprintf(&tmpFilePath[soFar], spaceLeft,
				 "%03i-%s", i, fileName);
	  }
          tmpFilePath[maxTmpFilePathLen-1] = '\0';

	  fd = open(tmpFilePath, O_CREAT | O_EXCL | O_WRONLY,
			  S_IRUSR | S_IWUSR);
	  if(fd >= 0)
	  {
	       break;
	  }
     }

     return fd;
}

/*****************************************************************************/
/**
 * From the url create a temporary file to hold a copy of th URL contents.
 *
 * @param[in] url           Pointer to url string
 * @param[out] tmpFileName  Pointer to place tmp file name string
 * @param[in] maxTmpFileLen
 *
 * @return -1 on error or file descriptor
 *
 *****************************************************************************/
static int createTmpFile(const char * fileName,  
                                     char * tmpFilePath, int maxTmpFilePathLen)
{
     int fd = -1;
     const char * root;
     const pid_t pid = getpid();

     D("Creating temp file for '%s'\n", fileName);

     root = getenv("MOZPLUGGER_TMP");
     if(root)
     {
          int soFar;

          strncpy(tmpFilePath, root, maxTmpFilePathLen);
          soFar = strlen(tmpFilePath);

	  soFar += snprintf(&tmpFilePath[soFar], maxTmpFilePathLen-soFar, 
                                                                "/tmp-%i", pid);
          if( (mkdir(tmpFilePath, S_IRWXU) == 0) || (errno == EEXIST))
          {
               D("Creating temp file in '%s'\n", tmpFilePath);

               tmpFilePath[soFar++] = '/';

	       fd = guessTmpFile(fileName, soFar, tmpFilePath, maxTmpFilePathLen);
          }
     }

     if(fd < 0)
     {
          root = getenv("TMPDIR");
          if(!root)
          {
               root = "/tmp";
          }

          snprintf(tmpFilePath, maxTmpFilePathLen, "%s/mozplugger-%i", 
                                                                     root, pid);
          if((mkdir(tmpFilePath, S_IRWXU) == 0) || (errno == EEXIST))
          {
                int soFar = strlen(tmpFilePath);

                D("Creating temp file in '%s'\n", tmpFilePath);

                tmpFilePath[soFar++] = '/';

	        fd = guessTmpFile(fileName, soFar, tmpFilePath, maxTmpFilePathLen);
          }
     }

     if(fd >= 0)
     {
          D("Opened temporary file '%s'\n", tmpFilePath);
     }
     return fd;
}

/*****************************************************************************/
/**
 * NPP New stream
 * Open a new stream.
 * Each instance can only handle one stream at a time.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] type     The mime type
 * @param[in] stream   Pointer to the stream data structure
 * @param[in] seekable Flag to say if stream is seekable
 * @param[out] stype   How the plugin will handle the stream
 *
 * @return Returns error code if a problem
 *
 *****************************************************************************/
NPError NPP_NewStream(NPP instance,
		      NPMIMEType type,
		      NPStream *stream, 
		      NPBool seekable,
		      uint16_t *stype)
{
     char fileName[SMALL_BUFFER_SIZE];
     char * savedMimetype = NULL;
     data_t * THIS;
     char refind_command = 0;

     D("NPP_NewStream() - instance=%p\n", instance);

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

          if(!find_command(THIS, 0))
          {
                NPN_MemFree(THIS->mimetype);
                THIS->mimetype = savedMimetype;
                find_command(THIS, 0);
          }
          else
          {
              NPN_MemFree(savedMimetype);
          }
     }
     else if(refind_command)
     {
          find_command(THIS, 0);
          D("Mime type %s\n", type);
     }

     /* Extract from the URL the various additional information */
     parseURL(THIS, THIS->url, fileName, sizeof(fileName)/sizeof(char)-1);

     /* Extract the fileName from HTTP headers, overide URL fileName */
     parseHeaders(THIS, stream->headers, fileName, sizeof(fileName)/sizeof(char)-1);

     if(THIS->command == 0)
     {
	  NPN_Status(instance, 
                         "MozPlugger: No appropriate application found.");
	  return NPERR_GENERIC_ERROR;
     }

     if( (THIS->cmd_flags & H_STREAM) == 0)
     {
          char tmpfile[LARGE_BUFFER_SIZE];

          THIS->tmpFileFd = createTmpFile(fileName, tmpfile, sizeof(tmpfile));

          if(THIS->tmpFileFd < 0)
          {
	       NPN_Status(instance, "MozPlugger: Failed to create tmp file");
	       return NPERR_GENERIC_ERROR;
          }
          else
          {
               THIS->tmpFileName = NP_strdup(tmpfile);
               THIS->tmpFileSize = 0;
          }
     }
     else
     {
         new_child(instance, THIS->url, 1);
     }

     *stype = NP_NORMAL;
     return NPERR_NO_ERROR;
}

/*****************************************************************************/
/**
 * NPP stream as file
 * Called after NPP_NewStream if *stype = NP_ASFILEONLY.
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] stream   Pointer to the stream data structure
 * @param[in] fname    Name of the file to stream
 *
 * @return none
 *
 *****************************************************************************/
void NPP_StreamAsFile(NPP instance,
		      NPStream *stream,
		      const char* fname)
{
     D("NPP_StreamAsFile() - instance=%p\n", instance);

     if (instance != NULL)
     {
          new_child(instance, fname, 0);
     }
}

/*****************************************************************************/
/**
 * The browser should have resized the window for us, but this function was
 * added because of a bug in Mozilla 1.7 (see Mozdev bug #7734) and
 * https://bugzilla.mozilla.org/show_bug.cgi?id=201158
 *
 * Bug was fixed in Mozilla CVS repositary in version 1.115 of
 * ns4xPluginInstance.cpp (13 Nov 2003), at the time version was 0.13.
 * version 0.14 happened on 14th July 2004
 *
 * @param[in] instance Pointer to plugin instance data
 *
 * @return none
 *
 ****************************************************************************/
static void resize_window(NPP instance)
{
     XSetWindowAttributes attrib;
     data_t * THIS;

     if(!does_browser_have_resize_bug())
     {
          return;
     }
     THIS = instance->pdata;

     attrib.override_redirect = True;
     XChangeWindowAttributes(THIS->display, (Window)THIS->windata.window,
			     (unsigned long) CWOverrideRedirect, &attrib);
     
     D("Bug #7734 work around - resizing WIN 0x%x to %ux%u!?\n", 
            (unsigned) THIS->windata.window,
            (unsigned) THIS->windata.width, (unsigned) THIS->windata.height);

     XResizeWindow(THIS->display, (Window)THIS->windata.window,
		   (unsigned) THIS->windata.width, 
                   (unsigned) THIS->windata.height);
}

/*****************************************************************************/
/**
 * NPP Set window
 * The browser calls NPP_SetWindow after creating the instance to allow drawing
 * to begin. Subsequent calls to NPP_SetWindow indicate changes in size or 
 * position. If the window handle is set to null, the window is destroyed. In
 * this case, the plug-in must not perform any additional graphics operations
 * on the window and should free any associated resources.
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] window   Pointer to NPWindow data structure
 *
 * @return Returns error code if problem
 *
 ****************************************************************************/
NPError NPP_SetWindow(NPP instance, NPWindow* window)
{
     data_t * THIS;
     D("NPP_SetWindow() - instance=%p\n", instance);

     if (!instance)
     {
	  return NPERR_INVALID_INSTANCE_ERROR;
     }

     if (!window)
     {
	  return NPERR_NO_ERROR;
     }
 
     /* TODO - Should really pass to helper / controller the fact that the
      * window has disappeared! - need to check the consequences first
      * before deleting these 2 lines */
     if (!window->window)
     {
          D("SetWindow() - NULL window passed in so exit\n");
	  return NPERR_NO_ERROR;
     }

     if (!window->ws_info)
     {
	  return NPERR_NO_ERROR;
     }

     THIS = instance->pdata;

     THIS->display = ((NPSetWindowCallbackStruct *)window->ws_info)->display;
     THIS->displayname = XDisplayName(DisplayString(THIS->display));
     THIS->windata = *window;

     if ((THIS->url) && (THIS->browserCantHandleIt))
     {
          if(THIS->command == 0)
          {
               /* Can only use streaming commands, as Mozilla cannot handle
                * these types (mms) of urls */
               if (!find_command(THIS, 1))
               {
                    if(errMsg)
                    {
                         NPN_Status(instance, errMsg);
                         errMsg = NULL;
                    }
                    else
                    {
	                 NPN_Status(instance, 
                          "MozPlugger: No appropriate application found.");
                    }
 	            return NPERR_GENERIC_ERROR;
               }
          }

          /* Extract from the URL the various additional information */
          parseURL(THIS, THIS->url, 0, 0);

	  new_child(instance, THIS->url, 1);
          THIS->url = NULL;                 /* Stops new_child from being called again */
	  return NPERR_NO_ERROR;
     }

     if (THIS->commsPipeFd >= 0)
     {
          ssize_t ret;
	  D("Writing WIN 0x%x to fd %d\n", (unsigned) window->window, 
                                                             THIS->commsPipeFd);
	  ret = write(THIS->commsPipeFd, (char *)window, sizeof(*window));
          if(ret < sizeof(*window))
          {
               D("Writing to comms pipe failed\n");
               close(THIS->commsPipeFd);
               THIS->commsPipeFd = -1;
          }
     }

     resize_window(instance);

     /* In case Mozilla would call NPP_SetWindow() in a loop. */
     usleep(4000);

//     get_browser_toolkit(instance);
//     does_browser_support_key_handling(instance);

     return NPERR_NO_ERROR;
}

/*****************************************************************************/
/**
 * NPP Destroy stream
 * Called from the Browser when the streaming has been completed  by the Browser
 * (the reason code indicates whether this was due to a User action, Network
 * issue or that streaming has been done.
 *
 * @param[in] instance Pointer to plugin instance data
 * @param[in] stream   Pointer to the stream data structure
 * @param[in] reason   Reason for stream being destroyed
 *
 * @return Returns error code if a problem
 *
 *****************************************************************************/
NPError NPP_DestroyStream(NPP instance, NPStream *stream, NPError reason)
{
     D("NPP_DestroyStream() - instance=%p\n", instance);
     
     if (!instance)
     {
	  return NPERR_INVALID_INSTANCE_ERROR;
     }

     data_t * const THIS = instance->pdata;
          
     if(THIS->tmpFileFd >= 0)
     {
          close(THIS->tmpFileFd);
          THIS->tmpFileFd = -1;

          if( THIS->tmpFileName != NULL)
          {
               D("Closing Temporary file \'%s\'\n", THIS->tmpFileName);     
               if(THIS->commsPipeFd < 0)
               {
                    new_child(instance, THIS->tmpFileName, 0);
               }
          }
          else
          {
              D("Closing stdin pipe\n");
          }
     }
     return NPERR_NO_ERROR;
}

/*****************************************************************************/
/**
 * NPP Initialize
 * The browser calls this function only once; when the plug-in is loaded, 
 * before the first instance is created. NPP_Initialize tells the plug-in that
 * the browser has loaded it.
 *
 * @return Returns error code if a problem
 *
 *****************************************************************************/
NPError NPP_Initialize(void)
{
     D("NPP_Initialize(void)\n");

     get_api_version();

     browserSupportsXEmbed = does_browser_support_xembed();

     do_read_config();

     {
          const int free = MAX_STATIC_MEMORY_POOL - staticPoolIdx;
          D("Static Pool used=%i, free=%i\n", staticPoolIdx, free);
     }

     return NPERR_NO_ERROR;
}

/*****************************************************************************/
/**
 * NPP Shutdown
 * The browser calls this function just before it unloads the plugin from
 * memory. So this function should do any tidy up - in this case nothing is
 * required.
 *
 * @return none
 *
 *****************************************************************************/
void NPP_Shutdown(void)
{
     D("NPP_Shutdown()\n");
}

/*****************************************************************************/
/**
 * NPP Print
 * Called when user as requested to print the webpage that contains a visible
 * plug-in. For mozplugger this is ignored.
 *
 * @param[in] instance  Pointer to the plugin instance data
 * @param[in] printInfo Pointer to the print info data structure
 *
 * @return none
 *
 *****************************************************************************/
void NPP_Print(NPP instance, NPPrint* printInfo)
{
     D("NPP_Print() - instance=%p\n", instance);
}

/*****************************************************************************/
/**
 * NPP write
 * Called when the Browser wishes to deliver a block of data from a stream to
 * the plugin. Since streaming is handled directly by the application specificed
 * in the configuration file, mozplugger has no need for this data. Here it
 * just pretends the data has been taken by returning 'len' to indicate all 
 * bytes consumed. Actaully this function should never be called by the 
 * Browser.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] stream   Pointer to the stream data structure
 * @param[in] offset   Where the data starts in 'buf'
 * @param[in] len      The amount of data
 * @param[in] buf      The data to be delivered
 *
 * @return Always returns value of passed in 'len'
 *
 *****************************************************************************/
int32_t NPP_Write(NPP instance, NPStream *stream, int32_t offset, int32_t len,
		  void * buf)
{
     D("NPP_Write(%d,%d) - instance=%p\n", offset, (int) len, instance);
     if(instance)
     {
          data_t * const THIS = instance->pdata;
          
          if(THIS->tmpFileFd >= 0)
          {
               if(offset != THIS->tmpFileSize)
               {
                   D("Strange, there's a gap?\n");
               }
               len = write(THIS->tmpFileFd, buf, len);
               THIS->tmpFileSize += len;
               D("Temporary file size now=%i\n", THIS->tmpFileSize);
          }
     }
     return len;
}

/*****************************************************************************/
/**
 * NPP Write ready
 * Browser calls this function before calling NPP_Write to see how much data
 * the plugin is ready to accept. Since streaming is handled directly by the
 * application specified in the configuration file, mozplugger does not need
 * the stream data from the Browser, therefore this function terminates
 * the streaming by calling NPN_DestoryStream.
 *
 * @param[in] instance Pointer to the plugin instance data
 * @param[in] stream   Pointer to the stream data structure
 *
 * @return Always returns zero
 *
 *****************************************************************************/
int32_t NPP_WriteReady(NPP instance, NPStream *stream)
{
     int32_t size = 0;

     D("NPP_WriteReady() - instance=%p\n", instance);
     if (instance != 0)
     {
          data_t * const THIS = instance->pdata;

          if(THIS->tmpFileFd >= 0)
          {     
               size = CHUNK_SIZE;
          }
          else
          {
               D("Nothing to do - Application will handle stream\n");
               /* STREAM, close NPAPI stream and pass URL to application */
          }

          if(size == 0)
          {
	       /* Tell the browser that it can finish with the stream
   	          (actually we just wanted the name of the stream!)
	          And not the stream data. */
               NPN_DestroyStream(instance, stream, NPRES_DONE);
          }
     }
     return size;
}

/*****************************************************************************/
/**
 * NPP URL Notify
 * Browser calls this function to notify when a GET or POST has completed
 * Currently not used by mozplugger
 *
 * @param[in] instance   Pointer to the plugin instance data
 * @param[in] url        The URL that was GET or POSTed
 * @param[in] reason     The reason for the notify event
 * @param[in] notifyData Data that was passed in the original call to Get or Post URL
 *
 * @return none
 *
 *****************************************************************************/
void NPP_URLNotify(NPP instance, const char * url, NPReason reason, void * notifyData)
{
     D("NPP_URLNotify() - instance=%p url=%s reason=%i\n", instance, url, reason);
}

/*****************************************************************************/
/**
 * NPP Got Focus
 * Called by the browser when the browser intends to focus an instance.
 * Instance argument indicates the instance getting focus.
 * Direction argument indicates the direction in which focus advanced to the instance.
 * Return value indicates whether or not the plugin accepts focus.
 * Currently not used by mozplugger
 *
 * @param[in] instance   Pointer to the plugin instance data
 * @param[in] direction  The advancement direction
 *
 * @return True or False
 *
 *****************************************************************************/
NPBool NPP_GotFocus(NPP instance, NPFocusDirection direction)
{
    D("NPP_GotFocus() - instance=%p, direction=%i\n", instance, direction);
    return false;
}

/*****************************************************************************/
/**
 * NPP Lost Focus
 * Called by the browser when the browser intends to take focus.
 * Instance argument indicates the instances losing focus.
 * There is no return value, plugins will lose focus when this is called.
 * Currently not used by mozplugger
 *
 * @param[in] instance   Pointer to the plugin instance data
 *
 * @return True or False
 *
 *****************************************************************************/
void NPP_LostFocus(NPP instance)
{
     D("NPP_LostFocus() - instance=%p\n", instance);
}

/*****************************************************************************/
/**
 * NPP URL Redirect Notify
 * Currently not used by mozplugger
 *
 * @param[in] instance   Pointer to the plugin instance data
 * @param[in] url        The URL that was GET or POSTed
 * @param[in] status     ??
 * @param[in] notifyData Data that was passed in the original call to Get or Post URL
 *
 * @return None
 *
 *****************************************************************************/
void NPP_URLRedirectNotify(NPP instance, const char * url, int32_t status, void * noifyData)
{
     D("NPP_URLRedirectNotify() - instance=%p, url=%s, status=%i\n", instance, url, status);
}

/*****************************************************************************/
/**
 * NPP Clear Site Data
 * Clear site data held by plugin (should this clear tmp files?)
 * Currently not used by mozplugger
 *
 * @param[in] site   The site name
 * @param[in] flags
 * @param[in] maxAge
 *
 * @return Error status
 *
 *****************************************************************************/
NPError NPP_ClearSiteData(const char * site, uint64_t flags, uint64_t maxAge)
{
     D("NPP_ClearSiteData() - site=%s\n", site);
     return NPERR_NO_ERROR;
}

/*****************************************************************************/
/**
 * NPP Ge Site Data
 * Get list of sites plugin has data for (should this be list of tmp files?)
 * Currently not used by mozplugger
 *
 * @param[in] site   The site name
 * @param[in] flags
 * @param[in] maxAge
 *
 * @return Error status
 *
 *****************************************************************************/
char** NPP_GetSitesWithData(void)
{
    D("NPP_GetSitesWithData()\n");
    return 0;
}
