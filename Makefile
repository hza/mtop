PREFIX ?= /usr/local

ktop: ktop.c
	cc -O2 -Wall -Wextra -o ktop ktop.c -lncurses

install: ktop
	install -d $(PREFIX)/bin
	install -m 755 ktop $(PREFIX)/bin/ktop

uninstall:
	rm -f $(PREFIX)/bin/ktop

clean:
	rm -f ktop

.PHONY: install uninstall clean
