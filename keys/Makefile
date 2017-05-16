CFLAGS=-I../micro-ecc -ggdb -Wall
LDFLAGS=-lmbedtls -lmbedcrypto -ggdb

genkey: genkey.o
	$(CC) -o $@ $^ $(LDFLAGS)

uECC.o: ../micro-ecc/uECC.c ../micro-ecc/uECC.h
	$(CC) $(CFLAGS) -c -o $@ $<


.PHONY: clean
clean:
	rm -f genkey.o uECC.o genkey