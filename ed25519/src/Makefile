
OBJS:=add_scalar.o fe.o ge.o key_exchange.o keypair.o sc.o seed.o sha512.o sign.o verify.o

libed25519.a: $(OBJS)
	ar cr $@ $^

.PHONY: clean
clean:
	-rm libed25519.a $(OBJS)
