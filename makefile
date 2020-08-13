CFLAGS  = -g -std=gnu99 -Wall -Werror
CFLAGS += -I/opt/redpitaya/include
LDFLAGS = -L/opt/redpitaya/lib
LDLIBS = -lm -lpthread -lrp

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=)

all: server

server: main.o network_thread.c
	$(CC) $(LDFLAGS) $(LDLIBS) main.o network_thread.o -o server

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	$(RM) *.o
	$(RM) $(OBJS)
