/*	$Id$ */
/*
 * Copyright (c) 2017, 2018, 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include "lowdown.h"
#include "extern.h"

/*
 * Starting size for input and output buffers.
 */
#define HBUF_START_BIG 4096

/*
 * Starting size for metadata buffers.
 */
#define HBUF_START_SMALL 128

int
lowdown_buf(const struct lowdown_opts *opts,
	const char *data, size_t datasz,
	char **res, size_t *rsz,
	struct lowdown_metaq *metaq)
{
	struct lowdown_buf	*ob;
	void			*renderer = NULL;
	struct lowdown_doc	*doc;
	size_t			 maxn;
	enum lowdown_type	 t;
	struct lowdown_node	*n;

	/* Parse the markdown into our AST. */

	if ((doc = lowdown_doc_new(opts)) == NULL)
		return 0;

	n = lowdown_doc_parse(doc, &maxn, data, datasz);
	lowdown_doc_free(doc);
	if (n == NULL)
		return 0;
	assert(n->type == LOWDOWN_ROOT);

	/* Now render it. */

	ob = lowdown_buf_new(HBUF_START_BIG);
	t = opts == NULL ? LOWDOWN_HTML : opts->type;

	switch (t) {
	case LOWDOWN_GEMINI:
		renderer = lowdown_gemini_new(opts);
		break;
	case LOWDOWN_HTML:
		renderer = lowdown_html_new(opts);
		break;
	case LOWDOWN_LATEX:
		renderer = lowdown_latex_new(opts);
		break;
	case LOWDOWN_MAN:
	case LOWDOWN_NROFF:
		renderer = lowdown_nroff_new(opts);
		break;
	case LOWDOWN_TERM:
		renderer = lowdown_term_new(opts);
		break;
	case LOWDOWN_TREE:
		renderer = lowdown_tree_new();
		break;
	default:
		break;
	}

	/* Parse the output and free resources. */


	/* Conditionally apply smartypants. */

    	if (opts != NULL && 
	    (opts->oflags & LOWDOWN_SMARTY)) 
		smarty(n, maxn, t);

	/* Render to output. */

	switch (t) {
	case LOWDOWN_GEMINI:
		lowdown_gemini_rndr(ob, metaq, renderer, n);
		lowdown_gemini_free(renderer);
		break;
	case LOWDOWN_HTML:
		lowdown_html_rndr(ob, metaq, renderer, n);
		lowdown_html_free(renderer);
		break;
	case LOWDOWN_LATEX:
		lowdown_latex_rndr(ob, metaq, renderer, n);
		lowdown_latex_free(renderer);
		break;
	case LOWDOWN_MAN:
	case LOWDOWN_NROFF:
		lowdown_nroff_rndr(ob, metaq, renderer, n);
		lowdown_nroff_free(renderer);
		break;
	case LOWDOWN_TERM:
		lowdown_term_rndr(ob, metaq, renderer, n);
		lowdown_term_free(renderer);
		break;
	case LOWDOWN_TREE:
		lowdown_tree_rndr(ob, metaq, renderer, n);
		lowdown_tree_free(renderer);
		break;
	default:
		break;
	}

	lowdown_node_free(n);

	*res = ob->data;
	*rsz = ob->size;
	ob->data = NULL;
	lowdown_buf_free(ob);
	return 1;
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
	struct lowdown_node 	*nn, *next;
	struct lowdown_buf	*nb, *nextbuf;

