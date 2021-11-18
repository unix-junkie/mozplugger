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
#include <ctype.h>      /* For isalnum() */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>     /* For the stat() function */
#include <sys/wait.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

#ifdef HAVE_GETPWUID
#include <pwd.h> /* For alternative way to find HOME dir */
#endif

#include "cmd_flags.h"
#include "plugin_name.h"

#define MAX_CONFIG_LINE_LEN (256)
#define MAX_FILE_PATH_LEN (512)

#define DEF_PLUGIN "[MozPlugger base Plugin]"

/**
 * Element of linked list of commands created when parsing config file
 */
struct command_s
{
     unsigned int flags;
     const char * cmd;
     const char * winname;
     const char * fmatchStr;

     struct command_s * pNext;
};

typedef struct command_s command_t;

/**
 * Element of linked list of mimetypes created when parsing config file
 */
struct mimetype_s
{
     const char * type;
     const char * desc;

     struct mimetype_s * pNext;
};

typedef struct mimetype_s mimetype_t;

/**
 * Element of linked list of handlers created when parsing config file
 */
struct handler_s
{
     mimetype_t * types;
     command_t * cmds;

     struct handler_s * pNext;
};

typedef struct handler_s handler_t;

/**
 * Element of linked list of plugin types created when parsing config file
 */
struct pluginType_s
{
     const char * name;
     const char * version;
     handler_t * handlers;

     struct pluginType_s * pNext;
};

typedef struct pluginType_s pluginType_t;

/**
 * Linked list of applications that have been tested
 */

struct appCacheEntry_s
{
     const char * shortName;
     const char * fullName; /* NULL if not found in path */

     struct appCacheEntry_s * pNext;
};

typedef struct appCacheEntry_s appCacheEntry_t;

/**
 * List of possible places to look for apps and config
 */
struct cfgPath_s
{
     const char * fmtStr;
     const char * envStr;
};

typedef struct cfgPath_s cfgPath_t;

/**
 * List of browser user plugin directories
 */
struct browser_s
{
    const char * localPluginDir;
    const cfgPath_t * cfgPaths;
};

typedef struct browser_s browser_t;

/**
 * Global variables
 */

static appCacheEntry_t * g_cache = NULL;
static bool g_verbose = false;


#ifdef __GNUC__
static void LOG_DEBUG(const char * fmt, ...) __attribute__((format(printf,1,2)));
static void LOG_INFO(const char * fmt, ...) __attribute__((format(printf,1,2)));
static void NOTICE(const char * fmt, ...) __attribute__((format(printf,1,2)));
static void WARNING(const char * fmt, ...) __attribute__((format(printf,1,2)));
static void ERROR(const char * fmt, ...) __attribute__((noreturn,format(printf,1,2)));
#endif

/**
 * Take the standard printf type arguments and write to stdout if verbose is
 * set.
 *
 * @param[in] fmt The printf format args
 * @param[in] ... The printf style arguments
 */
static void LOG_DEBUG(const char * fmt, ...)
{
     va_list ap;
     va_start(ap,fmt);
     if(g_verbose)
     {
          vprintf(fmt,ap);
     }
     va_end(ap);
}

/**
 * Take the standard printf type arguments and write to stdout if verbose is
 * set.
 *
 * @param[in] fmt The printf format args
 * @param[in] ... The printf style arguments
 */
static void LOG_INFO(const char * fmt, ...)
{
     va_list ap;
     va_start(ap,fmt);
     vprintf(fmt,ap);
     va_end(ap);
}

/**
 * Take the standard printf type arguments and write to stdout if verbose is
 * set.
 *
 * @param[in] fmt The printf format args
 * @param[in] ... The printf style arguments
 */
static void NOTICE(const char * fmt, ...)
{
     va_list ap;
     va_start(ap,fmt);
     printf("NOTICE: ");
     vprintf(fmt,ap);
     va_end(ap);
}

/**
 * Take the standard printf type arguments and write to stdout if verbose is
 * set.
 *
 * @param[in] fmt The printf format args
 * @param[in] ... The printf style arguments
 */
static void WARNING(const char * fmt, ...)
{
     va_list ap;
     va_start(ap,fmt);
     printf("WARNING: ");
     vprintf(fmt,ap);
     va_end(ap);
}

/**
 * Take the standard printf type arguments and write to stdout if verbose is
 * set.
 *
 * @param[in] fmt The printf format args
 * @param[in] ... The printf style arguments
 */
