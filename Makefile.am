EXTRA_DIST = autogen.sh jtv.plugin

plugin_LTLIBRARIES = libjtv.la
libjtv_la_SOURCES = totem-jtv.c

libjtv_la_CFLAGS = $(PLUGIN_CFLAGS)
libjtv_la_LIBADD = $(PLUGIN_LIBS)
libjtv_la_LDFLAGS = $(PLUGIN_LDFLAGS)
libjtv_la_LIBTOOLFLAGS = --tag=disable-static

plugin_DATA = jtv.plugin
