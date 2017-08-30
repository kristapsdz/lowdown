.SUFFIXES: .xml .md .html .pdf .1 .1.html .3 .3.html

include Makefile.configure

VERSION		 = 0.2.0
OBJS		 = autolink.o \
		   buffer.o \
		   document.o \
		   escape.o \
		   html.o \
		   html_smartypants.o \
		   library.o \
		   log.o \
		   nroff.o \
		   nroff_smartypants.o \
		   tree.o \
		   xmalloc.o
COMPAT_OBJS	 = compat_err.o \
		   compat_progname.o \
		   compat_reallocarray.o \
		   compat_strlcat.o \
		   compat_strlcpy.o \
		   compat_strtonum.o

WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/lowdown
HTMLS		 = archive.html index.html lowdown.1.html lowdown.3.html README.html
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

lowdown: liblowdown.a main.o
	$(CC) -o $@ main.o liblowdown.a

liblowdown.a: $(OBJS) $(COMPAT_OBJS)
	$(AR) rs $@ $(OBJS) $(COMPAT_OBJS)

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	install -m 0755 lowdown $(DESTDIR)$(BINDIR)
	install -m 0644 liblowdown.a $(DESTDIR)$(LIBDIR)
	install -m 0644 lowdown.h $(DESTDIR)$(INCLUDEDIR)
	install -m 0644 lowdown.1 $(DESTDIR)$(MANDIR)/man1
	install -m 0644 lowdown.3 $(DESTDIR)$(MANDIR)/man3

index.xml README.xml index.pdf README.pdf: lowdown

index.html README.html: template.xml

.md.pdf:
	./lowdown -Dnroff-numbered -s -Tms $< | \
		groff -Tpdf -dpaper=a4 -P-pa4 -ms -mpdfmark > $@

.xml.html:
	sblg -t template.xml -o $@ -C $< $< versions.xml

archive.html: archive.xml versions.xml
	sblg -t archive.xml -o $@ versions.xml

$(HTMLS): versions.xml

.md.xml:
	( echo "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" ; \
	  echo "<article data-sblg-article=\"1\">" ; \
	  ./lowdown $< ; \
	  echo "</article>" ; ) >$@

.1.1.html .3.3.html:
	mandoc -Thtml -Ostyle=mandoc.css $< >$@

lowdown.tar.gz.sha512: lowdown.tar.gz
	sha512 lowdown.tar.gz >$@

lowdown.tar.gz:
	mkdir -p .dist/lowdown-$(VERSION)/
	install -m 0644 *.c *.h Makefile *.1 *.3 .dist/lowdown-$(VERSION)
	install -m 0755 configure .dist/lowdown-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

$(OBJS) $(COMPAT_OBJS) main.o: config.h

$(OBJS): extern.h lowdown.h

main.o: lowdown.h

clean:
	rm -f $(OBJS) $(COMPAT_OBJS) $(PDFS) $(HTMLS) main.o
	rm -f lowdown liblowdown.a index.xml README.xml lowdown.tar.gz.sha512 lowdown.tar.gz

distclean: clean
	rm -f Makefile.configure config.h config.log