static void ERROR(const char * fmt, ...)
{
     va_list ap;
     va_start(ap,fmt);
     printf("ERROR: ");
     vprintf(fmt,ap);
     va_end(ap);
     exit(EXIT_FAILURE);
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
 * Check the path exists and if so allocate memory to hold the path name
 *
 * @param[in] fname full path to application
 *
 * @return Pointer to name or NULL
 */
static char * chk_path_and_alloc(const char * fname)
{
     struct stat buf;
     if (stat(fname, &buf) != 0)
     {
	  LOG_DEBUG("chk_path_and_alloc(%s) = no\n", fname);
          return NULL;
     }
     LOG_DEBUG("chk_path_and_alloc(%s) = yes\n", fname);
     return strdup(fname);
}

/**
 * Find the config file or the helper file in the list of possible locations
 * (paths) and exit when found.
 *
 * @param[in] paths List of possible paths.
 * @param[in] basename File or app name (without path).
 *
 * @return allocated memory containing path to the helper
 */
static char * find_helper_file(const cfgPath_t * paths, const char * basename)
{
     char * retVal = NULL;

     LOG_DEBUG("find_helper_file '%s'\n", basename);

     for(;paths->fmtStr && !retVal; paths++)
     {
          char fname[MAX_FILE_PATH_LEN];

          if(paths->envStr)
          {
               const char * tmp = (strcmp("HOME", paths->envStr) == 0) ? get_home_dir() : getenv(paths->envStr);
               if(tmp)
               {
                    char * const tmp2 = strdup(tmp);
                    {
                         char * wip = tmp2;
                         char * sep = NULL;
                         do
                         {
                              if( (sep = strchr(wip, ':')) != NULL)
                              {
                                   *sep = '\0';
                                   if(sep[-1] == '/')
                                   {
                                       sep[-1] = '\0';
                                   }
                              }
                              snprintf(fname, sizeof(fname), paths->fmtStr, wip, basename);
                              retVal = chk_path_and_alloc(fname);
                              if(sep)
                              {
                                    wip = &sep[1];
                              }
                         }
                         while(sep && !retVal);
                    }
                    free(tmp2);
               }
          }
          else
          {
               snprintf(fname, sizeof(fname), paths->fmtStr, basename);
               retVal = chk_path_and_alloc(fname);
          }
     }
     return retVal;
}


/**
 * Check if application 'file' exists. Uses a cache of previous finds to
 * avoid performing a stat call on the same file over and over again.
 *
 * @param[in] file The application to find
 *
 * @return Pointer to full path or NULL.
 */
static const char * find_application(const char * file)
{
     /* Places to search for the applications */
     static const cfgPath_t binPaths[] =
     {
          {"%s/%s", "PATH"},
          {NULL,    NULL}
     };

     struct stat filestat;
     appCacheEntry_t * p;

     LOG_DEBUG("find(%s)\n", file);

     for(p = g_cache; p; p = p->pNext)
     {
          if( strcmp(p->shortName, file) == 0)
          {
               return p->fullName;
          }
     }

     p = malloc(sizeof(appCacheEntry_t));
     memset(p, 0, sizeof(appCacheEntry_t));
     p->shortName = strdup(file);

     if (file[0] == '/')
     {
	  if(stat(file, &filestat) == 0)
          {
               p->fullName = strdup(file);
          }
     }
     else
     {
          const char * q = find_helper_file(binPaths, file);
          if(q)
          {
               p->fullName = q;
          }
     }

     if(p->fullName)
     {
          LOG_INFO("found '%s' at '%s'\n", p->shortName, p->fullName);
     }
     else
     {
          NOTICE("could not find '%s'\n", p->shortName);
     }
     p->pNext = g_cache;
     g_cache = p;
     return p->fullName;
}

/**
 * Delete a entry from the application cache.
 *
 * @param[in] The entry to delete
 */
static void del_cache_entry(appCacheEntry_t * entry)
{
     entry->pNext = NULL;
     free((char *) entry->shortName);
     free((char *) entry->fullName);
     free(entry);
}

/**
 * Delete the application cache
 */
static void delete_cache(void)
{
     while(g_cache)
     {
          appCacheEntry_t * entry = g_cache;
          g_cache = entry->pNext;
          del_cache_entry(entry);
     }
}

/**
 * get parameter from mozpluggerrc.  Match a word to a flag that takes a
 * parameter e.g. swallow(name).
 *
 * @param[in] x Pointer to position in text being parsed
 * @param[in] flag Pointer to flag being checked for
 * @param[out] c Pointer to the found parameter
 *
 * @return Pointer to new position in text being parsed or NULL if error
 */
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
          ERROR("expected '(' after '%s'\n", flag);
     }
     x++;
     end = strchr(x,')');
     if (end)
     {
	  len = end - x;
	  *c = strndup(x, len);
          /* No need to test if NULL as test done else where */
	  x = end+1;
     }
     else
     {
          ERROR("expected ')'\n");
     }
     return x;
}

/**
 * Checks if 'line' starts with 'kw' (keyword), if 'line' is longer than 'kw',
 * checks that there is not an additional alpha-numeric letter in the 'line'
 * after the 'kw'.
 *
 * @param[in] line The input text line being parsed
 * @param[in] kw The keyword to match against
 *
 * @return 1(true) if match, 0(false) if not
 */
static int match_word(const char *line, const char *kw)
{
     const unsigned kwLen = strlen(kw);

     return (strncasecmp(line, kw, kwLen) == 0) && !isalnum(line[kwLen]);
}

/**
 * Parse for flags. Scan a line for all the possible flags.
 *
 * @param [in,out] x The position in the line that is being parsed
 *                         (before & after)
 * @param[in,out] commandp The data structure to hold the details found
 *
 * @return 1(true) if sucessfully parsed, 0(false) otherwise
 */
static int parse_flags(char **x, command_t *commandp)
{
     const static str2flag_t flags[] =
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

     const str2flag_t *f;
     int retVal = false;

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
                         retVal = true;
                         break;
                    }
               }
               else if(f->value & H_FMATCH)
               {
	            char * p = get_parameter(*x, f->name, &commandp->fmatchStr);
                    if(p)
                    {
                         *x = p;
                         retVal = true;
                         break;
                    }
               }
               else
               {
	            retVal = true;
                    break;
               }
	  }
     }
     return retVal;
}

/**
 * Trim the line buffer and if the line is empty or contains just a comment
 * return false
 *
 * @param[in] buffer The line read
 *
 * @return true if not a blank line
 */
