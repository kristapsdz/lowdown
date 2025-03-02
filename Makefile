.PHONY: regress regen_regress valgrind
.SUFFIXES: .xml .md .html .pdf .1 .1.html .3 .3.html .5 .5.html .thumb.jpg .png .in.pc .pc .old.md

include Makefile.configure
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/lowdown
sinclude Makefile.local

# Follows semver.
# This is complex because lowdown is both a program and a library; and
# while libraries have well-defined semantics of semver change, programs
# do not.  Let the library guide our versioning until a better way is
# thought out.
VERSION		 = 2.0.2
# This is the major number of VERSION.  It might later become
# MAJOR.MINOR, if the library moves a lot.
LIBVER		 = 2
OBJS		 = autolink.o \
		   buffer.o \
		   diff.o \
		   document.o \
		   entity.o \
		   gemini.o \
		   gemini_escape.o \
		   html.o \
		   html_escape.o \
		   latex.o \
		   latex_escape.o \
		   library.o \
		   libdiff.o \
		   nroff.o \
		   nroff_escape.o \
		   odt.o \
		   smartypants.o \
		   template.o \
		   term.o \
		   tree.o \
		   util.o
COMPAT_OBJS	 = compats.o
HTMLS		 = archive.html \
		   atom.xml \
		   diff.html \
		   diff.diff.html \
		   index.html \
		   README.html \
		   $(MANS)
MANS		 = $(MAN1S) $(MAN3S) $(MAN5S)
MAN1S		 = man/lowdown.1.html \
		   man/lowdown-diff.1.html
MAN5S =  	   man/lowdown.5.html
MAN3S = 	   man/lowdown.3.html \
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
		   man/lowdown_node_free.3.html \
		   man/lowdown_nroff_free.3.html \
		   man/lowdown_nroff_new.3.html \
		   man/lowdown_nroff_rndr.3.html \
		   man/lowdown_odt_free.3.html \
		   man/lowdown_odt_new.3.html \
		   man/lowdown_odt_rndr.3.html \
		   man/lowdown_term_free.3.html \
		   man/lowdown_term_new.3.html \
		   man/lowdown_term_rndr.3.html \
		   man/lowdown_tree_rndr.3.html
SOURCES		 = autolink.c \
		   buffer.c \
		   compats.c \
		   diff.c \
		   document.c \
		   entity.c \
		   gemini.c \
		   gemini_escape.c \
		   html.c \
		   html_escape.c \
		   latex.c \
		   latex_escape.c \
		   libdiff.c \
		   library.c \
		   main.c \
		   nroff.c \
		   nroff_escape.c \
		   odt.c \
		   smartypants.c \
		   template.c \
		   term.c \
		   tests.c \
		   tree.c \
		   util.c
HEADERS 	 = extern.h \
		   libdiff.h \
		   lowdown.h \
		   term.h
PDFS		 = diff.pdf \
		   diff.diff.pdf \
		   index.latex.pdf \
		   index.mandoc.pdf \
		   index.nroff.pdf
MDS		 = index.md README.md
CSSS		 = diff.css template.css
JSS		 = diff.js
IMAGES		 = screen-mandoc.png \
		   screen-groff.png \
		   screen-term.png
THUMBS		 = screen-mandoc.thumb.jpg \
		   screen-groff.thumb.jpg \
		   screen-term.thumb.jpg
CFLAGS		+= -DVERSION=\"$(VERSION)\"
# Hack around broken Mac OS X nested sandboxes.
# If SANDBOX_INIT_ERROR_IGNORE is set to "always", errors from
# sandbox_init() are ignored.  If set to anything else, the user must
# also set SANDBOX_INIT_ERROR_IGNORE in their environment to ignore
# failure.
# Has no effect unless HAVE_SANDBOX_INIT is defined.
.ifdef SANDBOX_INIT_ERROR_IGNORE
.if $(SANDBOX_INIT_ERROR_IGNORE) == "always"
CFLAGS		+= -DSANDBOX_INIT_ERROR_IGNORE=2
.else
CFLAGS		+= -DSANDBOX_INIT_ERROR_IGNORE=1
.endif
.endif
# Because the objects will be compiled into a shared library:
CFLAGS		+= -fPIC
# To avoid exporting internal functions (lowdown.h has default visibility).
CFLAGS		+= -fvisibility=hidden

