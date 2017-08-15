#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([wsd], [0.21], [bugs@sequencedsystems.com])
AC_CONFIG_SRCDIR([src/wsd.c])
AM_INIT_AUTOMAKE([-Wall -Werror])
AM_PROG_CC_C_O
AM_PROG_AR

LT_PREREQ([2.2])
LT_INIT([dlopen])

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
 test/Makefile
])

AC_CANONICAL_HOST

case "$host_os" in
     linux*)
	AC_DEFINE([SYS_LINUX], [1], [Host operating system])
	;;
esac

# Checks for programs.
AC_PROG_LN_S

# Checks for libraries.
AC_CHECK_LIB(crypto, BIO_f_base64,, AC_MSG_FAILURE(cannot find libcrypto))

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netinet/in.h stddef.h stdlib.h string.h sys/socket.h sys/time.h syslog.h unistd.h endian.h openssl/sha.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_UID_T

# Checks for library functions.
AC_FUNC_FORK
AC_CHECK_FUNCS([gettimeofday inet_ntop memset socket strncasecmp strstr malloc])

AC_OUTPUT
