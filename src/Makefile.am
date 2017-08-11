bin_PROGRAMS = wsd wscat
wsd_SOURCES = wsd.c wschild.c pp2.c common.c http.c parser.c ws.c
wsd_CFLAGS = $(AM_CFLAGS)
wsd_LDFLAGS = -ldl
wscat_SOURCES = wscat.c common.c parser.c ws.c
wscat_CFLAGS = $(AM_CFLAGS)
noinst_LIBRARIES = libtestcommon.a
libtestcommon_a_SOURCES = wschild.c pp2.c http.c
