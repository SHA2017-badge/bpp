CFLAGS=-I../micro-ecc -ggdb

genkey: genkey.o uECC.o
	$(CC) -o $@ $^

uECC.o: ../micro-ecc/uECC.c ../micro-ecc/uECC.h
	$(CC) $(CFLAGS) -c -o $@ $<


.PHONY: clean
clean:
	rm -f genkey.o uECC.o genkey