CC = gcc
CFLAGS = -W -Wall -Wextra -std=c11 -pedantic -g
LDFLAGS =

all: main

main: main.c
	$(CC) $(CFLAGS) -o main main.c $(LDFLAGS)

clean:
	rm -f main

.PHONY: all clean