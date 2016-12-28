.SUFFIXES: .xml .md .html .pdf

VERSION		 = 0.1.0
PREFIX		?= /usr/local
CFLAGS 		+= -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings
OBJS		 = autolink.o \
		   buffer.o \
		   document.o \
		   escape.o \
		   html.o \
		   html_blocks.o \
		   html_smartypants.o \
		   main.o \
		   nroff.o \
		   nroff_smartypants.o \
		   stack.o \
		   xmalloc.o
BINDIR 		 = $(PREFIX)/bin
MANDIR 		 = $(PREFIX)/man
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/lowdown
HTMLS		 = index.html lowdown.1.html README.html
PDFS		 = index.pdf README.pdf
MDS		 = index.md README.md
CSSS		 = template.css mandoc.css

all: lowdown

www: $(HTMLS) $(PDFS) lowdown.tar.gz lowdown.tar.gz.sha512

installwww: www
	mkdir -p $(WWWDIR)/snapshots
	install -m 0444 $(HTMLS) $(CSSS) $(PDFS) $(WWWDIR)
	install -m 0444 lowdown.tar.gz $(WWWDIR)/snapshots/lowdown-$(VERSION).tar.gz
	install -m 0444 lowdown.tar.gz.sha512 $(WWWDIR)/snapshots/lowdown-$(VERSION).tar.gz.sha512
	install -m 0444 lowdown.tar.gz $(WWWDIR)/snapshots
	install -m 0444 lowdown.tar.gz.sha512 $(WWWDIR)/snapshots

lowdown: $(OBJS)
	$(CC) -o $@ $(OBJS)

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	install -m 0755 lowdown $(DESTDIR)$(BINDIR)
	install -m 0444 lowdown.1 $(DESTDIR)$(MANDIR)/man1

index.xml README.xml index.pdf README.pdf: lowdown

index.html README.html: template.xml

.md.pdf:
	./lowdown -s -t lowdown -Tms $< | groff -Tpdf -ms > $@

.xml.html:
	sblg -t template.xml -o $@ -C $< $< versions.xml

$(HTMLS): versions.xml

.md.xml:
	( echo "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" ; \
	  echo "<article data-sblg-article=\"1\">" ; \
	  ./lowdown $< ; \
	  echo "</article>" ; ) >$@

lowdown.1.html: lowdown.1
	mandoc -Thtml -Ostyle=mandoc.css lowdown.1 >$@

lowdown.tar.gz.sha512: lowdown.tar.gz
	sha512 lowdown.tar.gz >$@

lowdown.tar.gz:
	mkdir -p .dist/lowdown-$(VERSION)/
	install -m 0644 *.c *.h Makefile *.1 .dist/lowdown-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

$(OBJS): extern.h

clean:
	rm -f $(OBJS) $(PDFS) $(HTMLS) 
	rm -f lowdown index.xml README.xml lowdown.tar.gz.sha512 lowdown.tar.gz