static bool chk_cfg_line(char * buffer)
{
     char * end;

     if(buffer[0] == '#')
     {
          return false;
     }
     for(end = &buffer[strlen(buffer)-1]; end >= buffer; end--)
     {
          if((*end != '\r') && (*end != '\n') && (*end != '\t') && (*end != ' '))
          {
               end[1] = '\0';
               return true;
          }
     }
     return false;
}


/**
 * Move char pointer over any space of tab characters
 *
 * @param[in] x pointer to current character in array of characters
 *
 * @return Pointer to the next non-space character
 */
static char * skip_spaces(char * x)
{
     while((*x == ' ') || (*x == '\t'))
     {
          x++;
     }
     return x;
}

/**
 * Move the end back towards the start until a non-space is found
 *
 * @param[in] start Pointer to string start
 * @param[in] end Pointer to string end
 *
 * @return new end
 */
static char * skip_end_spaces(const char * start, char * end)
{
    for(; ((end > start) && ((end[-1] == ' ') || (end[-1] == '\t'))); end--)
          ;
    return end;
}

/**
 * Test if the line buffer contains a mimetype
 *
 * @param[in] buffer The line buffer
 *
 * @return true if contains a mimetype
 */
static bool is_cfg_mimetype(const char * buffer)
{
    return !isspace(buffer[0]);
}

/**
 * Parse the mimetype in the cfg file
 *
 * @param[in] buffer The line read from config file
 *
 * @return a mimetype_t structure if valid mimetype.
 */
static mimetype_t * parse_cfg_mimetype(char * buffer)
{
     char * start_desc;
     char * end_type;
     mimetype_t * type = (mimetype_t *) malloc(sizeof(mimetype_t));
     if(type == 0)
     {
           ERROR("Failed to alloc memory for mimetype\n");
     }
     memset(type, 0, sizeof(mimetype_t));

     if(!(start_desc = strchr(buffer, ':')))
     {
           ERROR("mime type has no description '%s'\n", buffer);
     }
     end_type = skip_end_spaces(buffer, start_desc);
     if( (type->type = strndup(buffer, end_type-buffer)) == 0)
     {
           ERROR("Failed to alloc memory for mimetype\n");
     }
     type->desc = strdup(skip_spaces(&start_desc[1]));
     LOG_DEBUG("New mime type - '%s':'%s'\n", type->type, type->desc);
     return type;
}

/**
 * Parse the command found in the cfg file
 *
 * @param[in] buffer The line read from config file
 *
 * @return a command structure if command found.
 */
static command_t * parse_cfg_cmd(char * buffer)
{
     command_t * cmd;
     char * x = skip_spaces(&buffer[1]);
     char * p;

     if(!(cmd = (command_t *) malloc(sizeof(command_t))))
     {
           ERROR("Failed to alloc memory for command\n");
     }
     memset(cmd, 0, sizeof(command_t));

     LOG_DEBUG("-- parsing cmd line %s\n", x);

     /* Parse the flags... */
     while (*x != ':' && *x)
     {
          if (*x == ',' || *x == ' ' || *x == '\t')
	  {
		x++;
	  }
	  else if (!parse_flags(&x, cmd))
	  {
               ERROR("unknown directive %s\n", x);
          }
     }

     /* Parse the command */
     if (*x != ':')
     {
          ERROR("Config error - no command? (%s)\n", x);
     }

     x = skip_spaces(&x[1]);

     if(strcmp(x, "if ") == 0)
     {
         x = skip_spaces(&x[2]);
     }

     /* Extract just the application name */
     p = strndup(x, strchr(x, ' ') - x);

     if (!find_application(p))
     {
          cmd = NULL;
     }
     else
     {
          cmd->cmd = strndup(x, strlen(x));
     }
     free(p);
     return cmd;
}

/**
 * Is buffer containing a plugin name
 *
 * @param[in] buffer The line buffer
 *
 * @return true or false
 */
static bool is_cfg_plugin_name(const char * buffer)
{
     return (buffer[0] == '[');
}

/**
 * parse the plugin name
 *
 * @param[in] buffer The line buffer
 * @param[out] pVersion Place to put pointer to version string
 *
 * @return pointer to the plugin name
 */
static char * parse_cfg_plugin_name(char * buffer, char ** pVersion)
{
     char * name = NULL;
     char * version = NULL;
     char * start = skip_spaces(&buffer[1]);
     char * end = strchr(buffer, ']');
     char * start_ver;

     if(!end)
     {
          ERROR("Missing closing ]  at line '%s'\n", buffer);
     }
     end = skip_end_spaces(start, end);

     if( !(start_ver = strchr(buffer, '@')))
     {
          name = strndup(start,end-start);
     }
     else
     {
          char * end_name = start_ver;
          end_name = skip_end_spaces(start, end_name);
          name = strndup(start,end_name-start);

          start_ver = skip_spaces(&start_ver[1]);
          version = strndup(start_ver, end-start_ver);
     }

     *pVersion = version;
     return name;
}

/**
 * Get the plugin tree associated with plugin name in the line buffer.
 *
 * @param[in] buffer The line buffer
 * @param[in,out] pplugins Pointer to the linked list of plugins
 *
 * @return the plugin
 */
