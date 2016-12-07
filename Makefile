CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings
OBJS		 = autolink.o \
		   buffer.o \
		   document.o \
		   escape.o \
		   html.o \
		   html_blocks.o \
		   main.o \
		   stack.o
BINDIR 		 = $(PREFIX)/bin
MANDIR 		 = $(PREFIX)/man

all: lowdown

lowdown: $(OBJS)
	$(CC) -o $@ $(OBJS)

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 lowdown $(DESTDIR)$(BINDIR)
	install -m 0444 lowdown.1 $(DESTDIR)$(MANDIR)/man1

$(OBJS): extern.h

clean:
	rm -f $(OBJS) lowdown
