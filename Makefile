all: .dep wsd

include .dep

.PHONY:	clean

CFLAGS = -Wall -ggdb -I. -L.

wsd:	wsd.o wschild.o http.o ws.o
	$(CC) $(CFLAGS) -lrt $^ -o $@

clean:
	rm -f *.o .dep wsd

.dep:
	$(CC) -MM -I. `find . -name \*.c` > .dep
