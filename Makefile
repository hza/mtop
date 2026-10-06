PREFIX ?= /usr/local

mtop: mtop.c
	cc -O2 -Wall -Wextra -o mtop mtop.c -lncurses

install: mtop
	install -d $(PREFIX)/bin
	install -m 755 mtop $(PREFIX)/bin/mtop

uninstall:
	rm -f $(PREFIX)/bin/mtop

clean:
	rm -f mtop

.PHONY: install uninstall clean
