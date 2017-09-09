AC_INIT([cmdfs], [0.6], [mike@hiko.co.nz])
AC_PROG_CC
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_HEADERS([config.h])
AM_PATH_PYTHON([2.7],,[:])
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != ":"])
AC_CONFIG_FILES([ Makefile src/Makefile test/Makefile])
AC_OUTPUT
