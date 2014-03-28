include depend

.PHONY:	clean

SSQLOBJS = ssql.o tuplestruct.o
CFLAGS = -Wall -ggdb -I. -L.

all: depend rd acmd mpaccmd pr ssql rwr rrd rcat apiwr apird jni_shmem_api.h \
	wsd

wsd:	wsd.o wschild.o
	$(CC) $(CFLAGS) -lssysring -lrt $^ -o $@

apiwr:	apiwr.o shmem_api.o rcommon.o ringtest.o
	$(CC) $(CFLAGS) -lssysring -lrt $^ -o $@

apird:	apird.o shmem_api.o rcommon.o ringtest.o
	$(CC) $(CFLAGS) -lssysring -lrt $^ -o $@

rrd:	rrd.o rcommon.o ringtest.o
	$(CC) $(CFLAGS) -lssysring -lrt $^ -o $@

rwr:	rwr.o rcommon.o ringtest.o
	$(CC) $(CFLAGS) -lssysring -lrt $^ -o $@

rcat:	rcat.o rcommon.o ringtest.o
	$(CC) $(CFLAGS) -lssysring -lrt $^ -o $@

ssql:	$(SSQLOBJS)
	$(CC) $(CFLAGS) $^ -o $@

rd:	rd.o
	$(CC) $(CFLAGS) $^ -o $@

acmd:	acmd.o
	$(CC) $(CFLAGS) $^ -o $@

mpaccmd:	mpaccmd.o
	$(CC) $(CFLAGS) $^ -o $@

pr:	pr.o
	$(CC) $(CFLAGS) $^ -o $@

NUMREADER = 50
check:	rwr rrd apiwr apird
	rm -f /dev/shm/ringtest
	(./rwr -f ringtest -n $(NUMREADER); echo $$? > rv.w ) & touch rv.w
	for i in `seq $(NUMREADER)`; do \
		(./rrd -f ringtest ; echo $$? > rv.$$i ) & touch rv.$$i ; \
	done
	for i in `seq $(NUMREADER)`; do \
		while [ ! -s rv.$$i ]; do sleep 1; done ; \
		if [ `cat rv.$$i` -ne 0 ]; then exit 1; fi ; \
	done
	for i in `seq $(NUMREADER)`; do  rm rv.$$i ; done
	rm rv.w
	@echo "*** $(NUMREADER) ring read/write test(s) passed ***"

	rm -f /dev/shm/apitest
	(./apiwr -f apitest -n $(NUMREADER); echo $$? > rv.w ) & touch rv.w
	for i in `seq $(NUMREADER)`; do \
		(./apird -f apitest ; echo $$? > rv.$$i ) & touch rv.$$i ; \
	done
	for i in `seq $(NUMREADER)`; do \
		while [ ! -s rv.$$i ]; do sleep 1; done ; \
		if [ `cat rv.$$i` -ne 0 ]; then exit 1; fi ; \
	done
	for i in `seq $(NUMREADER)`; do rm rv.$$i ; done
	rm rv.w
	@echo "*** $(NUMREADER) api read/write test passed ***"

clean:
	rm -f *.o depend pr mpaccmd acmd rd ssql rwr rrd rcat apiwr apird

depend:
	$(CC) -MM -I. `find . -name \*.c` > depend