static pluginType_t * get_plugin(char * buffer, pluginType_t ** pplugins)
{
     char * version = NULL;
     char * name = parse_cfg_plugin_name(buffer, &version);

     pluginType_t * new;
     pluginType_t * plugin = *pplugins; /* Get first plugin in linked list */

     while(plugin)
     {
          if(strcmp(plugin->name, name) == 0)
          {
               if(version)
               {
                    if(plugin->version)
                    {
                         if(strcmp(version, plugin->version) != 0)
                         {
                              ERROR("Version mismatch at line '%s'\n", buffer);
                         }
                         free(version);
                    }
                    else
                    {
                         plugin->version = version;
                    }
               }
               LOG_DEBUG("Using previous plugin '%s'\n", plugin->name);
               free(name);
               return plugin;
          }
          if(!plugin->pNext)
          {
               break;
          }
          plugin = plugin->pNext;
     }

     if( !(new = malloc(sizeof(pluginType_t))) )
     {
          ERROR("malloc error\n");
     }
     memset(new, 0, sizeof(pluginType_t));

     new->name = name;
     new->version = version;
     if(plugin)
     {
          plugin->pNext = new;
     }
     else
     {
          *pplugins = new;
     }
     LOG_DEBUG("Using new plugin '%s'\n", new->name);
     return new;
}

/**
 * Create a handler that holds the mimetype to command mappings
 *
 * @param[in] plugin The current plugin
 *
 * @return A new handler
 */
static handler_t * create_handler(pluginType_t * plugin)
{
     handler_t * handler = (handler_t *) malloc(sizeof(handler_t));
     if(!handler)
     {
          ERROR("malloc error\n");
     }
     memset(handler, 0, sizeof(handler_t));

     if(plugin->handlers)
     {
          handler_t * prev_handler = plugin->handlers;
          while(prev_handler->pNext)
          {
               prev_handler = prev_handler->pNext;
          }
          prev_handler->pNext = handler;
     }
     else
     {
          plugin->handlers = handler;
     }
     return handler;
}

/**
 * Read the configuration file into memory.
 *
 * @param[in] f The FILE pointer
 *
 * @return A allocated and filled in pluginType_t
 */
static pluginType_t * read_config(FILE * f)
{
     pluginType_t * plugins = NULL;
     pluginType_t * plugin = get_plugin(DEF_PLUGIN, &plugins);
     handler_t * handler = NULL;

     command_t * prev_cmd = NULL;
     mimetype_t * prev_type = NULL;

     char buffer[MAX_CONFIG_LINE_LEN];
     int lineNum = 0;

     plugin->version = strdup(VERSION);
     while (fgets(buffer, sizeof(buffer), f))
     {
          lineNum++;
          if(!chk_cfg_line(buffer))
          {
               continue;
          }

	  LOG_DEBUG("%5i::|%s|\n", lineNum, buffer);

          if(is_cfg_plugin_name(buffer))
          {
               plugin = get_plugin(buffer, &plugins);
               prev_type = NULL;
               prev_cmd = NULL;
          }
          else if(is_cfg_mimetype(buffer))
	  {
	       /* Mime type */
               mimetype_t * type = parse_cfg_mimetype(buffer);
               prev_cmd = NULL;

               if(prev_type)
               {
                    /* Add to end of linked list of handlers */
                    prev_type->pNext = type;
               }
               else
               {
                    /* New handler required */
                    handler = create_handler(plugin);
                    handler->types = type;
               }
               /* Remember the current end of linked list */
               prev_type = type;
	  }
	  else
	  {
               command_t * cmd = parse_cfg_cmd(buffer);
	       prev_type = NULL;
               if(cmd)
               {
                    if(prev_cmd)
                    {
                         /* Add to end of linked list of handlers */
                         prev_cmd->pNext = cmd;
                    }
                    else if(handler)
                    {
                         /* Add at header of link list of handlers */
                         handler->cmds = cmd;
                    }
                    else
                    {
                         ERROR("Unexpected command before mimetypes '%s'\n", buffer);
                    }
                    /* Remember the current end of linked list */
                    prev_cmd = cmd;
               }
          }
     }
     return plugins;
}


/**
 * Read config callback function. Open and run the configuration file (fname)
 * through M4 and then call read_config() to parse the output
 *
 * @param[in] fname Full name (and path) of configuration file
 * @param[out] m4_pid The pid of the m4 process
 *
 * @return Open file to read config from
 */
static FILE * open_config(const char * fname, pid_t * m4_pid)
{
     int m4out[2];
     pid_t pid;

     int fd = open(fname, O_RDONLY);
     if (fd < 0)
     {
          ERROR("Could not open '%s'\n", fname);
     }
     LOG_INFO("Parsing '%s'\n", fname);

     if (pipe(m4out) < 0)
     {
	  perror("failed to create pipe");
	  exit(EXIT_FAILURE);
     }

     pid = fork();

     if(pid == -1)
     {
          ERROR("Failed to fork\n");
     }
     else if(pid == 0)
     {
	  close(m4out[0]);

	  dup2(m4out[1], 1);
	  close(m4out[1]);

	  dup2(fd, 0);
	  close(fd);

	  execlp("m4", "m4", NULL);
	  ERROR("Failed to execute m4.\n");
     }
     else
     {
          FILE * fp = fdopen(m4out[0], "r");

	  close(m4out[1]);
	  close(fd);

	  if (!fp)
          {
               ERROR("Failed to open pipe\n");
          }
          *m4_pid = pid;
          return fp;
     }
}

/**
 * Close the config file
 *
 * @param[in] fp The config file object
 * @param[in] m4_pid The PID of the m4 process
 */
