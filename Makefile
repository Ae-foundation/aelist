CFLAGS=-lncurses -ltinfo -lpanel -O3 -g -Wall

all:
	cc $(CFLAGS) aelist.c -o aelist
install: all
	cp aelist /usr/local/bin
uninstall:
	rm /usr/local/bin/aelist
clean:
	rm aelist
