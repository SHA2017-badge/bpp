OBJS:=bppsource.o
CFLAGS:=-ggdb

libbppsource.a: $(OBJS)
	ar rcs $@ $^

.PHONY: clean
clean:
	rm libbppsource.a $(OBJS)