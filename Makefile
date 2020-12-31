.PHONY: regress
.SUFFIXES: .xml .md .html .pdf .1 .1.html .3 .3.html .5 .5.html .thumb.jpg .png .in.pc .pc

include Makefile.configure

VERSION		 = 0.7.5
OBJS		 = autolink.o \
		   buffer.o \
		   diff.o \
		   document.o \
		   entity.o \
		   gemini.o \
		   html.o \
		   html_escape.o \
		   latex.o \
		   library.o \
		   libdiff.o \
		   nroff.o \
		   smartypants.o \
		   term.o \
		   tree.o \
		   util.o \
		   xmalloc.o
COMPAT_OBJS	 = compats.o
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/lowdown
HTMLS		 = archive.html \
		   atom.xml \
		   diff.html \
		   diff.diff.html \
		   index.html \
		   README.html \
		   $(MANS)
MANS		 = man/lowdown.1.html \
		   man/lowdown-diff.1.html \
		   man/lowdown.3.html \
		   man/lowdown.5.html \
		   man/lowdown_buf.3.html \
		   man/lowdown_buf_diff.3.html \
		   man/lowdown_buf_free.3.html \
		   man/lowdown_buf_new.3.html \
		   man/lowdown_diff.3.html \
		   man/lowdown_doc_free.3.html \
		   man/lowdown_doc_new.3.html \
		   man/lowdown_doc_parse.3.html \
		   man/lowdown_file.3.html \
		   man/lowdown_file_diff.3.html \
		   man/lowdown_gemini_free.3.html \
		   man/lowdown_gemini_new.3.html \
		   man/lowdown_gemini_rndr.3.html \
		   man/lowdown_html_free.3.html \
		   man/lowdown_html_new.3.html \
		   man/lowdown_html_rndr.3.html \
		   man/lowdown_latex_free.3.html \
		   man/lowdown_latex_new.3.html \
		   man/lowdown_latex_rndr.3.html \
		   man/lowdown_metaq_free.3.html \
		   man/lowdown_nroff_free.3.html \
		   man/lowdown_nroff_new.3.html \
		   man/lowdown_nroff_rndr.3.html \
		   man/lowdown_term_free.3.html \
		   man/lowdown_term_new.3.html \
		   man/lowdown_term_rndr.3.html \
		   man/lowdown_tree_free.3.html \
		   man/lowdown_tree_new.3.html \
		   man/lowdown_tree_rndr.3.html
PDFS		 = diff.pdf \
		   index.pdf \
		   README.latex.pdf \
		   README.nroff.pdf
MDS		 = index.md README.md
CSSS		 = diff.css template.css
JSS		 = diff.js
IMAGES		 = screen-mandoc.png \
		   screen-groff.png \
		   screen-term.png
THUMBS		 = screen-mandoc.thumb.jpg \
		   screen-groff.thumb.jpg \
		   screen-term.thumb.jpg
REGRESS_ARGS	 = "--out-no-smarty"
REGRESS_ARGS	+= "--parse-no-img-ext"
REGRESS_ARGS	+= "--parse-no-metadata"
REGRESS_ARGS	+= "--html-no-head-ids"
REGRESS_ARGS	+= "--html-no-skiphtml"
REGRESS_ARGS	+= "--html-no-escapehtml"
REGRESS_ARGS	+= "--html-no-owasp"
REGRESS_ARGS	+= "--html-no-num-ent"
REGRESS_ARGS	+= "--parse-no-autolink"
REGRESS_ARGS	+= "--parse-no-cmark"
REGRESS_ARGS	+= "--parse-no-deflists"

all: lowdown lowdown-diff lowdown.pc

www: $(HTMLS) $(PDFS) $(THUMBS) lowdown.tar.gz lowdown.tar.gz.sha512

installwww: www
	mkdir -p $(WWWDIR)/snapshots
	$(INSTALL) -m 0444 $(THUMBS) $(IMAGES) $(MDS) $(HTMLS) $(CSSS) $(JSS) $(PDFS) $(WWWDIR)
	$(INSTALL) -m 0444 lowdown.tar.gz $(WWWDIR)/snapshots/lowdown-$(VERSION).tar.gz
	$(INSTALL) -m 0444 lowdown.tar.gz.sha512 $(WWWDIR)/snapshots/lowdown-$(VERSION).tar.gz.sha512
	$(INSTALL) -m 0444 lowdown.tar.gz $(WWWDIR)/snapshots
	$(INSTALL) -m 0444 lowdown.tar.gz.sha512 $(WWWDIR)/snapshots

lowdown: liblowdown.a main.o
	$(CC) -o $@ main.o liblowdown.a $(LDFLAGS) $(LDADD_MD5) -lm

lowdown-diff: lowdown
	ln -f lowdown lowdown-diff

liblowdown.a: $(OBJS) $(COMPAT_OBJS)
	$(AR) rs $@ $(OBJS) $(COMPAT_OBJS)

