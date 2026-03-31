CC = clang
CFLAGS = -Wall -Wextra -O2
FRAMEWORKS = -framework CoreGraphics -framework ApplicationServices
PREFIX = /usr/local

all: disable-key snoop-key

disable-key: disable-key.c
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORKS)

snoop-key: snoop-key.c
	$(CC) $(CFLAGS) -o $@ $< $(FRAMEWORKS)

install: disable-key
	install -m 755 disable-key $(PREFIX)/bin/disable-key

clean:
	rm -f disable-key snoop-key

.PHONY: install clean
