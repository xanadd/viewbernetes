CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
LDLIBS = -lncurses -lpthread -lm
PREFIX ?= $(HOME)/.local

all: viewbernetes
viewbernetes: src/viewbernetes.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)
test: viewbernetes
	python3 tests/test.py
install: viewbernetes
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 viewbernetes "$(DESTDIR)$(PREFIX)/bin/viewbernetes"
clean:
	rm -f viewbernetes tests/unit
.PHONY: all test install clean
