CC     = gcc
CFLAGS = -O3 -march=native -Wall
LDLIBS = -lcrypto

all: send recv

send: redundancy.o send.o

recv: redundancy.o recv.o

clean:
	rm -f redundancy.o
	rm -f send.o send
	rm -f recv.o recv
