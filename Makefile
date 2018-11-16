TARGET		:= busexmp loopback vsfat
LIBOBJS 	:= buse.o utils.o setup.o address.o fatfiles.o
OBJS		:= $(TARGET:=.o) $(LIBOBJS)
STATIC_LIB	:= libbuse.a

CC		:= /usr/bin/gcc
CFLAGS		:= -g -pedantic -Wall -Wextra -std=gnu99 -Og
LDFLAGS		:= -L. -lbuse

.PHONY: all clean
all: $(TARGET)

$(TARGET): %: %.o $(STATIC_LIB)
	$(CC) -o $@ $< $(LDFLAGS)

$(TARGET:=.o): %.o: %.c buse.h
	$(CC) $(CFLAGS) -o $@ -c $<

$(STATIC_LIB): $(LIBOBJS)
	ar rc $(STATIC_LIB) $(LIBOBJS)

$(LIBOBJS): %.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(TARGET) $(OBJS) $(STATIC_LIB)
