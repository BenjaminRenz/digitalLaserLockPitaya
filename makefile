CFLAGS  = -g -std=gnu99 -Wall -Werror -Wconversion
INC = -I/opt/redpitaya/include -I.
CFLAGS += $(INC)
LDFLAGS = -L/opt/redpitaya/lib
LDLIBS = -lrp -lm -pthread

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=)

all: server

server: main.o network_thread.o fft4g.o
	$(CC) $(LDFLAGS) main.o network_thread.o fft4g.o $(LDLIBS) -o server

network_thread.o: network_thread.c
	$(CC) -c $(CFLAGS) network_thread.c -o network_thread.o

fft4g.o: fft4g.c
	$(CC) -c $(INC) fft4g.c -o fft4g.o

main.o: main.c
	$(CC) -c $(CFLAGS) main.c -o main.o


clean:
	$(RM) *.o
	$(RM) $(OBJS)
