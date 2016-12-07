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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#define DEF_IUNIT 1024
#define DEF_OUNIT 64
#define DEF_MAX_NESTING 16

struct opts {
	size_t 		 iunit;
	size_t		 ounit;
	int 		 toc_level;
	hoedown_html_flags html_flags;
	hoedown_extensions extensions;
	size_t 		 max_nesting;
};

int
main(int argc, char *argv[])
{
	struct opts	 data;
	FILE		*file = stdin;
	const char	*fname = "<stdin>";
	hoedown_buffer	*ib, *ob;
	hoedown_renderer *renderer = NULL;
	hoedown_document *document;

	memset(&data, 0, sizeof(struct opts));

	data.iunit = DEF_IUNIT;
	data.ounit = DEF_OUNIT;
	data.max_nesting = DEF_MAX_NESTING;
	data.html_flags = HOEDOWN_HTML_USE_XHTML;

	if (-1 != getopt(argc, argv, ""))
		goto usage;

	argc -= optind;
	argv += optind;

	if (argc > 0) {
		fname = argv[0];
		if (NULL == (file = fopen(fname, "r")))
			err(EXIT_FAILURE, "%s", fname);
	}

	ib = hoedown_buffer_new(data.iunit);

	if (hoedown_buffer_putf(ib, file))
		err(EXIT_FAILURE, "%s", fname);

	if (file != stdin) 
		fclose(file);

	renderer = hoedown_html_renderer_new
		(data.html_flags, data.toc_level);

	ob = hoedown_buffer_new(data.ounit);
	document = hoedown_document_new
		(renderer, data.extensions, data.max_nesting);
	hoedown_document_render(document, ob, ib->data, ib->size);

	hoedown_buffer_free(ib);
	hoedown_document_free(document);
	hoedown_html_renderer_free(renderer);

	fwrite(ob->data, 1, ob->size, stdout);
	hoedown_buffer_free(ob);

	return(EXIT_SUCCESS);
usage:
	return(EXIT_FAILURE);
}