static void close_config(FILE * fp, pid_t m4_pid)
{
     int status;

     fclose(fp);

     waitpid(m4_pid, &status, 0);
     LOG_DEBUG("M4 exit status was %i\n", WEXITSTATUS(status));

     if(WEXITSTATUS(status) != 0)
     {
          ERROR("M4 parsing of config generated error\n");
     }
}


/**
 * Find configuration file, helper and controller executables. Call the
 * appropriate xxx_cb function to handle the action (e.g. for configuration
 * file, the parsering and reading into memory).
 *
 * @param[in] plugibName The name of the plugin
 *
 * @return A linked list of pluginTypes
 */
static pluginType_t * do_read_config(const char * config_fname)
{
     pluginType_t * plugins = NULL;
     pid_t m4_pid = 0;
     FILE * fp = NULL;

     fp = open_config(config_fname, &m4_pid);
     if(fp == NULL)
     {
          ERROR("Failed to open '%s'\n", config_fname);
     }

     plugins = read_config(fp);
     close_config(fp, m4_pid);

     return plugins;
}

/**
 * Delete a type
 *
 * @param[in] type The mimetype_t structure
 */
static void delete_type(mimetype_t * type)
{
     type->pNext = NULL;
     free((char *)type->type);
     free(type);
}

/**
 * Delete a command
 *
 * @param[in] cmd The command_t structure
 */
static void delete_cmd(command_t * cmd)
{
     cmd->pNext = NULL;
     free((char *)cmd->cmd);
     free((char *)cmd->winname);
     free((char *)cmd->fmatchStr);
     free(cmd);
}

/**
 * Delete a handler
 *
 * @param[in] handler The handler_t structure
 */
static void delete_handler(handler_t * handler)
{
     handler->pNext = NULL;
     while(handler->types)
     {
         mimetype_t * type = handler->types;
         handler->types = type->pNext;
         delete_type(type);
     }
     while(handler->cmds)
     {
         command_t * cmd = handler->cmds;
         handler->cmds = cmd->pNext;
         delete_cmd(cmd);
     }
     free(handler);
}

/**
 * Delete a plugin
 *
 * @param[in] plugin The pluginType_t structure
 */
static void delete_plugin(pluginType_t * plugin)
{
     plugin->pNext = NULL;
     while(plugin->handlers)
     {
          handler_t * handler = plugin->handlers;
          plugin->handlers = handler->pNext;
          delete_handler(handler);
     }
     free((char *)plugin->name);
     free((char *)plugin->version);
     free(plugin);
}

/**
 * Trim the linked list of plugins
 *
 * @param[in] plugins The linked list of plugins
 *
 * @return The new linked list of plugins
 */
static pluginType_t * trim(pluginType_t * plugins)
{
    pluginType_t * prev_plugin = NULL; /* Skip the default */
    pluginType_t * plugin = plugins;
    while(plugin)
    {
        pluginType_t * next_plugin = plugin->pNext;

        handler_t * prev_handler = NULL;
        handler_t * handler = plugin->handlers;
        while(handler)
        {
            handler_t * next_handler = handler->pNext;
            if(!handler->cmds)
            {
                mimetype_t * type = handler->types;

                if(prev_handler)
                {
                    prev_handler->pNext = handler->pNext;
                }
                else
                {
                    plugin->handlers = handler->pNext;
                }
                for(; type; type = type->pNext)
                {
                    WARNING("ignoring %s associated helpers not found\n", type->type);
                }
                delete_handler(handler);
            }
            else
            {
                prev_handler = handler;
            }
            handler = next_handler;
        }
        if(!plugin->handlers)
        {
            if(prev_plugin)
            {
                 prev_plugin->pNext = plugin->pNext;
            }
            else
            {
                 plugins = plugin->pNext;
            }
            delete_plugin(plugin);
        }
        else
        {
            prev_plugin = plugin;
        }
        plugin = next_plugin;
    }
    return plugins;
}

/**
 * Write the mimetype to the mimetypes and cmds files
 *
 * @param[in] type Pointer to the mimetype structure.
 * @param[in] mimetypes_fp Pointer to the mimetypes file
 * @param[in] cmds_fp Pointer to the cmds file
 */
static void write_type(mimetype_t * type, FILE * mimetypes_fp, FILE * cmds_fp)
{
     LOG_DEBUG("%s\n", type->type);
     fprintf(mimetypes_fp, "%s:%s;", type->type, type->desc);
     fprintf(cmds_fp, "%s\n", type->type);
}

/**
 * Write the cmd to the cmds files
 *
 * @param[in] cmd Pointer to the cmd structure
 * @param[in] cmds_fp Pointer to the cmds file
 */
static void write_cmd(command_t * cmd, FILE * fp)
{
     LOG_DEBUG("\t0x%x\t%s\t%s:%s\n", cmd->flags, cmd->winname, cmd->fmatchStr, cmd->cmd);
     fprintf(fp, "\t%x\t%s\t%s\t%s\n", cmd->flags, cmd->winname ? cmd->winname : "", cmd->fmatchStr ? cmd->fmatchStr : "", cmd->cmd);
}

/**
 * Write the handler to the mimetypes and cmds files
 *
 * @param[in] handler Pointer to the handler structure.
 * @param[in] mimetypes_fp Pointer to the mimetypes file
 * @param[in] cmds_fp Pointer to the cmds file
 */
