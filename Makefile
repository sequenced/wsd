all: .dep wsd test

include .dep

.PHONY:	clean test

CFLAGS = -Wall -ggdb -I. -L.

wsd:	wsd.o wschild.o http.o ws.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@

test:	test/test1

test/test1:	test/test1.o ws.o http.o wstypes.o
	$(CC) $(CFLAGS) -lrt -lssl $^ -o $@
	test/test1

clean:
	rm -f *.o test/*.o .dep wsd test/test*

.dep:
	$(CC) -MM -I. `find . -name \*.c` > .dep