install: all
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(LIBDIR)/pkgconfig
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	$(INSTALL_DATA) lowdown.pc $(DESTDIR)$(LIBDIR)/pkgconfig
	$(INSTALL_PROGRAM) lowdown $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) lowdown-diff $(DESTDIR)$(BINDIR)
	$(INSTALL_LIB) liblowdown.a $(DESTDIR)$(LIBDIR)
	$(INSTALL_DATA) lowdown.h $(DESTDIR)$(INCLUDEDIR)
	for f in $(MANS) ; do \
		name=`basename $$f .html` ; \
		section=$${name##*.} ; \
		$(INSTALL_MAN) man/$$name $(DESTDIR)$(MANDIR)/man$$section ; \
	done

distcheck: lowdown.tar.gz.sha512
	mandoc -Tlint -Werror man/*.[135]
	newest=`grep "<h1>" versions.xml | tail -1 | sed 's![ 	]*!!g'` ; \
	       [ "$$newest" = "<h1>$(VERSION)</h1>" ] || \
		{ echo "Version $(VERSION) not newest in versions.xml" 1>&2 ; exit 1 ; }
	[ "`openssl dgst -sha512 -hex lowdown.tar.gz`" = "`cat lowdown.tar.gz.sha512`" ] || \
 		{ echo "Checksum does not match." 1>&2 ; exit 1 ; }
	rm -rf .distcheck
	mkdir -p .distcheck
	( cd .distcheck && tar -zvxpf ../lowdown.tar.gz )
	( cd .distcheck/lowdown-$(VERSION) && ./configure PREFIX=prefix )
	( cd .distcheck/lowdown-$(VERSION) && $(MAKE) )
	( cd .distcheck/lowdown-$(VERSION) && $(MAKE) regress )
	( cd .distcheck/lowdown-$(VERSION) && $(MAKE) install )
	rm -rf .distcheck

$(PDFS) index.xml README.xml: lowdown

index.html README.html: template.xml

.md.pdf:
	./lowdown --nroff-no-numbered -s -Tms $< | \
		pdfroff -i -mspdf -t -k -Kutf8 > $@

README.latex.pdf: README.md
	./lowdown -s -Tlatex README.md >README.latex.latex
	pdflatex README.latex.latex

README.nroff.pdf: README.md
	./lowdown --nroff-no-numbered -s -Tms README.md | \
		pdfroff -i -mspdf -t -k -Kutf8 > $@

.xml.html:
	sblg -t template.xml -s date -o $@ -C $< $< versions.xml

archive.html: archive.xml versions.xml
	sblg -t archive.xml -s date -o $@ versions.xml

atom.xml: atom-template.xml versions.xml
	sblg -a -t atom-template.xml -s date -o $@ versions.xml

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
	mandoc -Thtml -Ostyle=https://bsd.lv/css/mandoc.css $< >$@

lowdown.tar.gz.sha512: lowdown.tar.gz
	openssl dgst -sha512 -hex lowdown.tar.gz >$@

lowdown.tar.gz:
	mkdir -p .dist/lowdown-$(VERSION)/
	mkdir -p .dist/lowdown-$(VERSION)/man
	mkdir -p .dist/lowdown-$(VERSION)/regress/smarty
	mkdir -p .dist/lowdown-$(VERSION)/regress/MarkdownTest_1.0.3
	$(INSTALL) -m 0644 *.c *.h *.in.pc Makefile LICENSE.md .dist/lowdown-$(VERSION)
	$(INSTALL) -m 0644 man/*.1 man/*.3 man/*.5 .dist/lowdown-$(VERSION)/man
	$(INSTALL) -m 0755 configure .dist/lowdown-$(VERSION)
	$(INSTALL) -m 644 regress/MarkdownTest_1.0.3/*.text \
		.dist/lowdown-$(VERSION)/regress/MarkdownTest_1.0.3
	$(INSTALL) -m 644 regress/MarkdownTest_1.0.3/*.html \
		.dist/lowdown-$(VERSION)/regress/MarkdownTest_1.0.3
	$(INSTALL) -m 644 regress/smarty/*.md \
		.dist/lowdown-$(VERSION)/regress/smarty
	$(INSTALL) -m 644 regress/smarty/*.html \
		.dist/lowdown-$(VERSION)/regress/smarty
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

$(OBJS) $(COMPAT_OBJS) main.o: config.h

$(OBJS): extern.h lowdown.h

main.o: lowdown.h

clean:
	rm -f $(OBJS) $(COMPAT_OBJS) main.o
	rm -f lowdown lowdown-diff liblowdown.a lowdown.pc
	rm -f index.xml diff.xml diff.diff.xml README.xml lowdown.tar.gz.sha512 lowdown.tar.gz
	rm -f $(PDFS) $(HTMLS) $(THUMBS)
	rm -f README.latex.aux README.latex.latex README.latex.log README.latex.out

distclean: clean
	rm -f Makefile.configure config.h config.log

regress: lowdown
	tmp1=`mktemp` ; \
	tmp2=`mktemp` ; \
	for f in regress/MarkdownTest_1.0.3/*.text ; \
	do \
		echo "$$f" ; \
		want="`dirname \"$$f\"`/`basename \"$$f\" .text`.html" ; \
		sed -e '/^[ ]*$$/d' "$$want" > $$tmp1 ; \
		./lowdown $(REGRESS_ARGS) "$$f" | \
			sed -e 's!	! !g' | sed -e '/^[ ]*$$/d' > $$tmp2 ; \
		diff -uw $$tmp1 $$tmp2 ; \
	done  ; \
	for f in regress/smarty/*.md ; \
	do \
		echo "$$f" ; \
		./lowdown "$$f" > $$tmp1 ; \
		diff -uw `dirname $$f`/`basename $$f .md`.html $$tmp1 ; \
	done ; \
	rm -f $$tmp1 ; \
	rm -f $$tmp2

.png.thumb.jpg:
	convert $< -thumbnail 350 -quality 50 $@

.in.pc.pc:
	sed -e "s!@PREFIX@!$(PREFIX)!g" \
	    -e "s!@LIBDIR@!$(LIBDIR)!g" \
	    -e "s!@INCLUDEDIR@!$(INCLUDEDIR)!g" \
	    -e "s!@VERSION@!$(VERSION)!g" $< >$@
