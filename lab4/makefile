CC = gcc
CFLAGS = -W -Wall -Wextra -std=c11 -pedantic -g
LDFLAGS =

all: main producer consumer

main: main.c
	$(CC) $(CFLAGS) -o main main.c $(LDFLAGS)

producer: producer.c
	$(CC) $(CFLAGS) -o producer producer.c $(LDFLAGS)

consumer: consumer.c
	$(CC) $(CFLAGS) -o consumer consumer.c $(LDFLAGS)

clean:
	rm -f main producer consumer

.PHONY: all clean