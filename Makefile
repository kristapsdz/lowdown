.SUFFIXES: .xml .md .html .pdf .1 .1.html .3 .3.html .5 .5.html

include Makefile.configure

VERSION		 = 0.4.0
OBJS		 = autolink.o \
		   buffer.o \
		   diff.o \
		   document.o \
		   html.o \
		   html_escape.o \
		   html_smartypants.o \
		   library.o \
		   libdiff.o \
		   log.o \
		   nroff.o \
		   nroff_escape.o \
		   nroff_smartypants.o \
		   tree.o \
		   util.o \
		   xmalloc.o
COMPAT_OBJS	 = compats.o
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/lowdown
HTMLS		 = archive.html \
		   diff.html \
		   diff.diff.html \
		   index.html \
		   README.html \
		   $(MANS)
MANS		 = man/lowdown.1.html \
		   man/lowdown.3.html \
		   man/lowdown.5.html \
		   man/lowdown_buf.3.html \
		   man/lowdown_buf_diff.3.html \
		   man/lowdown_doc_free.3.html \
		   man/lowdown_doc_new.3.html \
		   man/lowdown_doc_parse.3.html \
		   man/lowdown_errstr.3.html \
		   man/lowdown_file.3.html \
		   man/lowdown_file_diff.3.html \
		   man/lowdown_html_free.3.html \
		   man/lowdown_html_new.3.html \
		   man/lowdown_html_rndr.3.html \
		   man/lowdown_nroff_free.3.html \
		   man/lowdown_nroff_new.3.html \
		   man/lowdown_nroff_rndr.3.html \
		   man/lowdown_tree_free.3.html \
		   man/lowdown_tree_new.3.html \
		   man/lowdown_tree_rndr.3.html
PDFS		 = diff.pdf index.pdf README.pdf
MDS		 = index.md README.md
CSSS		 = diff.css template.css mandoc.css
JSS		 = diff.js

all: lowdown lowdown-diff

www: $(HTMLS) $(PDFS) lowdown.tar.gz lowdown.tar.gz.sha512

installwww: www
	mkdir -p $(WWWDIR)/snapshots
	install -m 0444 $(MDS) $(HTMLS) $(CSSS) $(JSS) $(PDFS) $(WWWDIR)
	install -m 0444 lowdown.tar.gz $(WWWDIR)/snapshots/lowdown-$(VERSION).tar.gz
	install -m 0444 lowdown.tar.gz.sha512 $(WWWDIR)/snapshots/lowdown-$(VERSION).tar.gz.sha512
	install -m 0444 lowdown.tar.gz $(WWWDIR)/snapshots
	install -m 0444 lowdown.tar.gz.sha512 $(WWWDIR)/snapshots

lowdown: liblowdown.a main.o
	$(CC) -o $@ main.o liblowdown.a $(LDFLAGS) -lm

lowdown-diff: lowdown
	ln -f lowdown lowdown-diff

liblowdown.a: $(OBJS) $(COMPAT_OBJS)
	$(AR) rs $@ $(OBJS) $(COMPAT_OBJS)

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	install -m 0755 lowdown $(DESTDIR)$(BINDIR)
	install -m 0755 lowdown-diff $(DESTDIR)$(BINDIR)
	install -m 0644 liblowdown.a $(DESTDIR)$(LIBDIR)
	install -m 0644 lowdown.h $(DESTDIR)$(INCLUDEDIR)
	for f in $(MANS) ; do \
		name=`basename $$f .html` ; \
		section=$${name##*.} ; \
		install -m 0644 man/$$name $(DESTDIR)$(MANDIR)/man$$section ; \
	done

index.xml README.xml index.pdf diff.pdf README.pdf: lowdown

index.html README.html: template.xml

.md.pdf:
	./lowdown -Dnroff-numbered -s -Tms $< | \
		pdfroff -i -mspdf -t -k -Kutf8 > $@

.xml.html:
	sblg -t template.xml -s date -o $@ -C $< $< versions.xml

archive.html: archive.xml versions.xml
	sblg -t archive.xml -s date -o $@ versions.xml

diff.html: diff.md lowdown
	./lowdown -s diff.md >$@

diff.diff.html: diff.md diff.old.md lowdown-diff
	./lowdown-diff -s diff.old.md diff.md >$@

$(HTMLS): versions.xml

.md.xml: lowdown
	( echo "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" ; \
	  echo "<article data-sblg-article=\"1\">" ; \
	  ./lowdown $< ; \
	  echo "</article>" ; ) >$@

.1.1.html .3.3.html .5.5.html:
	mandoc -Thtml -Ostyle=mandoc.css $< >$@

lowdown.tar.gz.sha512: lowdown.tar.gz
	sha512 lowdown.tar.gz >$@

lowdown.tar.gz:
	mkdir -p .dist/lowdown-$(VERSION)/
	mkdir -p .dist/lowdown-$(VERSION)/man
	install -m 0644 *.c *.h Makefile LICENSE.md .dist/lowdown-$(VERSION)
	install -m 0644 man/*.1 man/*.3 man/*.5 .dist/lowdown-$(VERSION)/man
	install -m 0755 configure .dist/lowdown-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

$(OBJS) $(COMPAT_OBJS) main.o: config.h

$(OBJS): extern.h lowdown.h

main.o: lowdown.h

clean:
	rm -f $(OBJS) $(COMPAT_OBJS) $(PDFS) $(HTMLS) main.o
	rm -f index.xml diff.xml diff.diff.xml README.xml lowdown.tar.gz.sha512 lowdown.tar.gz
	rm -f lowdown lowdown-diff liblowdown.a 

distclean: clean
	rm -f Makefile.configure config.h config.log
