#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([mozplugger], [1.14.5])
#AC_INIT([mozplugger], [CVS-2012-Feb-26])
AC_CONFIG_SRCDIR([child.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_LN_S
AC_PROG_MAKE_SET
#AC_PROG_MKDIR_P


# Determine the linker flags for mozplugger.so
LDSHARED='gcc -shared'
PLATFORM="x`uname`"
XCFLAGS="-fPIC -Wall -O2"

if test "${PLATFORM}" = xIRIX; then
    XCFLAGS="-fPIC -O2"
    LDSHARED='ld -shared -n32'
fi
if test "${PLATFORM}" = xAIX; then
    XCFLAGS="-fPIC -O2"
    LDSHARED='ld -G -bexpall'
fi
if test "${PLATFORM}${CC}" = "xHP-UXgcc"; then
    XCFLAGS="-fPIC -O2"
    LDSHARED='ld -b'
fi
if test "${PLATFORM}${CC}" = "xHP-UXcc"; then
    XCFLAGS='+DAportable -Ae +z +02'
    LDSHARED='ld -b +e NP_GetValue +e NP_Initialize +e NP_Shutdown +e NP_GetMIMEDescription' 
fi
if test "${PLATFORM}${CC}" = "xSunOSgcc"; then
    XCFLAGS="-fPIC -O2"
    LDSHARED='ld -G'
fi
if test "${PLATFORM}${CC}" = "xSunOScc"; then
    XCFLAGS='-Kpic -O'
    LDSHARED='ln -G' 
fi
if test "${PLATFORM}" = "xOSF1"; then
    XCFLAGS='-shared' 
    LDSHARED='ld -expect_unresolved "*" -shared -msym -O3'
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

#/usr/lib/mozilla/plugins/
#/usr/lib/netscape/plugins
#/usr/lib/firefox/plugins
#$MOZILLA_HOME/plugins
#/usr/lib/chromium/plugins

PLUGINDIR=${MOZ_PLUGIN_PATH}
AC_CHECK_FILE([${PLUGINDIR}], [PLUGINDIRS="${PLUGINDIR} ${PLUGINDIRS}"])

PLUGINDIR=/usr/${PLUGINLIB}/mozilla/plugins
AC_CHECK_FILE([${PLUGINDIR}], [PLUGINDIRS="${PLUGINDIR} ${PLUGINDIRS}"])

PLUGINDIR=/usr/${PLUGINLIB}/firefox/plugins
AC_CHECK_FILE([${PLUGINDIR}], [PLUGINDIRS="${PLUGINDIR} ${PLUGINDIRS}"])

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
    AC_CHECK_LIB([gcov], [main])
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
AC_CHECK_HEADERS([fcntl.h inttypes.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T

# Check to see if -lsocket and -lnsl is required (solaris) 
AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([socket], [socket], [],
               [AC_CHECK_LIB([socket], [socket],
               [LIBS="-lsocket -lnsl $LIBS"], [], [-lnsl])])



# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_CHECK_FUNCS([alarm dup2 gethostname memset mkdir putenv rmdir select strcasecmp strchr strcspn strncasecmp strrchr strstr])

AC_CONFIG_FILES([Makefile])

AC_SUBST([XCFLAGS])
AC_SUBST([XCPPFLAGS])
AC_SUBST([LDSHARED])
AC_SUBST([PLUGINDIRS])

AC_DEFINE([XP_UNIX])
AC_DEFINE_UNQUOTED([VERSION], ["${PACKAGE_VERSION}"])

AC_OUTPUT
