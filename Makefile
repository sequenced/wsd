all: .dep wsd test check

include .dep

.PHONY:	clean test check

CFLAGS = -Wall -ggdb -I. -Iproto -L. -Lproto

libchatterbox.so.1.0:
	$(CC) $(CFLAGS) -fPIC -c proto/chatterbox.c
	$(CC) -shared -W1,-soname,libchatterbox.so.1 -o libchatterbox.so.1.0 \
		chatterbox.o
	ln -sf libchatterbox.so.1.0 libchatterbox.so.1
	ln -sf libchatterbox.so.1.0 libchatterbox.so

wsd:	wsd.o wschild.o http.o ws.o wstypes.o config_parser.o libchatterbox.so.1.0
	$(CC) $(CFLAGS) -lrt -lssl -lchatterbox $^ -o $@

test:	test/test1 test/test2 test/test3 test/test4 test/test5

test/test1:	test/test1.o ws.o http.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test/test2:	test/test2.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test/test3:	test/test3.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test/test4:	test/test4.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test/test5:	test/test5.o wstypes.o config_parser.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

check:
	test/test1
	test/test2
	test/test3
	test/test4
	test/test5

clean:
	rm -f *.o test/*.o .dep wsd test/test[0-9] proto/*.o libchatterbox.*

.dep:
	$(CC) -MM -I. -Iproto `find . -name \*.c` > .dep
