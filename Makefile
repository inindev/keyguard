CC = clang
CFLAGS = -Wall -Wextra -O2
HID_FRAMEWORKS = -framework IOKit -framework CoreFoundation

all: snoop-key

snoop-key: snoop-key.c
	$(CC) $(CFLAGS) -o $@ $< $(HID_FRAMEWORKS)

clean:
	rm -f snoop-key

.PHONY: all clean
