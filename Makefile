prefix?=/usr/local

lv2h_cflags:=-std=c99 -Wall -Wextra -pedantic -g -O0 -D_GNU_SOURCE -I. $(CFLAGS)
lv2h_ldflags:=$(LDFLAGS)
lv2h_ldlibs:=-lm -ldl -lpthread -lasound -lsoundio -llua5.3 -llilv-0 $(LDLIBS)
lv2h_objects:=$(patsubst %.c,%.o,$(wildcard *.c))
lv2h_static_var:=

all: lv2h

lv2h: $(lv2h_vendor_deps) $(lv2h_objects)
	$(CC) $(lv2h_cflags) $(lv2h_objects) $(lv2h_ldflags) $(lv2h_ldlibs) -o lv2h

$(lv2h_objects): %.o: %.c
	$(CC) -c $(lv2h_cflags) $< -o $@

install: lv2h
	install -D -v -m 755 lv2h $(DESTDIR)$(prefix)/bin/lv2h

clean:
	rm -f lv2h $(lv2h_objects)

.PHONY: all install clean
