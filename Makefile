TARGET		:= busexmp loopback vsfat bs_print
LIBOBJS 	:= buse.o utils.o setup.o address.o fatfiles.o
OBJS		:= $(TARGET:=.o) $(LIBOBJS)
STATIC_LIB	:= libbuse.a

CC		:= /usr/bin/gcc
CFLAGS		:= -g -pedantic -Wall -Wextra -std=gnu99
LDFLAGS		:= -L. -lbuse

.PHONY: all clean
all: CFLAGS += -O3
all: $(TARGET)

debug: CFLAGS += -Og
debug: $(TARGET)

$(TARGET): %: %.o $(STATIC_LIB) setup.h
	$(CC) -o $@ $< $(LDFLAGS)

$(TARGET:=.o): %.o: %.c buse.h setup.h
	$(CC) $(CFLAGS) -o $@ -c $<

$(STATIC_LIB): $(LIBOBJS) setup.h
	ar rc $(STATIC_LIB) $(LIBOBJS)

$(LIBOBJS): %.o: %.c setup.h
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(TARGET) $(OBJS) $(STATIC_LIB)
