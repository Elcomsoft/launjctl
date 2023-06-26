
DESTDIR ?= /usr/local

all:
	$(CC) ${CFLAGS} -o launjctl launjctl.c

install:
	mkdir -p ${DESTDIR}/bin
	cp launjctl ${DESTDIR}/bin/

clean:
	rm -f launjctl
