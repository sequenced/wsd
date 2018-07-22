bin_PROGRAMS = wsd wscat
wsd_SOURCES = wsd.c wschild.c wschild.h pp2.c pp2.h ws.c ws.h ws_wsd.c \
	ws_wsd.h http.c http.h parser.c parser.h common.c common.h  types.h \
	list.h hashtable.h
wsd_LDFLAGS = -ldl
wscat_SOURCES = wscat.c common.c parser.c ws.c uri.c uri.h
# Binaries to aid unit testing
noinst_LIBRARIES = libtestcommon.a liburi.a
libtestcommon_a_SOURCES = wschild.c pp2.c http.c wscat.c
liburi_a_SOURCES = uri.c uri.h
