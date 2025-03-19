CC = gcc
CFLAGS = -W -Wall -Wextra -std=c11 -pedantic -g
LDFLAGS =

all: parent child

parent: parent.c
	$(CC) $(CFLAGS) -o parent parent.c $(LDFLAGS)

child: child.c
	$(CC) $(CFLAGS) -o child child.c $(LDFLAGS)

clean:
	rm -f parent child

.PHONY: all clean