/*	$Id$ */
/*
 * Copyright (c) 2017 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include "config.h"

#include <sys/queue.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "lowdown.h"
#include "extern.h"

#define DEF_IUNIT 1024
#define DEF_OUNIT 64

static	const char *const errs[LOWDOWN_ERR__MAX] = {
	"space before link (CommonMark violation)",
	"bad character in metadata key (MultiMarkdown violation)",
	"unknown footnote reference",
};

const char *
lowdown_errstr(enum lowdown_err err)
{

	return(errs[err]);
}

void
lowdown_buf(const struct lowdown_opts *opts,
	const unsigned char *data, size_t datasz,
	unsigned char **res, size_t *rsz,
	struct lowdown_meta **m, size_t *msz)
{
	hbuf	 	*ob, *spb;
	void 		*renderer;
	hdoc 		*document;
	size_t		 i;
	struct lowdown_node *n;

	/*
	 * Begin by creating our buffers, renderer, and document.
	 */

	ob = hbuf_new(DEF_OUNIT);

	renderer = NULL == opts || LOWDOWN_HTML == opts->type ?
		hrend_html_new(opts) : hrend_nroff_new(opts);

	document = hdoc_new(opts);

	/* Parse the output and free resources. */

	n = hdoc_parse(document, data, datasz, m, msz);
	hdoc_free(document);

	if (NULL == opts || LOWDOWN_HTML == opts->type) {
		lowdown_html_rndr(ob, renderer, n);
		hrend_html_free(renderer);
	} else {
		lowdown_nroff_rndr(ob, renderer, n);
		hrend_nroff_free(renderer);
	}

	lowdown_node_free(n);

	/*
	 * Now we escape all of our metadata values.
	 * This may not be standard (?), but it's required: we generally
	 * include metadata into our documents, and if not here, we'd
	 * leave escaping to our caller.
	 * Which we should never do!
	 */

	for (i = 0; i < *msz; i++) {
		spb = hbuf_new(DEF_OUNIT);
		if (NULL == opts ||
		    LOWDOWN_HTML == opts->type)
			hesc_html(spb, (uint8_t *)(*m)[i].value, 
				strlen((*m)[i].value), 0);
		else
			hesc_nroff(spb, (uint8_t *)(*m)[i].value, 
				strlen((*m)[i].value), 0, 1);
		free((*m)[i].value);
		(*m)[i].value = xstrndup
			((char *)spb->data, spb->size);
		hbuf_free(spb);
	}

	/* Reprocess the output as smartypants. */

	if (NULL != opts && 
	    LOWDOWN_SMARTY & opts->oflags) {
		spb = hbuf_new(DEF_OUNIT);
		if (LOWDOWN_HTML == opts->type)
			hsmrt_html(spb, ob->data, ob->size);
		else
			hsmrt_nroff(spb, ob->data, ob->size);
		*res = spb->data;
		*rsz = spb->size;
		spb->data = NULL;
		hbuf_free(spb);
		for (i = 0; i < *msz; i++) {
			spb = hbuf_new(DEF_OUNIT);
			if (NULL == opts ||
			    LOWDOWN_HTML == opts->type)
				hsmrt_html(spb, 
					(uint8_t *)(*m)[i].value, 
					strlen((*m)[i].value));
			else
				hsmrt_nroff(spb, 
					(uint8_t *)(*m)[i].value, 
					strlen((*m)[i].value));
			free((*m)[i].value);
			(*m)[i].value = xstrndup
				((char *)spb->data, spb->size);
			hbuf_free(spb);
		}
	} else {
		*res = ob->data;
		*rsz = ob->size;
		ob->data = NULL;
	}
	hbuf_free(ob);
}

int
lowdown_file(const struct lowdown_opts *opts,
	FILE *fin, unsigned char **res, size_t *rsz,
	struct lowdown_meta **m, size_t *msz)
{
	hbuf *ib = NULL;

	ib = hbuf_new(DEF_IUNIT);

	if (hbuf_putf(ib, fin)) {
		hbuf_free(ib);
		return(0);
	}

	lowdown_buf(opts, ib->data, ib->size, res, rsz, m, msz);
	hbuf_free(ib);
	return(1);
}
