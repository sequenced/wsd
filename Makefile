all: .dep wsd test check

include .dep

.PHONY:	clean test check

CFLAGS = -Wall -ggdb -I. -Iproto -L.

wsd:	wsd.o wschild.o http.o ws.o wstypes.o proto/chatterbox1.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test:	test/test1 test/test2 test/test3 test/test4

test/test1:	test/test1.o ws.o http.o wstypes.o proto/chatterbox1.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test/test2:	test/test2.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test/test3:	test/test3.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test/test4:	test/test4.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

check:
	test/test1
	test/test2
	test/test3
	test/test4

clean:
	rm -f *.o test/*.o .dep wsd test/test[0-9]

.dep:
	$(CC) -MM -I. -Iproto `find . -name \*.c` > .dep
