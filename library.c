/*	$Id$ */
/*
 * Copyright (c) 2017, Kristaps Dzonsons
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"
#include "lowdown.h"

#define DEF_IUNIT 1024
#define DEF_OUNIT 64
#define DEF_MAX_NESTING 16

void
lowdown_buf(const struct lowdown_opts *opts, 
	const unsigned char *data, size_t datasz,
	unsigned char **res, size_t *rsz) 
{
	hbuf	 	*ob, *spb;
	hrend 		*renderer = NULL;
	hdoc 		*document;

	/* 
	 * Begin by creating our buffers, renderer, and document.
	 */

	ob = hbuf_new(DEF_OUNIT);
	spb = hbuf_new(DEF_OUNIT);

	renderer = NULL == opts || LOWDOWN_HTML == opts->type ?
		hrend_html_new
		(HOEDOWN_HTML_USE_XHTML |
		 HOEDOWN_HTML_ESCAPE | 
		 HOEDOWN_HTML_ASIDE, 0) :
		hrend_nroff_new
		(HOEDOWN_HTML_ESCAPE, 
		 LOWDOWN_MAN == opts->type);

	document = hdoc_new
		(renderer, 
		 HDOC_EXT_FOOTNOTES |
		 HDOC_EXT_AUTOLINK |
		 HDOC_EXT_TABLES |
		 HDOC_EXT_SUPERSCRIPT |
		 HDOC_EXT_STRIKETHROUGH |
		 HDOC_EXT_FENCED_CODE,
		 DEF_MAX_NESTING);

	/* Parse the output and free resources. */

	hdoc_render(document, ob, data, datasz);
	hdoc_free(document);

	/* Reprocess the output as smartypants. */

	if (NULL == opts || LOWDOWN_HTML == opts->type) {
		hrend_html_free(renderer);
		hsmrt_html(spb, ob->data, ob->size);
	} else {
		hrend_nroff_free(renderer);
		hsmrt_nroff(spb, ob->data, ob->size);
	}

	hbuf_free(ob);
	*res = spb->data;
	*rsz = spb->size;
	spb->data = NULL;
	hbuf_free(spb);
}

int
lowdown_file(const struct lowdown_opts *opts, 
	FILE *fin, unsigned char **res, size_t *rsz) 
{
	hbuf	 	*ib, *ob, *spb;
	hrend 		*renderer = NULL;
	hdoc 		*document;

	/* 
	 * Begin by creating our buffers, renderer, and document.
	 */

	ib = hbuf_new(DEF_IUNIT);
	ob = hbuf_new(DEF_OUNIT);
	spb = hbuf_new(DEF_OUNIT);

	renderer = NULL == opts || LOWDOWN_HTML == opts->type ?
		hrend_html_new
		(HOEDOWN_HTML_USE_XHTML |
		 HOEDOWN_HTML_ESCAPE | 
		 HOEDOWN_HTML_ASIDE, 0) :
		hrend_nroff_new
		(HOEDOWN_HTML_ESCAPE, 
		 LOWDOWN_MAN == opts->type);

	document = hdoc_new
		(renderer, 
		 HDOC_EXT_FOOTNOTES |
		 HDOC_EXT_AUTOLINK |
		 HDOC_EXT_TABLES |
		 HDOC_EXT_SUPERSCRIPT |
		 HDOC_EXT_STRIKETHROUGH |
		 HDOC_EXT_FENCED_CODE,
		 DEF_MAX_NESTING);

	/* Read from our input and close out the input .*/

	if (hbuf_putf(ib, fin))
		return(0);

	/* Parse the output and free resources. */

	hdoc_render(document, ob, ib->data, ib->size);
	hbuf_free(ib);
	hdoc_free(document);

	/* Reprocess the output as smartypants. */

	if (NULL == opts || LOWDOWN_HTML == opts->type) {
		hrend_html_free(renderer);
		hsmrt_html(spb, ob->data, ob->size);
	} else {
		hrend_nroff_free(renderer);
		hsmrt_nroff(spb, ob->data, ob->size);
	}

	hbuf_free(ob);
	*res = spb->data;
	*rsz = spb->size;
	spb->data = NULL;
	hbuf_free(spb);

	return(1);
}
