#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([libadspprc.so], [0.0.1])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST

HEADER_PATH=""
AC_ARG_WITH([kernel_header], [AS_HELP_STRING([--with-kernel_header=PATH], [Specify path to the header])], [HEADER_PATH="$withval"],[AC_MSG_ERROR([Header path is required])])
# Checking if the header path is valid
if test -d "$HEADER_PATH"; then
	AC_MSG_RESULT([Linux kernel header found])
else
	AC_MSG_ERROR([Linux kernel headers not found in $HEADER_PATH])
fi

# Checking for fastrpc.h file
AC_CHECK_HEADER([$HEADER_PATH/include/misc/fastrpc.h], [CFLAGS="${CFLAGS} -I${HEADER_PATH}"] ,[AC_MSG_ERROR([fastrpc.h is not present in kernel header])])

AM_INIT_AUTOMAKE(1.10 foreign)
LT_INIT(disable-static)

AS_CASE([$host],
  [*-linux-android], [
  compile_for_android=yes
  ], [
  compile_for_android=no
  ]
)

AM_CONDITIONAL([ANDROID_CC],
[test "$compile_for_android" = yes])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AM_PROG_CC_C_O

# Checks for libraries.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([
Makefile
src/Makefile])
AC_OUTPUT
