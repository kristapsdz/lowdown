/*	$Id$ */
/*
 * Copyright (c) 2017, 2018 Kristaps Dzonsons <kristaps@bsd.lv>
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

#if HAVE_SYS_QUEUE
# include <sys/queue.h>
#endif

#include <assert.h>
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

/*
 * Documented in lowdown_buf(3).
 */
void
lowdown_buf(const struct lowdown_opts *opts,
	const char *data, size_t datasz,
	char **res, size_t *rsz,
	struct lowdown_meta **m, size_t *msz)
{
	hbuf	 	 *ob, *spb;
	void 		 *renderer = NULL;
	hdoc 		 *document;
	size_t		  i;
	enum lowdown_type t;
	struct lowdown_node *n;

	/* Create our buffers, renderer, and document. */

	ob = hbuf_new(DEF_OUNIT);
	document = lowdown_doc_new(opts);
	t = NULL == opts ? LOWDOWN_HTML : opts->type;

	switch (t) {
	case (LOWDOWN_HTML):
		renderer = lowdown_html_new(opts);
		break;
	case (LOWDOWN_MAN):
	case (LOWDOWN_NROFF):
		renderer = lowdown_nroff_new(opts);
		break;
	case (LOWDOWN_TREE):
		renderer = lowdown_tree_new();
		break;
	}

	/* Parse the output and free resources. */

	n = lowdown_doc_parse(document, data, datasz, m, msz);
	lowdown_doc_free(document);

	switch (t) {
	case (LOWDOWN_HTML):
		lowdown_html_rndr(ob, renderer, n);
		lowdown_html_free(renderer);
		break;
	case (LOWDOWN_MAN):
	case (LOWDOWN_NROFF):
		lowdown_nroff_rndr(ob, renderer, n);
		lowdown_nroff_free(renderer);
		break;
	case (LOWDOWN_TREE):
		lowdown_tree_rndr(ob, renderer, n);
		lowdown_tree_free(renderer);
		break;
	}

	lowdown_node_free(n);

	/*
	 * Now we escape all of our metadata values.
	 * This may not be standard (?), but it's required: we generally
	 * include metadata into our documents, and if not here, we'd
	 * leave escaping to our caller.
	 * Which we should never do!
	 */

	if (LOWDOWN_TREE != t) 
		for (i = 0; i < *msz; i++) {
			spb = hbuf_new(DEF_OUNIT);
			if (LOWDOWN_HTML == t)
				hesc_html(spb, (*m)[i].value, 
					strlen((*m)[i].value), 0);
			else
				hesc_nroff(spb, (*m)[i].value, 
					strlen((*m)[i].value), 0, 1);
			free((*m)[i].value);
			(*m)[i].value = xstrndup(spb->data, spb->size);
			hbuf_free(spb);
		}

	/* Reprocess the output as smartypants. */

	if (LOWDOWN_TREE != t &&
	    NULL != opts && LOWDOWN_SMARTY & opts->oflags) {
		spb = hbuf_new(DEF_OUNIT);
		if (LOWDOWN_HTML == t)
			lowdown_html_smrt(spb, ob->data, ob->size);
		else
			lowdown_nroff_smrt(spb, ob->data, ob->size);
		*res = spb->data;
		*rsz = spb->size;
		spb->data = NULL;
		hbuf_free(spb);
		for (i = 0; i < *msz; i++) {
			spb = hbuf_new(DEF_OUNIT);
			if (LOWDOWN_HTML == t)
				lowdown_html_smrt(spb, (*m)[i].value, 
					strlen((*m)[i].value));
			else
				lowdown_nroff_smrt(spb, (*m)[i].value, 
					strlen((*m)[i].value));
			free((*m)[i].value);
			(*m)[i].value = xstrndup(spb->data, spb->size);
			hbuf_free(spb);
		}
	} else {
		*res = ob->data;
		*rsz = ob->size;
		ob->data = NULL;
	}
	hbuf_free(ob);
}

/*
 * Merge adjacent text nodes into single text nodes, freeing the
 * duplicates along the way.
 * This is only used when diffing, as it makes the diff algorithm hvae a
 * more reasonable view of text in the tree.
 */
static void
lowdown_merge_adjacent_text(struct lowdown_node *n)
{
	struct lowdown_node *nn, *next;
	hbuf	*nb, *nextbuf;

	TAILQ_FOREACH(nn, &n->children, entries) {
		if (LOWDOWN_NORMAL_TEXT != nn->type) {
			lowdown_merge_adjacent_text(nn);
			continue;
		}
		nb = &nn->rndr_normal_text.text;
		for (;;) {
			next = TAILQ_NEXT(nn, entries);
			if (NULL == next ||
			    LOWDOWN_NORMAL_TEXT != next->type)
				break;
			nextbuf = &next->rndr_normal_text.text;
			TAILQ_REMOVE(&n->children, next, entries);
			nb->data = xrealloc(nb->data, 
				nb->size + nextbuf->size + 1);
			memcpy(nb->data + nb->size,
				nextbuf->data, nextbuf->size);
			nb->data[nb->size + nextbuf->size] = '\0';
			nb->size += nextbuf->size;
			lowdown_node_free(next);
		}
	}
}