static void write_handler(handler_t * handler, FILE * mimetypes_fp, FILE * cmds_fp)
{
     mimetype_t * type = handler->types;
     command_t * cmd = handler->cmds;

     for(; type; type = type->pNext)
     {
          write_type(type, mimetypes_fp, cmds_fp);
     }
     for(; cmd; cmd = cmd->pNext)
     {
          write_cmd(cmd, cmds_fp);
     }
}

/**
 * Get the directory where cached config files are put
 *
 * @return allocated string containing the path
 */
static char * get_cache_dir(void)
{
     char path[MAX_FILE_PATH_LEN];
     const char * home;
     const char ** sub_dirs = NULL;

     /* Locations are ...
      * $MOZPLUGGER_HOME/.cache/
      * $XDG_CACHE_HOME/mozplugger/
      * $HOME/.cache/mozplugger/
      */

     if( (home = getenv("MOZPLUGGER_HOME")) != NULL)
     {
          static const char * dirs[] = {".cache", 0};
          sub_dirs = dirs;
     }
     else if( (home = getenv("XDG_CACHE_HOME")) != NULL)
     {
          static const char * dirs[] = {"mozplugger", 0};
          sub_dirs = dirs;
     }
     else if( (home = get_home_dir()) != NULL)
     {
          static const char * dirs[] = {".cache", "mozplugger", 0};
          sub_dirs = dirs;
     }
     else
     {
          ERROR("Neither MOZPLUGGER_HOME or HOME are set!\n");
     }
 
     strncpy(path, home, sizeof(path));
    
     do
     {
          int lenSoFar;
          if((mkdir(path, 0777) != 0) && (errno != EEXIST))
          {
               perror("Failed to create directory");
               exit(EXIT_FAILURE);
          }
          if( *sub_dirs == NULL)
          {
               break;
          }

          lenSoFar = strlen(path);
          snprintf(&path[lenSoFar], sizeof(path) - lenSoFar, "/%s", *sub_dirs);
          sub_dirs++;
     }
     while(1);

     return strdup(path);
}


#define BUF_LEN (1024)

/**
 * Return the length of the longest string
 *
 * @param[in] str1 String 1
 * @param[in] str2 String 2
 *
 * @return The max length
 */
static size_t max_string_len(const char * str1, const char * str2)
{
    const size_t len1 = strlen(str1);
    const size_t len2 = strlen(str2);
    return (len1 > len2) ? len1 : len2;
}

#ifdef READ_FROM_INT_BLOB
extern unsigned char _binary_mozplugger_so_start[];
extern unsigned char _binary_mozplugger_so_end[];
unsigned char * blob_pos;
#endif


void * open_mozplugger_so(const char * path)
{
#ifdef READ_FROM_INT_BLOB
    LOG_INFO("- using internal blob as source\n");
    LOG_DEBUG("open_mozplugger_so %p\n", _binary_mozplugger_so_start);
    blob_pos = _binary_mozplugger_so_start;
    return (void *) &blob_pos;
#else
    LOG_INFO("- using '%s' as source\n", path);
    return (void *) fopen(path, "rb");
#endif
}

void close_mozplugger_so(void * handle)
{
#ifndef READ_FROM_INT_BLOB
    fclose((FILE *)handle);
#endif
}


static size_t read_mozplugger_so(char * buffer, size_t bufLen, void * in)
{
#ifdef READ_FROM_INT_BLOB
    unsigned char ** ppBlob = (unsigned char **)in;
    size_t len = _binary_mozplugger_so_end - *ppBlob;
    if(len > 0)
    {
/*        NOTICE("read_mozplugger_so %p %u\n", *ppBlob, len); */
        if(len > bufLen)
            len = bufLen;
        memcpy(buffer, *ppBlob, len);
        *ppBlob += len;
    }
    return len;
#else
    FILE * inFp = (FILE *)in;
    return fread(buffer, 1, bufLen, inFp);
#endif
}

/**
 * Copy mozplugger.so from in to out and add in the magic value
 *
 * @param[in] in filename of source
 * @param[in] out filename of destination
 * @param[in] magic The magic string
 */
static int copy(const char * in, const char * out, const char * magic)
{
    char buffer[BUF_LEN];
    void * inFp;
    FILE * outFp;
    bool found = false;
    size_t extra;
    size_t len;

    LOG_INFO("Creating '%s' for '%s'\n", out, magic);

    if( !(inFp = open_mozplugger_so(in)))
    {
        NOTICE("Failed to open '%s'\n", in);
        return 0;
    }
    if( !(outFp = fopen(out, "wb")))
    {
        close_mozplugger_so(inFp);
        ERROR("Failed to open '%s'\n", out);
    }

    extra = max_string_len(PLACE_HOLDER_STR, magic)+1;
    len = read_mozplugger_so(buffer, BUF_LEN, inFp);

    while(1)
    {
        char * p;
        for(p = buffer; p < &buffer[BUF_LEN-extra]; p++)
        {
            if(strcmp(p, PLACE_HOLDER_STR) == 0)
            {
                strcpy(p, magic);
                found = true;
                break;
            }
        }

        if(found || (len < BUF_LEN))
        {
            break;
        }

        len -= fwrite(buffer, 1, BUF_LEN-extra, outFp);
        memcpy(buffer, &buffer[BUF_LEN-extra], len);
        len += read_mozplugger_so(&buffer[len], BUF_LEN - len, inFp);
    }

     fwrite(buffer, 1, len, outFp);
     while((len = read_mozplugger_so(buffer, 1024, inFp)) > 0)
     {
          fwrite(buffer, 1, len, outFp);
     }
     close_mozplugger_so(inFp);
     fclose(outFp);
     return 1;
}