ifeq ($(LINK_METHOD),"static")
LIB_LOWDOWN = liblowdown.a
else
LIB_LOWDOWN = liblowdown.so
endif

# Only for MarkdownTestv1.0.3 in regress/original.

REGRESS_ARGS	 = "--out-no-smarty"
REGRESS_ARGS	+= "--parse-no-img-ext"
REGRESS_ARGS	+= "--parse-no-metadata"
REGRESS_ARGS	+= "--parse-super-short"
REGRESS_ARGS	+= "--html-no-head-ids"
REGRESS_ARGS	+= "--html-no-skiphtml"
REGRESS_ARGS	+= "--html-no-escapehtml"
REGRESS_ARGS	+= "--html-no-owasp"
REGRESS_ARGS	+= "--html-no-num-ent"
REGRESS_ARGS	+= "--parse-no-autolink"
REGRESS_ARGS	+= "--parse-no-cmark"
REGRESS_ARGS	+= "--parse-no-deflists"

REGRESS_ENV	 = LC_ALL=en_US.UTF-8

all: bins lowdown.pc liblowdown.so
bins: lowdown lowdown-diff

www: all $(HTMLS) $(PDFS) $(THUMBS) lowdown.tar.gz lowdown.tar.gz.sha512

installwww: www
	mkdir -p $(WWWDIR)/snapshots
	$(INSTALL) -m 0444 $(THUMBS) $(IMAGES) $(MDS) $(HTMLS) $(CSSS) $(JSS) $(PDFS) $(WWWDIR)
	$(INSTALL) -m 0444 lowdown.tar.gz $(WWWDIR)/snapshots/lowdown-$(VERSION).tar.gz
	$(INSTALL) -m 0444 lowdown.tar.gz.sha512 $(WWWDIR)/snapshots/lowdown-$(VERSION).tar.gz.sha512
	$(INSTALL) -m 0444 lowdown.tar.gz $(WWWDIR)/snapshots
	$(INSTALL) -m 0444 lowdown.tar.gz.sha512 $(WWWDIR)/snapshots

lowdown: main.o $(LIB_LOWDOWN)
	$(CC) -o $@ $+ $(LDFLAGS) $(LDADD_MD5) -lm $(LDADD)

lowdown-diff: lowdown
	ln -sf lowdown lowdown-diff

liblowdown.a: $(OBJS) $(COMPAT_OBJS)
	$(AR) rs $@ $(OBJS) $(COMPAT_OBJS)

liblowdown.so: $(OBJS) $(COMPAT_OBJS)
	$(CC) -shared -o $@.$(LIBVER) $(OBJS) $(COMPAT_OBJS) $(LDFLAGS) $(LDADD_MD5) -lm -Wl,${LINKER_SONAME},$@.$(LIBVER) $(LDLIBS)
	ln -sf $@.$(LIBVER) $@