void
lowdown_buf_diff(const struct lowdown_opts *optnew,
	const char *new, size_t newsz,
	const struct lowdown_opts *optold,
	const char *old, size_t oldsz,
	char **res, size_t *rsz)
{
	hbuf	 	 *ob, *spb;
	void 		 *renderer = NULL;
	hdoc 		 *doc;
	enum lowdown_type t;
	struct lowdown_node *nnew, *nold, *ndiff;

	t = NULL == optnew ? LOWDOWN_HTML : optnew->type;

	/*
	 * XXX: for the renderer, the lodwown_opts used is not
	 * significant.
	 * This is an ugly part of the interface.
	 */

	switch (t) {
	case (LOWDOWN_HTML):
		renderer = lowdown_html_new(optnew);
		break;
	case (LOWDOWN_MAN):
	case (LOWDOWN_NROFF):
		renderer = lowdown_nroff_new(optnew);
		break;
	case (LOWDOWN_TREE):
		renderer = lowdown_tree_new();
		break;
	}

	/* Parse the output and free resources. */

	doc = lowdown_doc_new(optnew);
	nnew = lowdown_doc_parse(doc, new, newsz, NULL, NULL);
	lowdown_doc_free(doc);

	doc = lowdown_doc_new(optold);
	nold = lowdown_doc_parse(doc, old, oldsz, NULL, NULL);
	lowdown_doc_free(doc);

	/* Merge adjacent text nodes. */

	lowdown_merge_adjacent_text(nnew);
	lowdown_merge_adjacent_text(nold);

	/* Get the difference tree. */

	ndiff = lowdown_diff(nold, nnew);

	lowdown_node_free(nnew);
	lowdown_node_free(nold);

	ob = hbuf_new(DEF_OUNIT);

	switch (t) {
	case (LOWDOWN_HTML):
		lowdown_html_rndr(ob, renderer, ndiff);
		lowdown_html_free(renderer);
		break;
	case (LOWDOWN_MAN):
	case (LOWDOWN_NROFF):
		lowdown_nroff_rndr(ob, renderer, ndiff);
		lowdown_nroff_free(renderer);
		break;
	case (LOWDOWN_TREE):
		lowdown_tree_rndr(ob, renderer, ndiff);
		lowdown_tree_free(renderer);
		break;
	}

	lowdown_node_free(ndiff);

	/* Reprocess the output as smartypants. */

	if (LOWDOWN_TREE != t &&
	    NULL != optnew && LOWDOWN_SMARTY & optnew->oflags) {
		spb = hbuf_new(DEF_OUNIT);
		if (LOWDOWN_HTML == t)
			lowdown_html_smrt(spb, ob->data, ob->size);
		else
			lowdown_nroff_smrt(spb, ob->data, ob->size);
		*res = spb->data;
		*rsz = spb->size;
		spb->data = NULL;
		hbuf_free(spb);
	} else {
		*res = ob->data;
		*rsz = ob->size;
		ob->data = NULL;
	}

	hbuf_free(ob);
}

/*
 * Documented in lowdown_file(3).
 */
int
lowdown_file(const struct lowdown_opts *opts,
	FILE *fin, char **res, size_t *rsz,
	struct lowdown_meta **m, size_t *msz)
{
	hbuf *ib;

	ib = hbuf_new(DEF_IUNIT);
	assert(NULL != ib);

	if (hbuf_putf(ib, fin)) {
		hbuf_free(ib);
		return(0);
	}

	lowdown_buf(opts, ib->data, ib->size, res, rsz, m, msz);
	hbuf_free(ib);
	return(1);
}

int
lowdown_file_diff(const struct lowdown_opts *optnew, FILE *fnew, 
	const struct lowdown_opts *optold, FILE *fold, 
	char **res, size_t *rsz)
{
	hbuf *src, *dst;

	src = hbuf_new(DEF_IUNIT);
	assert(NULL != src);
	dst = hbuf_new(DEF_IUNIT);
	assert(NULL != dst);

	if (hbuf_putf(dst, fold) || hbuf_putf(src, fnew)) {
		hbuf_free(src);
		hbuf_free(dst);
		return(0);
	}

	lowdown_buf_diff(optnew, src->data, src->size, 
		optold, dst->data, dst->size, res, rsz);
	hbuf_free(src);
	hbuf_free(dst);
	return(1);
}

