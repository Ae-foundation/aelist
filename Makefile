CFLAGS=-lncurses -ltinfo -lpanel -O3 -g -Wall

all:
	cc $(CFLAGS) aelist.c -o aelist
clean:
	rm aelist.o aelist
