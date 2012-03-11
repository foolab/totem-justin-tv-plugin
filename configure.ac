AC_PREREQ([2.53])

AC_INIT([totem-plugin-jtv],[0.0.1])

AC_CONFIG_SRCDIR([totem-jtv.c])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.10])

AC_PROG_CC

GETTEXT_PACKAGE=totem-plugin-jtv
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, ["$GETTEXT_PACKAGE"], [Define to the Gettext package name])
AC_SUBST(GETTEXT_PACKAGE)
IT_PROG_INTLTOOL([0.40.0])

LT_PREREQ([2.2.6])
LT_INIT

AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

PKG_CHECK_MODULES(PLUGIN, [
totem libpeas-1.0 libsoup-2.4 libxml
], [
  AC_SUBST(PLUGIN_CFLAGS)
  AC_SUBST(PLUGIN_LIBS)
], [
  AC_MSG_ERROR([
      Missing dependencies.
  ])
])

AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.local/share/totem/plugins/jtv/"
else
  plugindir="\$(libdir)/totem/plugins/jtv/"
fi
AC_SUBST(plugindir)

PLUGIN_LDFLAGS='-module -avoid-version'
AC_SUBST(PLUGIN_LDFLAGS)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
