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

/*
 * Start with all of the sandboxes.
 * The sandbox_pre() happens before we open our input file for reading,
 * while the sandbox_post() happens afterward.
 */

#if defined(__OpenBSD__) && OpenBSD > 201510

static void
sandbox_post(int fd)
{

	if (-1 == pledge("stdio", NULL)) 
		err(EXIT_FAILURE, "pledge");
}

static void
sandbox_pre(void)
{

	if (-1 == pledge("stdio rpath", NULL)) 
		err(EXIT_FAILURE, "pledge");
}

#elif defined(__APPLE__)

static void
sandbox_post(int fd)
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
sandbox_post(int fd)
{
	cap_rights_t	 rights;

	cap_rights_init(&rights);

	cap_rights_init(&rights, CAP_EVENT, CAP_READ, CAP_FSTAT);
	if (cap_rights_limit(fd, &rights) < 0) 
 		err(EXIT_FAILURE, "cap_rights_limit");

	cap_rights_init(&rights, CAP_EVENT, CAP_WRITE, CAP_FSTAT);
	if (cap_rights_limit(STDERR_FILENO, &rights) < 0)
 		err(EXIT_FAILURE, "cap_rights_limit");

	cap_rights_init(&rights, CAP_EVENT, CAP_WRITE, CAP_FSTAT);
	if (cap_rights_limit(STDOUT_FILENO, &rights) < 0)
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
sandbox_post(int fd)
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
	const char	 *fname = "<stdin>";
	hoedown_buffer	 *ib, *ob, *spb;
	hoedown_renderer *renderer = NULL;
	hoedown_document *document;
	const char	 *pname;
	int		  c, standalone = 0;

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

	sandbox_pre();

	while (-1 != (c = getopt(argc, argv, "s")))
		switch (c) {
		case ('s'):
			standalone = 1;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (argc > 0 && strcmp(argv[0], "-")) {
		fname = argv[0];
		if (NULL == (fin = fopen(fname, "r")))
			err(EXIT_FAILURE, "%s", fname);
	}

	sandbox_post(fileno(fin));

	/* 
	 * We're now completely sandboxed.
	 * Nothing more is allowed to happen.
	 * Begin by creating our buffers, renderer, and document.
	 */

	ib = hoedown_buffer_new(DEF_IUNIT);
	ob = hoedown_buffer_new(DEF_OUNIT);
	spb = hoedown_buffer_new(DEF_OUNIT);

	renderer = hoedown_html_renderer_new
		(HOEDOWN_HTML_USE_XHTML |
		 HOEDOWN_HTML_ASIDE, 0);
	document = hoedown_document_new
		(renderer, 
		 HOEDOWN_EXT_FOOTNOTES |
		 HOEDOWN_EXT_AUTOLINK |
		 HOEDOWN_EXT_TABLES |
		 HOEDOWN_EXT_STRIKETHROUGH |
		 HOEDOWN_EXT_FENCED_CODE,
		 DEF_MAX_NESTING);

	/* Read from our input and close out the input .*/

	if (hoedown_buffer_putf(ib, fin))
		err(EXIT_FAILURE, "%s", fname);
	if (fin != stdin) 
		fclose(fin);

	/* Parse the output and free resources. */

	hoedown_document_render(document, ob, ib->data, ib->size);

	hoedown_buffer_free(ib);
	hoedown_document_free(document);
	hoedown_html_renderer_free(renderer);

	/* Reprocess the HTML as smartypants. */

	hoedown_html_smartypants(spb, ob->data, ob->size);
	hoedown_buffer_free(ob);

	/* Push to output. */

	if (standalone)
		fputs("<!DOCTYPE html>\n"
		      "<html>\n"
		      "<head>\n"
		      "<meta charset=\"utf-8\">\n"
		      "<meta name=\"viewport\" content=\""
		       "width=device-width,initial-scale=1\">\n"
		      "<title></title>\n"
		      "</head>\n"
		      "<body>\n", fout);
	fwrite(spb->data, 1, spb->size, fout);
	if (standalone)
		fputs("</body>\n"
		      "</html>\n", fout);

	hoedown_buffer_free(spb);
	return(EXIT_SUCCESS);
usage:
	fprintf(stderr, "usage: %s [-s] [file]\n", pname);
	return(EXIT_FAILURE);
}
