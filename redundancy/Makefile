
OBJS:=redundancy.o

libredundancy.a: $(OBJS)
	ar cr $@ $^

.PHONY: clean
clean:
	-rm libredundancy.a $(OBJS)