uninstall:
	rm -rf $(SHAREDIR)/lowdown
	rm -f $(BINDIR)/lowdown $(BINDIR)/lowdown-diff
	for f in $(MAN1S) $(MAN5S) ; do \
		name=`basename $$f .html` ; \
		section=$${name##*.} ; \
		rm -f $(MANDIR)/man$$section/$$name ; \
	done

install: bins
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(MANDIR)/man5
	mkdir -p $(DESTDIR)$(SHAREDIR)/lowdown/html
	mkdir -p $(DESTDIR)$(SHAREDIR)/lowdown/latex
	mkdir -p $(DESTDIR)$(SHAREDIR)/lowdown/man
	mkdir -p $(DESTDIR)$(SHAREDIR)/lowdown/ms
	mkdir -p $(DESTDIR)$(SHAREDIR)/lowdown/odt
	$(INSTALL_DATA) share/html/* $(DESTDIR)$(SHAREDIR)/lowdown/html
	$(INSTALL_DATA) share/latex/* $(DESTDIR)$(SHAREDIR)/lowdown/latex
	$(INSTALL_DATA) share/man/* $(DESTDIR)$(SHAREDIR)/lowdown/man
	$(INSTALL_DATA) share/ms/* $(DESTDIR)$(SHAREDIR)/lowdown/ms
	$(INSTALL_DATA) share/odt/* $(DESTDIR)$(SHAREDIR)/lowdown/odt
	$(INSTALL_PROGRAM) lowdown $(DESTDIR)$(BINDIR)
	$(INSTALL_PROGRAM) lowdown-diff $(DESTDIR)$(BINDIR)
	for f in $(MAN1S) $(MAN5S) ; do \
		name=`basename $$f .html` ; \
		section=$${name##*.} ; \
		$(INSTALL_MAN) man/$$name $(DESTDIR)$(MANDIR)/man$$section ; \
	done

uninstall_lib_common:
	rm -f $(LIBDIR)/pkgconfig/lowdown.pc
	rm -f $(INCLUDEDIR)/lowdown.h
	for f in $(MAN3S) ; do \
		name=`basename $$f .html` ; \
		section=$${name##*.} ; \
		rm -f $(MANDIR)/man$$section/$$name ; \
	done

install_lib_common: lowdown.pc
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	mkdir -p $(DESTDIR)$(LIBDIR)/pkgconfig
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL_DATA) lowdown.pc $(DESTDIR)$(LIBDIR)/pkgconfig
	$(INSTALL_DATA) lowdown.h $(DESTDIR)$(INCLUDEDIR)
	for f in $(MAN3S) ; do \
		name=`basename $$f .html` ; \
		section=$${name##*.} ; \
		$(INSTALL_MAN) man/$$name $(DESTDIR)$(MANDIR)/man$$section ; \
	done

uninstall_shared: uninstall_lib_common
	rm -f $(LIBDIR)/liblowdown.so.$(LIBVER) $(LIBDIR)/liblowdown.so

install_shared: liblowdown.so install_lib_common
	$(INSTALL_LIB) liblowdown.so.$(LIBVER) $(DESTDIR)$(LIBDIR)
	( cd $(DESTDIR)$(LIBDIR) && ln -sf liblowdown.so.$(LIBVER) liblowdown.so )

uninstall_static: uninstall_lib_common
	rm -f $(LIBDIR)/liblowdown.a

install_static: liblowdown.a install_lib_common
	$(INSTALL_LIB) liblowdown.a $(DESTDIR)$(LIBDIR)

uninstall_libs: uninstall_shared uninstall_static

install_libs: install_shared install_static

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
	./lowdown --roff-no-numbered -s -tms $< | \
		pdfroff -i -mspdf -t -k > $@

index.latex.pdf: index.md $(THUMBS)
	./lowdown -s -tlatex index.md >index.latex.latex
	pdflatex index.latex.latex
	pdflatex index.latex.latex

index.mandoc.pdf: index.md
	./lowdown --roff-no-numbered -s -tman index.md | \
		mandoc -Tpdf > $@

index.nroff.pdf: index.md
	./lowdown --roff-no-numbered -s -tms index.md | \
		pdfroff -i -mspdf -t -k > $@

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

diff.diff.pdf: diff.md diff.old.md lowdown-diff
	./lowdown-diff --roff-no-numbered -s -tms diff.old.md diff.md | \
		pdfroff -i -mspdf -t -k > $@

$(HTMLS): versions.xml lowdown

.md.xml: lowdown
	( echo "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" ; \
	  echo "<article data-sblg-article=\"1\">" ; \
	  ./lowdown $< ; \
	  echo "</article>" ; ) >$@

index.xml: index.md coverage.md coverage-table.md lowdown
	( echo "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" ; \
	  echo "<article data-sblg-article=\"1\">" ; \
	  cat index.md coverage.md coverage-table.md | ./lowdown ; \
	  echo "</article>" ; ) >$@

.1.1.html .3.3.html .5.5.html:
	mandoc -Thtml -Ostyle=https://bsd.lv/css/mandoc.css $< >$@

lowdown.tar.gz.sha512: lowdown.tar.gz
	openssl dgst -sha512 -hex lowdown.tar.gz >$@

lowdown.tar.gz:
	mkdir -p .dist/lowdown-$(VERSION)/
	mkdir -p .dist/lowdown-$(VERSION)/man
	mkdir -p .dist/lowdown-$(VERSION)/share/html
	mkdir -p .dist/lowdown-$(VERSION)/share/latex
	mkdir -p .dist/lowdown-$(VERSION)/share/man
	mkdir -p .dist/lowdown-$(VERSION)/share/ms
	mkdir -p .dist/lowdown-$(VERSION)/share/odt
	mkdir -p .dist/lowdown-$(VERSION)/regress/html
	mkdir -p .dist/lowdown-$(VERSION)/regress/metadata
	mkdir -p .dist/lowdown-$(VERSION)/regress/original
	mkdir -p .dist/lowdown-$(VERSION)/regress/standalone
	mkdir -p .dist/lowdown-$(VERSION)/regress/template
	mkdir -p .dist/lowdown-$(VERSION)/regress/diff
	$(INSTALL) -m 0644 $(HEADERS) .dist/lowdown-$(VERSION)
	$(INSTALL) -m 0644 $(SOURCES) .dist/lowdown-$(VERSION)
	$(INSTALL) -m 0644 share/html/* .dist/lowdown-$(VERSION)/share/html
	$(INSTALL) -m 0644 share/latex/* .dist/lowdown-$(VERSION)/share/latex
	$(INSTALL) -m 0644 share/man/* .dist/lowdown-$(VERSION)/share/man
	$(INSTALL) -m 0644 share/ms/* .dist/lowdown-$(VERSION)/share/ms
	$(INSTALL) -m 0644 share/odt/* .dist/lowdown-$(VERSION)/share/odt
	$(INSTALL) -m 0644 lowdown.in.pc Makefile LICENSE.md .dist/lowdown-$(VERSION)
	$(INSTALL) -m 0644 man/*.1 man/*.3 man/*.5 .dist/lowdown-$(VERSION)/man
	$(INSTALL) -m 0755 configure .dist/lowdown-$(VERSION)
	$(INSTALL) -m 644 regress/original/* .dist/lowdown-$(VERSION)/regress/original
	$(INSTALL) -m 644 regress/*.* .dist/lowdown-$(VERSION)/regress
	$(INSTALL) -m 644 regress/diff/* .dist/lowdown-$(VERSION)/regress/diff
	$(INSTALL) -m 644 regress/html/* .dist/lowdown-$(VERSION)/regress/html
	$(INSTALL) -m 644 regress/metadata/* .dist/lowdown-$(VERSION)/regress/metadata
	$(INSTALL) -m 644 regress/standalone/* .dist/lowdown-$(VERSION)/regress/standalone
	$(INSTALL) -m 644 regress/template/* .dist/lowdown-$(VERSION)/regress/template
	( cd .dist/ && tar zcf ../$@ lowdown-$(VERSION) )
	rm -rf .dist/

$(OBJS) $(COMPAT_OBJS) main.o: config.h

$(OBJS): extern.h lowdown.h

term.o: term.h

main.o: lowdown.h

clean:
	rm -f $(OBJS) $(COMPAT_OBJS) main.o
	rm -f lowdown lowdown-diff liblowdown.a liblowdown.so liblowdown.so.$(LIBVER) lowdown.pc
	rm -f index.xml diff.xml diff.diff.xml README.xml lowdown.tar.gz.sha512 lowdown.tar.gz
	rm -f $(PDFS) $(HTMLS) $(THUMBS)
	rm -f index.latex.aux index.latex.latex index.latex.log index.latex.out

distclean: clean
	rm -f Makefile.configure config.h config.log config.h.old config.log.old

coverage-table.md:
	$(MAKE) clean
	CC=gcc CFLAGS="--coverage" ./configure LDFLAGS="--coverage"
	$(MAKE) regress
	( echo "| Files | Coverage |" ; \
	  echo "|-------|----------|" ; \
	  for f in $(OBJS) ; do \
	  	src=$$(basename $$f .o).c ; \
		link=https://github.com/kristapsdz/lowdown/blob/master/$$src ; \
		pct=$$(gcov -H $$src | grep 'Lines executed' | head -n1 | \
			cut -d ":" -f 2 | cut -d "%" -f 1) ; \
	  	echo "| [$$src]($$link) | $$pct% | " ; \
	  done ; \
	) >coverage-table.md

regen_regress: bins
	@tmp1=`mktemp` ; \
	tmp2=`mktemp` ; \
	set +e ; \
	for f in regress/*.md ; do \
		ff=regress/`basename $$f .md` ; \
		for type in html fodt latex ms man gemini term ; do \
			if [ -f $$ff.$$type ]; then \
				./lowdown -t$$type $$f >$$tmp1 2>&1 ; \
				diff -uw $$ff.$$type $$tmp1 ; \
				[ $$? -eq 0 ] || { \
					echo "$$f" ; \
					echo -n "Replace? " ; \
					read ; \
					mv $$tmp1 $$ff.$$type ; \
				} ; \
			fi ; \
		done ; \
	done ; \
	for f in regress/standalone/*.md ; do \
		ff=regress/standalone/`basename $$f .md` ; \
		for type in html fodt latex ms man gemini term ; do \
			if [ -f $$ff.$$type ]; then \
				./lowdown -s -t$$type $$f >$$tmp1 2>&1 ; \
				diff -uw $$ff.$$type $$tmp1 ; \
				[ $$? -eq 0 ] || { \
					echo -n "Replace? " ; \
					read ; \
					mv $$tmp1 $$ff.$$type ; \
				} ; \
			fi ; \
		done ; \
	done ; \
	for f in regress/diff/*.old.md ; do \
		bf=`dirname $$f`/`basename $$f .old.md` ; \
		echo "$$f -> $$bf.new.md" ; \
		for type in html fodt latex ms man gemini term ; do \
			if [ -f $$bf.$$type ]; then \
				./lowdown-diff -s -t$$type $$f $$bf.new.md >$$tmp1 2>&1 ; \
				diff -uw $$bf.$$type $$tmp1 ; \
				[ $$? -eq 0 ] || { \
					echo -n "Replace? " ; \
					read ; \
					mv $$tmp1 $$bf.$$type ; \
				} ; \
			fi ; \
		done ; \
	done ; \
	rm -f $$tmp1 ; \
	rm -f $$tmp2

valgrind::
	@ulimit -n 1024 ; \
	tmp=`mktemp` ; \
	VALGRIND="valgrind -q --leak-check=full --leak-resolution=high --show-reachable=yes --log-fd=3" $(MAKE) regress 3>$$tmp ; \
	rc=$$? ; \
	[ ! -s $$tmp ] || rc=1 ; \
	cat $$tmp ; \
	rm -f $$tmp ; \
	exit $$rc

regress:: bins
	@tmp1=`mktemp` ; \
	tmp2=`mktemp` ; \
	rc=0 ; \
	for f in regress/original/*.text ; do \
		echo "$$f" ; \
		want="`dirname \"$$f\"`/`basename \"$$f\" .text`.html" ; \
		sed -e '/^[ ]*$$/d' "$$want" > $$tmp1 ; \
		$(REGRESS_ENV) $(VALGRIND) ./lowdown $(REGRESS_ARGS) "$$f" | \
			sed -e 's!	! !g' | sed -e '/^[ ]*$$/d' > $$tmp2 ; \
		diff -uw $$tmp1 $$tmp2 || rc=$$((rc + 1)) ; \
		for type in html fodt latex ms man gemini term tree ; do \
			$(REGRESS_ENV) $(VALGRIND) ./lowdown -s -t$$type "$$f" >/dev/null 2>&1 ; \
		done ; \
	done  ; \
	for f in regress/*.md ; do \
		ff=regress/`basename $$f .md` ; \
		echo "$$f" ; \
		for type in html fodt latex ms man gemini term ; do \
			if [ -f $$ff.$$type ]; then \
				$(REGRESS_ENV) $(VALGRIND) ./lowdown -t$$type $$f >$$tmp1 2>&1 ; \
				diff -uw $$ff.$$type $$tmp1 || rc=$$((rc + 1)) ; \
			fi ; \
		done ; \
	done ; \
	for f in regress/standalone/*.md ; do \
		ff=regress/standalone/`basename $$f .md` ; \
		echo "$$f" ; \
		for type in html fodt latex ms man gemini term ; do \
			if [ -f $$ff.$$type ]; then \
				$(REGRESS_ENV) $(VALGRIND) ./lowdown -s -t$$type $$f >$$tmp1 2>&1 ; \
				diff -uw $$ff.$$type $$tmp1 || rc=$$((rc + 1)) ; \
			fi ; \
		done ; \
	done ; \
	for f in regress/template/*.html ; do \
		ff=regress/template/`basename $$f .html` ; \
		echo "$$f" ; \
		tf=regress/template/simple.md ; \
		[ ! -f $$ff.md ] || tf=$$ff.md ; \
		$(REGRESS_ENV) $(VALGRIND) ./lowdown -M "blank=" --template $$ff.xml -s $$tf >$$tmp1 2>&1 ; \
		diff -uw $$f $$tmp1 || rc=$$((rc + 1)) ; \
	done ; \
	for f in regress/html/*.md ; do \
		ff=regress/html/`basename $$f .md` ; \
		echo "$$f" ; \
		$(REGRESS_ENV) $(VALGRIND) ./lowdown -thtml --parse-math --html-callout-gfm \
			--html-callout-mdn --html-titleblock $$f >$$tmp1 2>&1 ; \
		diff -uw $$ff.html $$tmp1 || rc=$$((rc + 1)) ; \
	done ; \
	for f in regress/metadata/*.md ; do \
		echo "$$f" ; \
		if [ -f regress/metadata/`basename $$f .md`.txt ]; then \
			$(REGRESS_ENV) $(VALGRIND) ./lowdown -X title $$f >$$tmp1 2>&1 ; \
			diff -uw regress/metadata/`basename $$f .md`.txt $$tmp1 || rc=$$((rc + 1)) ; \
		fi ; \
	done ; \
	for f in regress/diff/*.old.md ; do \
		bf=`dirname $$f`/`basename $$f .old.md` ; \
		echo "$$f -> $$bf.new.md" ; \
		for type in html fodt latex ms man gemini term ; do \
			if [ -f $$bf.$$type ]; then \
				$(REGRESS_ENV) $(VALGRIND) ./lowdown-diff -s -t$$type $$f $$bf.new.md >$$tmp1 2>&1 ; \
				diff -uw $$bf.$$type $$tmp1 || rc=$$((rc + 1)) ; \
			fi ; \
		done ; \
	done ; \
	rm -f $$tmp1 ; \
	rm -f $$tmp2 ; \
	if [ $$rc -gt 0 ]; then \
		echo "Failed with $$rc test failures" 1>&2 ; \
		exit 1 ; \
	fi

.png.thumb.jpg:
	convert $< -thumbnail 350 -quality 50 $@

.in.pc.pc:
	sed -e "s!@PREFIX@!$(PREFIX)!g" \
	    -e "s!@LIBDIR@!$(LIBDIR)!g" \
	    -e "s!@INCLUDEDIR@!$(INCLUDEDIR)!g" \
	    -e "s!@VERSION@!$(VERSION)!g" $< >$@
