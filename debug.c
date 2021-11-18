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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "debug.h"

#ifdef DEBUG

static int    debug_on = 1;
static FILE * debug_output = NULL;
static char * debug_path = "";

/**
 * Open the debug file for appending to
 *
 * @return The FILE pointer, or NULL if problem
 */
static FILE *getout(void)
{
     char debug_filename[128];
     char *tmpdir;

     if (debug_output)
     {
          return debug_output;
     }

     if(!debug_on)
     {
          return NULL;
     }

     tmpdir = getenv("MOZPLUGGER_TMP");
     if(tmpdir == NULL)
     {
        tmpdir = getenv("TMPDIR");
        debug_path = "$MOZPLUGGER_TMP";
        snprintf(debug_filename, sizeof(debug_filename),
                                               "%s/%s", tmpdir, DEBUG_FILENAME);
     }

     if(tmpdir == NULL)
     {
          /* If (as on default Fedora 7 install) the environment variable
           * TMPDIR is not defined, just assume $HOME/tmp */
          char * homedir = getenv("HOME");
          if(homedir == NULL)
          {
               fprintf(stderr, "Failed to open debug file - "
                               "neither HOME or TMPDIR defined\n");
               return NULL;
          }
          debug_path = "$HOME/tmp";        /* For display purposes */
          snprintf(debug_filename, sizeof(debug_filename),
                                          "%s/tmp/%s", homedir, DEBUG_FILENAME);
     }
     else
     {
          debug_path = "$TMDIR";          /* For display purposes */
          snprintf(debug_filename, sizeof(debug_filename),
                                               "%s/%s", tmpdir, DEBUG_FILENAME);
     }

     debug_output = fopen(debug_filename,"a+");
     if (debug_output == NULL)
     {
          fprintf(stderr,"Failed to open debug file \'%s'\n", debug_filename);
          debug_on = 0;
          return NULL;
     }
     else
     {
          fprintf(stderr,"Opened debug file \'%s\'\n", debug_filename);
     }

     fprintf(debug_output, "------------\n");
     return debug_output;
}

/**
 * Close debug file
 */
void close_debug(void)
{
     FILE * f = getout();

     if(f)
     {
          fclose(f);
     }
     debug_output = NULL;
}

/**
 * Take the standard printf type arguments and write the output to the debug
 * file
 *
 * @param[in] fmt The printf format args
 * @param[in] ... The printf style arguments
 */
void D(char *fmt, ...)
{
     char buffer[9999];
     va_list ap;
     FILE * f = getout();

     if(f)
     {
          va_start(ap,fmt);
          vsnprintf(buffer,sizeof(buffer),fmt,ap);
          va_end(ap);

          fprintf(f,"PID%4d: %s",(int) getpid(), buffer);
          fflush(f);
     }
}

/**
 * Get path to the debug file (for display purposes only!)
 *
 * @return String containing debug path name
 */
char * get_debug_path(void)
{
     if(debug_path[0] == '\0')
     {
          getout();
     }

     return debug_path;
}

int is_debugging(void)
{
     return 1;
}

#else

void D(char *fmt, ...)
{
}

void close_debug(void)
{
}

char * get_debug_path(void)
{
     return NULL;
};

int is_debugging(void)
{
     return 0;
}
#endif

