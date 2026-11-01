CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra $(shell pkg-config --cflags libdrm)
LDLIBS += $(shell pkg-config --libs libdrm)

mbp-t1-fallback: mbp-t1-fallback.c

clean:
	rm -f mbp-t1-fallback
.PHONY: clean
