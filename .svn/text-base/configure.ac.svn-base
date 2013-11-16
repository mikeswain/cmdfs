AC_INIT([cmdfs], [0.5], [mike@hiko.co.nz])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_CONFIG_HEADERS([config.h])
AM_PATH_PYTHON([2.6],, [:])
AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != ":"])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
 src/test/Makefile
])
AC_OUTPUT