/**
 * Install the plugin
 *
 * @param[in] localPluginDir The directory to place the plugin
 * @param[in] cfgIdx The config idx number
 * @param[in] pluginIdx The plugin idx number
 *
 */
static void install(const char * localPluginDir, int cfgIdx, int pluginIdx)
{
     char localPluginPath[MAX_FILE_PATH_LEN];
     char tmpPluginPath[MAX_FILE_PATH_LEN];
     char globalPluginPath[MAX_FILE_PATH_LEN];
     char magic[MAX_PLUGIN_MAGIC_LEN];
     char * p = GLOBAL_PLUGIN_DIRS "/usr/lib/mozilla/plugins /usr/lib/netscape/plugins "
                                   "/usr/lib/firefox/plugins /usr/lib/chromium/plugins "
                                   "/usr/local/lib/browser_plugins/mozplugger";

     snprintf(tmpPluginPath, MAX_FILE_PATH_LEN, "%s/mozplugger.tmp", localPluginDir);
     snprintf(localPluginPath, MAX_FILE_PATH_LEN, "%s/mozplugger%i.so", localPluginDir, pluginIdx);
     snprintf(magic, MAX_PLUGIN_MAGIC_LEN, "%i:", cfgIdx);

     unlink(tmpPluginPath);
     rename(localPluginPath, tmpPluginPath);

     do
     {
          char * q;
          p = skip_spaces(p);
          for(q = p; (*q != ' ') && (*q != '\t') && (*q != '\0'); q++)
               ;
          if(q == p)
          {
               ERROR("Failed to find 'mozplugger.so'\n");
          }
          snprintf(globalPluginPath, sizeof(globalPluginPath), "%.*s/mozplugger.so", q-p, p);
          p = q;
     }
     while(!copy(globalPluginPath, localPluginPath, magic));
}

/*
 * Write a list of the locations of the helpers to a config file
 *
 * @param[in] fp The File to write to
 */
static void write_helpers(FILE * fp)
{
     static const char * helpers[] =
     {
          "mozplugger-linker",
          "mozplugger-helper",
          "mozplugger-controller"
     };

     /* Places to search for the mozplugger helper executables */
     static const cfgPath_t pluginBinPaths[] =
     {
          {"%s/bin/%s",              "MOZPLUGGER_HOME"},
          {"%s/.mozplugger/bin/%s",  "HOME"},
#ifdef EXEDIR
          {EXEDIR "/%s",             NULL},
#endif
          {"%s/%s",                  "PATH"},
          {NULL,                     NULL}
     };

     int i;
     for(i = 0; i < sizeof(helpers)/sizeof(const char *); i++)
     {
          char * exePath = find_helper_file(pluginBinPaths, helpers[i]);
          fprintf(fp, "%s\t%s\n", &strchr(helpers[i], '-')[1], exePath);
          free(exePath);
     }
}

/**
 * Remove any obsolete plugins in local plugin directory
 *
 * @param[in] localPluginDir Path to local plugin directory
 * @param[in] idx The first obsolete plugin to delete
 */
static void removeRest(const char * localPluginDir, int idx)
{
     char pluginPath[MAX_FILE_PATH_LEN];
     int startIdx = idx;

     for(;;idx++)
     {
          struct stat buf;

          snprintf(pluginPath, MAX_FILE_PATH_LEN, "%s/mozplugger%i.so", localPluginDir, idx);
          if(stat(pluginPath, &buf) != 0)
          {
               break;
          }
     }
     idx--;
     for(;idx >= startIdx; idx--)
     {
          snprintf(pluginPath, MAX_FILE_PATH_LEN, "%s/mozplugger%i.so", localPluginDir, idx);
          unlink(pluginPath);
     }
}

/**
 * Write the plugin data to some post cached config files so that mozplugger
 * can be quick.
 *
 * @param[in] localPluginDir The local(user) directory where plugins live.
 * @param[in] plugin The plugin config tree.
 * @param[in] cfgIdx The config tree idx.
 * @param[in] pluginIdx The plugin idx.
 */
static void write_plugin(const char * config_fname, const char * localPluginDir, pluginType_t * plugin, int cfgIdx, int pluginIdx)
{
     FILE * fp1;
     FILE * fp2;
     handler_t * handler;
     char buffer[MAX_FILE_PATH_LEN];
     char * path = get_cache_dir();

     install(localPluginDir, cfgIdx, pluginIdx);

     LOG_INFO("Creating %s ['%s' @ '%s']\n", localPluginDir, plugin->name, plugin->version);

     snprintf(buffer, sizeof(buffer), "%s/%i.helpers", path, cfgIdx);
     if(!(fp1 = fopen(buffer, "wb")))
     {
          ERROR("Failed to open '%s'\n", buffer);
     }
     fprintf(fp1, "#%s\n", VERSION);
     fprintf(fp1, "# This is autogenerated from %s\n", config_fname);
     fprintf(fp1, "%s\t%s\n", "name", plugin->name);
     fprintf(fp1, "%s\t%s\n", "version", plugin->version);
     write_helpers(fp1);
     fclose(fp1);

     snprintf(buffer, sizeof(buffer), "%s/%i.mimetypes", path, cfgIdx);
     if(!(fp1 = fopen(buffer, "wb")))
     {
          ERROR("Failed to open '%s'\n", buffer);
     }
     fprintf(fp1, "#%s\n", VERSION);
     fprintf(fp1, "# This is autogenerated from %s\n", config_fname);

     snprintf(buffer, sizeof(buffer), "%s/%i.cmds", path, cfgIdx);
     if(!(fp2 = fopen(buffer, "wb")))
     {
          fclose(fp1);
          ERROR("Failed to open '%s'\n", buffer);
     }
     fprintf(fp2, "#%s\n", VERSION);
     fprintf(fp2, "# This is autogenerated from %s\n", config_fname);

     for(handler = plugin->handlers; handler; handler = handler->pNext)
     {
         write_handler(handler, fp1, fp2);
     }

     fclose(fp2);
     fclose(fp1);
     free(path);
}

