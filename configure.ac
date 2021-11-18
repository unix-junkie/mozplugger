#
# This file is part of mozplugger a fork of plugger, for list of developers
# see the README file.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
#

# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([mozplugger], [2.1.6])
#AC_INIT([mozplugger], [CVS-2013-Feb-18])
AC_CONFIG_SRCDIR([child.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Determine the linker flags for mozplugger.so
LDSHARED='-shared -Wl,--version-script=exportmap'
PLATFORM="x`uname`"
XCFLAGS="-fPIC -Wall -O2 -Wdeclaration-after-statement"
BIN2O="ld -r -b binary"
MOZPLUGGER_SO_BLOB="mozplugger_so_blob.o"

if test "${PLATFORM}" = xIRIX; then
    XCFLAGS="-fPIC -O2"
    LDSHARED='-shared -n32'
    MOZPLUGGER_SO_BLOB=""
fi
if test "${PLATFORM}" = xAIX; then
    XCFLAGS="-fPIC -O2"
    LDSHARED='-G -bexpall'
    MOZPLUGGER_SO_BLOB=""
fi
if test "${PLATFORM}${CC}" = "xHP-UXgcc"; then
    XCFLAGS="-fPIC -O2"
    LDSHARED='-b'
    MOZPLUGGER_SO_BLOB=""
fi
if test "${PLATFORM}${CC}" = "xHP-UXcc"; then
    XCFLAGS='+DAportable -Ae +z +02'
    LDSHARED='-b +e NP_GetValue +e NP_Initialize +e NP_Shutdown +e NP_GetMIMEDescription'
    MOZPLUGGER_SO_BLOB=""
fi
if test "${PLATFORM}${CC}" = "xSunOSgcc"; then
    XCFLAGS="-fPIC -O2"
    LDSHARED='-G'
    MOZPLUGGER_SO_BLOB=""
fi
if test "${PLATFORM}${CC}" = "xSunOScc"; then
    XCFLAGS='-Kpic -O'
    LDSHARED='-G'
    MOZPLUGGER_SO_BLOB=""
fi
if test "${PLATFORM}" = "xOSF1"; then
    XCFLAGS='-shared'
    LDSHARED='-expect_unresolved "*" -shared -msym -O3'
    MOZPLUGGER_SO_BLOB=""
fi
#freebsd-aout:
#	${MAKE} all XCFLAGS='-fPIC -aout' XLDFLAGS='-shared -aout'


XCPPFLAGS=

# Checks for X path
AC_PATH_X
if test "x$x_includes" != x; then
    XCPPFLAGS="$XCPPFLAGS -I$x_includes"
fi
if test "x$x_libraries" != x; then
    LDFLAGS="$LDFLAGS -L$x_libraries"
fi

# Work out where plugins should be installed
PLUGINDIRS=
if test "$libdir" = '${exec_prefix}/lib64'; then
    PLUGINLIB=lib64
else
    PLUGINLIB=lib
fi

AC_ARG_VAR(PLUGINDIR,AS_HELP_STRING([The path where the plugin is to be installed]))
AC_CHECK_FILE([${PLUGINDIR}], [PLUGINDIRS="${PLUGINDIRS} ${PLUGINDIR}"])

#/usr/lib/mozilla/plugins/
#/usr/lib/netscape/plugins
#/usr/lib/firefox/plugins
#$MOZILLA_HOME/plugins
#/usr/lib/chromium/plugins
#/usr/local/lib/browser_plugins

PLUGINDIR=${MOZ_PLUGIN_PATH}
AC_CHECK_FILE([${PLUGINDIR}], [PLUGINDIRS="${PLUGINDIRS} ${PLUGINDIR}"])

PLUGINDIR=/usr/${PLUGINLIB}/mozilla/plugins
AC_CHECK_FILE([${PLUGINDIR}], [PLUGINDIRS="${PLUGINDIRS} ${PLUGINDIR}"])

PLUGINDIR=/usr/${PLUGINLIB}/firefox/plugins
AC_CHECK_FILE([${PLUGINDIR}], [PLUGINDIRS="${PLUGINDIRS} ${PLUGINDIR}"])

PLUGINDIR=/usr/local/lib/browser_plugins
AC_CHECK_FILE([${PLUGINDIR}], [PLUGINDIRS="${PLUGINDIRS} ${PLUGINDIR}"])

# Checks for libraries.
AC_CHECK_LIB([X11], [XDisplayName])


# Check to see if gcov required...
AC_ARG_ENABLE(gcov,
              AS_HELP_STRING([--enable-gcov],
                             [enable test coverage with gcov @<:@default=no@:>@]),
              [case "${enableval}" in
                 yes) gcov=true ;;
                 no)  gcov=false ;;
                 *)   AC_MSG_ERROR([bad value ${enableval} for --enable-gcov]) ;;
               esac],
              [gcov=false])
if test "x$gcov" = xtrue; then
    AC_CHECK_LIB([gcov], [main],[LDFLAGS="${LDFLAGS} -lgcov"])
    XCFLAGS="-g -fprofile-arcs -ftest-coverage -fstack-protector-all -Wall"
    AC_DEFINE([GCOV])
fi

#Check to see if debug required...
AC_ARG_ENABLE(debug,
              AS_HELP_STRING([--enable-debug],
                             [enable debug output @<:@default=no@:>@]),
              [case "${enableval}" in
                 yes) debug=true ;;
                 no)  debug=false ;;
                 *)   AC_MSG_ERROR([Bad value ${enableval} for --enable-debug]) ;;
               esac],
              [debug=false])
if test "x$debug" = xtrue; then
    AC_DEFINE([DEBUG])
else
    AC_DEFINE([NODEBUG])
fi

#Check to see if Always Xembed...
AC_ARG_ENABLE(always-xembed,
              AS_HELP_STRING([--enable-always-xembed],
                             [enable build with xembed always enabled @<:@default=no@:>@]),
              [case "${enableval}" in
                 yes) xembed=true ;;
                 no)  xembed=false ;;
                 *)   AC_MSG_ERROR([Bad value ${enableval} for --enable-always-xembed]) ;;
               esac],
              [xembed=false])
if test "x${xembed}" = xtrue; then
    AC_DEFINE([ALWAYS_NEEDS_XEMBED])
fi


# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h utime.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Check to see if -lsocket and -lnsl is required (solaris)
AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([socket], [socket], [],
               [AC_CHECK_LIB([socket], [socket],
               [LIBS="-lsocket -lnsl $LIBS"], [], [-lnsl])])



# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([alarm dup2 gethostname memmove memset mkdir putenv rmdir select strcasecmp strchr strcspn strncasecmp strrchr strstr strdup strtol getpwuid])

AC_CONFIG_FILES([Makefile])

AC_SUBST([XCFLAGS])
AC_SUBST([XCPPFLAGS])
AC_SUBST([LDSHARED])
AC_SUBST([PLUGINDIRS])
AC_SUBST([BIN2O])
AC_SUBST([MOZPLUGGER_SO_BLOB])

AC_DEFINE_UNQUOTED([VERSION], ["${PACKAGE_VERSION}"])
AC_DEFINE_UNQUOTED([GLOBAL_PLUGIN_DIRS], ["${PLUGINDIRS}"])
if test "x${MOZPLUGGER_SO_BLOB}" != x; then
    AC_DEFINE([READ_FROM_INT_BLOB])
fi
AC_OUTPUT
