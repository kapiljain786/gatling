#DEBUG=1
ZLIB=1
prefix=/opt/diet
BINDIR=${prefix}/bin
MANDIR=${prefix}/man
man1dir=$(MANDIR)/man1

TARGETS=gatling httpbench bindbench mmapbench forkbench dl \
mktestdata manymapbench ioerr forksbench tlsgatling pthreadbench

all: $(TARGETS)

CC=gcc
CFLAGS=-pipe -Wall
LDFLAGS=

path = $(subst :, ,$(PATH))
diet_path = $(foreach dir,$(path),$(wildcard $(dir)/diet))
ifeq ($(strip $(diet_path)),)
ifneq ($(wildcard /opt/diet/bin/diet),)
DIET=/opt/diet/bin/diet
else
DIET=
endif
else
DIET:=$(strip $(diet_path))
endif

# to build without diet libc support, use $ make DIET=
# see http://www.fefe.de/dietlibc/ for details about the diet libc

ifneq ($(DEBUG),)
CFLAGS+=-g
LDFLAGS+=-g
else
CFLAGS+=-O2 -fomit-frame-pointer
LDFLAGS+=-s
ifneq ($(DIET),)
DIET+=-Os
endif
endif

LDLIBS=-lowfat

ifeq ($(ZLIB),1)
CFLAGS+=-DUSE_ZLIB
LDLIBS+=-lz
endif

libowfat_path = $(strip $(foreach dir,../libowfat*,$(wildcard $(dir)/textcode.h)))
ifneq ($(libowfat_path),)
CFLAGS+=$(foreach fnord,$(libowfat_path),-I$(dir $(fnord)))
LDFLAGS+=$(foreach fnord,$(libowfat_path),-L$(dir $(fnord)))
endif

CC:=$(DIET) $(CC)

pthreadbench: pthreadbench.o
	$(CC) $< -o $@ -I. $(CFLAGS) $(LDFLAGS) $(LDLIBS) -lpthread

forksbench: forkbench.o
	$(CC) -static -o $@ forkbench.o $(LDFLAGS) $(LDLIBS)

gatling.o: version.h

tlsgatling: gatling.c ssl.o
	-$(CC) -o $@ $^ $(CFLAGS) -DSUPPORT_HTTPS $(LDFLAGS) -lssl -lcrypto $(LDLIBS)

version.h: CHANGES
	(head -n 1 CHANGES | sed 's/\([^:]*\):/#define VERSION "\1"/') > version.h

%.o: %.c
	$(CC) -c $< -o $@ -I. $(CFLAGS)

libsocket: trysocket.c
	if $(DIET) $(CC) $(CFLAGS) -o trysocket trysocket.c >/dev/null 2>&1; then echo ""; else \
	if $(DIET) $(CC) $(CFLAGS) -o trysocket trysocket.c -lsocket >/dev/null 2>&1; then echo "-lsocket"; else \
	if $(DIET) $(CC) $(CFLAGS) -o trysocket trysocket.c -lsocket -lnsl >/dev/null 2>&1; then echo "-lsocket -lnsl"; \
	fi; fi; fi > libsocket
	rm -f trysocket

libiconv: tryiconv.c
	if $(DIET) $(CC) $(CFLAGS) -o tryiconv tryiconv.c >/dev/null 2>&1; then echo ""; else \
	if $(DIET) $(CC) $(CFLAGS) -o tryiconv tryiconv.c -liconv >/dev/null 2>&1; then echo "-liconv"; \
	fi; fi > libiconv
	rm -f tryiconv

dummy.c:
	touch $@

libsocketkludge.a: libsocket libiconv dummy.o
	ar q $@ dummy.o
	-ranlib $@

LDLIBS+=`cat libsocket libiconv`

$(TARGETS): libsocketkludge.a

install: gatling
	install -d $(BINDIR) $(man1dir)
	install $< $(BINDIR)
	test -f tlsgatling && install tlsgatling $(BINDIR)
	install -m 644 gatling.1 $(man1dir)

uninstall:
	rm -f $(BINDIR)/gatling $(BINDIR)/tlsgatling $(man1dir)/gatling.1

clean:
	rm -f $(TARGETS) *.o version.h core *.core libsocket libsocketkludge.a libiconv

VERSION=gatling-$(shell head -n 1 CHANGES|sed 's/://')
CURNAME=$(notdir $(shell pwd))

rename:
	if test $(CURNAME) != $(VERSION); then cd .. && mv $(CURNAME) $(VERSION); fi

tar: clean rename
	rm -f dep libdep
	cd ..; tar cvvf $(VERSION).tar.bz2 --use=bzip2 --exclude CVS $(VERSION)

cert: server.pem

rand.dat:
	-dd if=/dev/random of=rand.dat bs=1024 count=1

cakey.key: rand.dat
	openssl genrsa -out cakey.key -rand rand.dat 2048

cakey.csr: cakey.key
	openssl req -new -key cakey.key -out cakey.csr

cakey.pem: cakey.key cakey.csr
	openssl x509 -req -days 1780 -set_serial 1 -in cakey.csr \
	  -signkey cakey.key -out $@

server.pem: cakey.key cakey.pem
	cat cakey.key cakey.pem > server.pem
