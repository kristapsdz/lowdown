/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016, Kristaps Dzonsons
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/param.h>
#ifdef __FreeBSD__
# include <sys/resource.h>
# include <sys/capability.h>
#endif

#include <err.h>
#include <errno.h>
#ifdef __APPLE__
# include <sandbox.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#define DEF_IUNIT 1024
#define DEF_OUNIT 64
#define DEF_MAX_NESTING 16

enum	out {
	OUT_HTML,
	OUT_NROFF
};

/*
 * Start with all of the sandboxes.
 * The sandbox_pre() happens before we open our input file for reading,
 * while the sandbox_post() happens afterward.
 */

#if defined(__OpenBSD__) && OpenBSD > 201510

static void
sandbox_post(int fdin, int fdout)
{

	if (-1 == pledge("stdio", NULL)) 
		err(EXIT_FAILURE, "pledge");
}

static void
sandbox_pre(void)
{

	if (-1 == pledge("stdio rpath wpath cpath", NULL)) 
		err(EXIT_FAILURE, "pledge");
}

#elif defined(__APPLE__)

static void
sandbox_post(int fdin, int fdout)
{
	char	*ep;
	int	 rc;

	rc = sandbox_init
		(kSBXProfilePureComputation, 
		 SANDBOX_NAMED, &ep);
	if (0 != rc)
		errx(EXIT_FAILURE, "sandbox_init: %s", ep);
}

static void
sandbox_pre(void)
{

	/* Do nothing. */
}

#elif defined(__FreeBSD__)

static void
sandbox_post(int fdin, int fdout)
{
	cap_rights_t	 rights;

	cap_rights_init(&rights);

	cap_rights_init(&rights, CAP_EVENT, CAP_READ, CAP_FSTAT);
	if (cap_rights_limit(fdin, &rights) < 0) 
 		err(EXIT_FAILURE, "cap_rights_limit");

	cap_rights_init(&rights, CAP_EVENT, CAP_WRITE, CAP_FSTAT);
	if (cap_rights_limit(STDERR_FILENO, &rights) < 0)
 		err(EXIT_FAILURE, "cap_rights_limit");

	cap_rights_init(&rights, CAP_EVENT, CAP_WRITE, CAP_FSTAT);
	if (cap_rights_limit(fdout, &rights) < 0)
 		err(EXIT_FAILURE, "cap_rights_limit");

	if (cap_enter())
		err(EXIT_FAILURE, "cap_enter");
}

static void
sandbox_pre(void)
{

	/* Do nothing. */
}

#else /* No sandbox. */

#warning Compiling without sandbox support.
static void
sandbox_post(int fdin, int fdout)
{

	/* Do nothing. */
}

static void
sandbox_pre(void)
{

	/* Do nothing. */
}

#endif

int
main(int argc, char *argv[])
{
	FILE		 *fin = stdin, *fout = stdout;
	const char	 *fnin = "<stdin>", *fnout = NULL,
	      		 *title = NULL;
	hbuf	 *ib, *ob, *spb;
	hoedown_renderer *renderer = NULL;
	hdoc 		 *document;
	const char	 *pname;
	int		  c, standalone = 0;
	enum out	  outm = OUT_HTML;

	sandbox_pre();

#ifdef __linux__
	pname = argv[0];
#else
	if (argc < 1)
		pname = "lowdown";
	else if (NULL == (pname = strrchr(argv[0], '/')))
		pname = argv[0];
	else
		++pname;
#endif

	while (-1 != (c = getopt(argc, argv, "st:T:o:")))
		switch (c) {
		case ('T'):
			if (0 == strcasecmp(optarg, "nroff"))
				outm = OUT_NROFF;
			else if (0 == strcasecmp(optarg, "html"))
				outm = OUT_HTML;
			else
				goto usage;
			break;
		case ('t'):
			title = optarg;
			break;
		case ('s'):
			standalone = 1;
			break;
		case ('o'):
			fnout = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (argc > 0 && strcmp(argv[0], "-")) {
		fnin = argv[0];
		if (NULL == (fin = fopen(fnin, "r")))
			err(EXIT_FAILURE, "%s", fnin);
	}

	if (NULL != fnout && strcmp(fnout, "-")) {
		if (NULL == (fout = fopen(fnout, "w")))
			err(EXIT_FAILURE, "%s", fnout);
	}

	sandbox_post(fileno(fin), fileno(fout));

	/* 
	 * We're now completely sandboxed.
	 * Nothing more is allowed to happen.
	 * Begin by creating our buffers, renderer, and document.
	 */

	ib = hbuf_new(DEF_IUNIT);
	ob = hbuf_new(DEF_OUNIT);
	spb = hbuf_new(DEF_OUNIT);

	renderer = OUT_HTML == outm ?
		hoedown_html_renderer_new
		(HOEDOWN_HTML_USE_XHTML |
		 HOEDOWN_HTML_ESCAPE | 
		 HOEDOWN_HTML_ASIDE, 0) :
		hoedown_nroff_renderer_new
		(HOEDOWN_HTML_ESCAPE, 0);

	document = hdoc_new
		(renderer, 
		 HDOC_EXT_FOOTNOTES |
		 HDOC_EXT_AUTOLINK |
		 HDOC_EXT_TABLES |
		 HDOC_EXT_STRIKETHROUGH |
		 HDOC_EXT_FENCED_CODE,
		 DEF_MAX_NESTING);

	/* Read from our input and close out the input .*/

	if (hbuf_putf(ib, fin))
		err(EXIT_FAILURE, "%s", fnin);
	if (fin != stdin) 
		fclose(fin);

	/* Parse the output and free resources. */

	hdoc_render(document, ob, ib->data, ib->size);
	hbuf_free(ib);
	hdoc_free(document);

	/* Reprocess the HTML as smartypants. */

	if (OUT_HTML == outm) {
		hoedown_html_renderer_free(renderer);
		hoedown_html_smartypants(spb, ob->data, ob->size);
		hbuf_free(ob);
		if (standalone)
			fprintf(fout, "<!DOCTYPE html>\n"
			      "<html>\n"
			      "<head>\n"
			      "<meta charset=\"utf-8\">\n"
			      "<meta name=\"viewport\" content=\""
			       "width=device-width,initial-scale=1\">\n"
			      "<title>%s</title>\n"
			      "</head>\n"
			      "<body>\n", NULL == title ?
			      "Untitled article" : title);
		fwrite(spb->data, 1, spb->size, fout);
		hbuf_free(spb);
		if (standalone)
			fputs("</body>\n"
			      "</html>\n", fout);
	} else {
		hoedown_nroff_renderer_free(renderer);
		hoedown_nroff_smartypants(spb, ob->data, ob->size);
		hbuf_free(ob);
		if (standalone)
			fprintf(fout, ".TL\n%s\n", NULL == title ?
				"Untitled article" : title);
		fwrite(spb->data, 1, spb->size, fout);
		hbuf_free(spb);
	}

	if (fout != stdout)
		fclose(fout);
	return(EXIT_SUCCESS);
usage:
	fprintf(stderr, "usage: %s "
		"[-s] "
		"[-o output] "
		"[-t title] "
		"[-T mode] "
		"[file]\n", pname);
	return(EXIT_FAILURE);
}