/**
 * Write a timestamp file to indicate the last time mozplugger-update was
 * called.
 */
static void write_ts_file(void)
{
     char * path = get_cache_dir();

     if(path)
     {
          char ts_fname[MAX_FILE_PATH_LEN];
          FILE * fp;

          snprintf(ts_fname, sizeof(ts_fname)-1, "%s/.last_update", path);
          fp = fopen(ts_fname, "wb");
          fprintf(fp, "%lu\n", time(NULL));
          fclose(fp);

          free(path);
     }
}

/**
 * main, look for and read the mozpluggerrc and create a cached processed
 * versions of that file.
 *
 * @param[in] argc
 * @param[in] argv
 */
int main(int argc, char * argv[])
{
     /* Places to search for the mozplugger config file */
     static const cfgPath_t mozillaCfgPaths[] =
     {
          {"%s/%s",                    "MOZPLUGGER_HOME"},
          {"%s/mozplugger/%s",         "XDG_CONFIG_HOME"},
          {"%s/.config/mozplugger/%s", "HOME"},
          {"%s/.mozplugger/%s",        "HOME"},
          {"%s/.mozilla/%s",           "HOME"},
          {"%s/%s",                    "MOZILLA_HOME"},
#ifdef CONFDIR
          {CONFDIR "/%s",              NULL},
#endif
          {"/etc/%s",                  NULL},
          {"/usr/etc/%s",              NULL},
          {"/usr/local/mozilla/%s",    NULL},
          {NULL,                       NULL}
     };

     /* Places to search for the mozplugger config file */
     static const cfgPath_t netscapeCfgPaths[] =
     {
          {"%s/%s",                    "MOZPLUGGER_HOME"},
          {"%s/mozplugger/%s",         "XDG_CONFIG_HOME"},
          {"%s/.config/mozplugger/%s", "HOME"},
          {"%s/.mozplugger/%s",        "HOME"},
          {"%s/.netscape/%s",          "HOME"},
#ifdef CONFDIR
          {CONFDIR "/%s",              NULL},
#endif
          {"/etc/%s",                  NULL},
          {"/usr/etc/%s",              NULL},
          {"/usr/local/netscape/%s",   NULL},
          {NULL,                       NULL}
     };

     /* Places to search for the mozplugger config file */
     static const cfgPath_t operaCfgPaths[] =
     {
          {"%s/%s",                    "MOZPLUGGER_HOME"},
          {"%s/mozplugger/%s",         "XDG_CONFIG_HOME"},
          {"%s/.config/mozplugger/%s", "HOME"},
          {"%s/.mozplugger/%s",        "HOME"},
          {"%s/.opera/%s",             "HOME"},
          {"%s/%s",                    "OPERA_DIR"},
#ifdef CONFDIR
          {CONFDIR "/%s",              NULL},
#endif
          {"/etc/%s",                  NULL},
          {"/usr/etc/%s",              NULL},
          {NULL,                       NULL}
     };

     static const browser_t browsers[] =
     {
          {"%s/.mozilla/plugins", mozillaCfgPaths},
          {"%s/.netscape/plugins", netscapeCfgPaths},
          {"%s/.opera/plugins", operaCfgPaths},
     };

     const char * home = get_home_dir();
     int i;
     int cfgIndex = 0;

     /* first thing make sure cache directory exists */
     write_ts_file();

     if((argc > 1) && (strcmp(argv[1], "-v") == 0))
     {
          g_verbose = 1;
     }

     if(home == NULL)
     {
          ERROR("HOME not defined\n");
     }

//     g_verbose = 1;
     for(i = 0; i < sizeof(browsers)/sizeof(browser_t); i++)
     {
          pluginType_t * plugins;
          pluginType_t * plugin;
          char localPluginDir[200];
          int pluginIdx = 0;
          char * config_fname;

          snprintf(localPluginDir, sizeof(localPluginDir), browsers[i].localPluginDir, home);

          if( (mkdir(localPluginDir, S_IRWXU) != 0) && (errno != EEXIST))
          {
               continue;
          }

          if(!(config_fname = find_helper_file(browsers[i].cfgPaths, "mozpluggerrc")))
          {
               ERROR("Failed to locate mozpluggerrc\n");
          }

          LOG_INFO("Creating plugins for '%s'\n", localPluginDir);

          plugins = trim(do_read_config(config_fname));

          for(plugin = plugins; plugin ; plugin = plugin->pNext)
          {
               write_plugin(config_fname, localPluginDir, plugin, cfgIndex++, pluginIdx++);
          }

          free(config_fname);

          while(plugins)
          {
              pluginType_t * plugin = plugins;
              plugins = plugin->pNext;
              delete_plugin(plugin);
          }
          removeRest(localPluginDir, pluginIdx);
     }

     delete_cache();

     return EXIT_SUCCESS;
}

