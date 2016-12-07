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
#include <err.h>
#include <errno.h>
#ifdef __APPLE__
# include <sandbox.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#define DEF_IUNIT 1024
#define DEF_OUNIT 64
#define DEF_MAX_NESTING 16

struct opts {
};

int
main(int argc, char *argv[])
{
	FILE		 *file = stdin;
	const char	 *fname = "<stdin>";
	hoedown_buffer	 *ib, *ob;
	hoedown_renderer *renderer = NULL;
	hoedown_document *document;

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio rpath", NULL)) 
		err(EXIT_FAILURE, "pledge");
#endif

	if (-1 != getopt(argc, argv, ""))
		goto usage;

	argc -= optind;
	argv += optind;

	if (argc > 0) {
		fname = argv[0];
		if (NULL == (file = fopen(fname, "r")))
			err(EXIT_FAILURE, "%s", fname);
	}

#if defined(__OpenBSD__) && OpenBSD > 201510
	if (-1 == pledge("stdio", NULL)) 
		err(EXIT_FAILURE, "pledge");
#elif defined(__APPLE__)
	if (sandbox_init(kSBXProfilePureComputation, SANDBOX_NAMED, NULL))
		err(EXIT_FAILURE, "sandbox_init");
#endif

	ib = hoedown_buffer_new(DEF_IUNIT);

	if (hoedown_buffer_putf(ib, file))
		err(EXIT_FAILURE, "%s", fname);

	if (file != stdin) 
		fclose(file);

	renderer = hoedown_html_renderer_new
		(HOEDOWN_HTML_USE_XHTML, 0);

	ob = hoedown_buffer_new(DEF_OUNIT);
	document = hoedown_document_new
		(renderer, 0, DEF_MAX_NESTING);
	hoedown_document_render(document, ob, ib->data, ib->size);

	hoedown_buffer_free(ib);
	hoedown_document_free(document);
	hoedown_html_renderer_free(renderer);

	fwrite(ob->data, 1, ob->size, stdout);
	hoedown_buffer_free(ob);

	return(EXIT_SUCCESS);
usage:
	fprintf(stderr, "usage: %s [file]\n", getprogname());
	return(EXIT_FAILURE);
}
