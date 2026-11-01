# mbp_speakerguard LADSPA plugin; 'make test' runs the offline model checks.
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -fPIC -std=c99 -D_DEFAULT_SOURCE
LADSPA_DIR ?= /usr/local/lib/ladspa

all: mbp_speakerguard.so

mbp_speakerguard.so: mbp_speakerguard.c guard.h basslimit.h vbass.h ladspa_min.h
	$(CC) $(CFLAGS) -shared \
		-o $@ mbp_speakerguard.c -lm

test_guard: test_guard.c guard.h
	$(CC) $(CFLAGS) -o $@ test_guard.c -lm

test_host: test_host.c mbp_speakerguard.so
	$(CC) $(CFLAGS) -o $@ test_host.c -ldl -lm

test: test_guard test_host
	./test_guard
	./test_host ./mbp_speakerguard.so

install: mbp_speakerguard.so
	install -Dm755 mbp_speakerguard.so $(DESTDIR)$(LADSPA_DIR)/mbp_speakerguard.so

clean:
	rm -f mbp_speakerguard.so test_guard test_host