	TAILQ_FOREACH(nn, &n->children, entries) {
		if (nn->type != LOWDOWN_NORMAL_TEXT) {
			lowdown_merge_adjacent_text(nn);
			continue;
		}
		nb = &nn->rndr_normal_text.text;
		for (;;) {
			next = TAILQ_NEXT(nn, entries);
			if (next  == NULL ||
			    next->type != LOWDOWN_NORMAL_TEXT)
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
lowdown_buf_diff(const struct lowdown_opts *opts,
	const char *new, size_t newsz,
	const char *old, size_t oldsz,
	char **res, size_t *rsz,
	struct lowdown_metaq *metaq)
{
	struct lowdown_buf 	*ob;
	void 		 	*renderer = NULL;
	struct lowdown_doc 	*doc;
	enum lowdown_type 	 t;
	struct lowdown_node 	*nnew, *nold, *ndiff;
	size_t			 maxnew, maxold, maxn;

	t = opts == NULL ? LOWDOWN_HTML : opts->type;

	switch (t) {
	case LOWDOWN_GEMINI:
		renderer = lowdown_gemini_new(opts);
		break;
	case LOWDOWN_HTML:
		renderer = lowdown_html_new(opts);
		break;
	case LOWDOWN_LATEX:
		renderer = lowdown_latex_new(opts);
		break;
	case LOWDOWN_MAN:
	case LOWDOWN_NROFF:
		renderer = lowdown_nroff_new(opts);
		break;
	case LOWDOWN_TERM:
		renderer = lowdown_term_new(opts);
		break;
	case LOWDOWN_TREE:
		renderer = lowdown_tree_new();
		break;
	default:
		break;
	}

	/* Parse the output and free resources. */

	doc = lowdown_doc_new(opts);
	nnew = lowdown_doc_parse(doc, &maxnew, new, newsz);
	lowdown_doc_free(doc);

	doc = lowdown_doc_new(opts);
	nold = lowdown_doc_parse(doc, &maxold, old, oldsz);
	lowdown_doc_free(doc);

	/* Merge adjacent text nodes. */

	lowdown_merge_adjacent_text(nnew);
	lowdown_merge_adjacent_text(nold);

	/* Get the difference tree and clear the old. */

	ndiff = lowdown_diff(nold, nnew, &maxn);
	lowdown_node_free(nnew);
	lowdown_node_free(nold);

    	if (opts != NULL && 
	    (opts->oflags & LOWDOWN_SMARTY)) 
		smarty(ndiff, maxn, t);

	ob = lowdown_buf_new(HBUF_START_BIG);

	switch (t) {
	case LOWDOWN_GEMINI:
		lowdown_gemini_rndr(ob, metaq, renderer, ndiff);
		lowdown_gemini_free(renderer);
		break;
	case LOWDOWN_HTML:
		lowdown_html_rndr(ob, metaq, renderer, ndiff);
		lowdown_html_free(renderer);
		break;
	case LOWDOWN_LATEX:
		lowdown_latex_rndr(ob, metaq, renderer, ndiff);
		lowdown_latex_free(renderer);
		break;
	case LOWDOWN_MAN:
	case LOWDOWN_NROFF:
		lowdown_nroff_rndr(ob, metaq, renderer, ndiff);
		lowdown_nroff_free(renderer);
		break;
	case LOWDOWN_TERM:
		lowdown_term_rndr(ob, metaq, renderer, ndiff);
		lowdown_term_free(renderer);
		break;
	case LOWDOWN_TREE:
		lowdown_tree_rndr(ob, metaq, renderer, ndiff);
		lowdown_tree_free(renderer);
		break;
	default:
		break;
	}

	lowdown_node_free(ndiff);

	*res = ob->data;
	*rsz = ob->size;
	ob->data = NULL;
	lowdown_buf_free(ob);
}

int
lowdown_file(const struct lowdown_opts *opts, FILE *fin,
	char **res, size_t *rsz, struct lowdown_metaq *metaq)
{
	struct lowdown_buf	*ib;
	int	 		 rc = 0;

	ib = lowdown_buf_new(HBUF_START_BIG);

	hbuf_putf(ib, fin);
	if (!lowdown_buf(opts, ib->data, ib->size, res, rsz, metaq))
		goto out;
	rc = 1;
out:
	lowdown_buf_free(ib);
	return rc;
}

int
lowdown_file_diff(const struct lowdown_opts *opts,
	FILE *fnew, FILE *fold, char **res, size_t *rsz, 
	struct lowdown_metaq *metaq)
{
	struct lowdown_buf	*src, *dst;
	int	 		 rc = 0;

	src = lowdown_buf_new(HBUF_START_BIG);
	dst = lowdown_buf_new(HBUF_START_BIG);

	if (hbuf_putf(dst, fold) || hbuf_putf(src, fnew))
		goto out;
	lowdown_buf_diff(opts, src->data, src->size, 
		dst->data, dst->size, res, rsz, metaq);
	rc = 1;
out:
	lowdown_buf_free(src);
	lowdown_buf_free(dst);
	return rc;
}

