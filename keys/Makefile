CFLAGS=-I../micro-ecc -ggdb -Wall -I../ed25519/src
LDFLAGS=-ggdb ../ed25519/src/libed25519.a

genkey: genkey.o
	$(CC) -o $@ $^ $(LDFLAGS)

uECC.o: ../micro-ecc/uECC.c ../micro-ecc/uECC.h
	$(CC) $(CFLAGS) -c -o $@ $<


.PHONY: clean
clean:
	rm -f genkey.o uECC.o genkey
