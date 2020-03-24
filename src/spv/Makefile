OBJECTS	:=$(wildcard bcash/*.c bitcoin/*.c support/*.c)
OBJECTS :=$(filter-out bitcoin/test.c support/testSup.c, $(OBJECTS))
OBJECTS	:=$(OBJECTS:.c=.o)

CFLAGS	:=-I. -I./support -I../secp256k1 -Wno-format-extra-args -g -std=gnu99
LIBSPV  :=libspv.a

test: lib
	gcc -o $@ bitcoin/test.c $(CFLAGS) -L./support -L./bitcoin -L./bcash $(LIBSPV) -lm -lbsd -lpthread

lib: $(OBJECTS)
	ar r $(LIBSPV) $(OBJECTS)

%.o: %.c
	gcc -c $(@:.o=.c) -o $@ $(CFLAGS)

clean:
	rm -f *.o */*.o *.d */*.d test $(LIBSPV)

run: test
	./test
