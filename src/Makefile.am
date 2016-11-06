bin_PROGRAMS = wsd
wsd_SOURCES = wsd.c wschild.c wstypes.c http.c parser.c ws.c
wsd_CFLAGS = $(AM_CFLAGS)
wsd_LDFLAGS = -ldl
noinst_LIBRARIES = libtestcommon.a libwstypes.a
libwstypes_a_SOURCES = wstypes.c
libtestcommon_a_SOURCES = wstypes.c parser.c
