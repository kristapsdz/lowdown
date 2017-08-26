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

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

static	const char *const names[LOWDOWN__MAX] = {
	"LOWDOWN_ROOT",			/* LOWDOWN_ROOT */
	"LOWDOWN_BLOCKCODE",            /* LOWDOWN_BLOCKCODE */
	"LOWDOWN_BLOCKQUOTE",           /* LOWDOWN_BLOCKQUOTE */
	"LOWDOWN_HEADER",               /* LOWDOWN_HEADER */
	"LOWDOWN_HRULE",                /* LOWDOWN_HRULE */
	"LOWDOWN_LIST",                 /* LOWDOWN_LIST */
	"LOWDOWN_LISTITEM",             /* LOWDOWN_LISTITEM */
	"LOWDOWN_PARAGRAPH",            /* LOWDOWN_PARAGRAPH */
	"LOWDOWN_TABLE_BLOCK",          /* LOWDOWN_TABLE_BLOCK */
	"LOWDOWN_TABLE_HEADER",         /* LOWDOWN_TABLE_HEADER */
	"LOWDOWN_TABLE_BODY",           /* LOWDOWN_TABLE_BODY */
	"LOWDOWN_TABLE_ROW",            /* LOWDOWN_TABLE_ROW */
	"LOWDOWN_TABLE_CELL",           /* LOWDOWN_TABLE_CELL */
	"LOWDOWN_FOOTNOTES_BLOCK",      /* LOWDOWN_FOOTNOTES_BLOCK */
	"LOWDOWN_FOOTNOTE_DEF",         /* LOWDOWN_FOOTNOTE_DEF */
	"LOWDOWN_BLOCKHTML",            /* LOWDOWN_BLOCKHTML */
	"LOWDOWN_LINK_AUTO",            /* LOWDOWN_LINK_AUTO */
	"LOWDOWN_CODESPAN",             /* LOWDOWN_CODESPAN */
	"LOWDOWN_DOUBLE_EMPHASIS",      /* LOWDOWN_DOUBLE_EMPHASIS */
	"LOWDOWN_EMPHASIS",             /* LOWDOWN_EMPHASIS */
	"LOWDOWN_HIGHLIGHT",            /* LOWDOWN_HIGHLIGHT */
	"LOWDOWN_IMAGE",                /* LOWDOWN_IMAGE */
	"LOWDOWN_LINEBREAK",            /* LOWDOWN_LINEBREAK */
	"LOWDOWN_LINK",                 /* LOWDOWN_LINK */
	"LOWDOWN_TRIPLE_EMPHASIS",      /* LOWDOWN_TRIPLE_EMPHASIS */
	"LOWDOWN_STRIKETHROUGH",        /* LOWDOWN_STRIKETHROUGH */
	"LOWDOWN_SUPERSCRIPT",          /* LOWDOWN_SUPERSCRIPT */
	"LOWDOWN_FOOTNOTE_REF",         /* LOWDOWN_FOOTNOTE_REF */
	"LOWDOWN_MATH_BLOCK",           /* LOWDOWN_MATH_BLOCK */
	"LOWDOWN_RAW_HTML",             /* LOWDOWN_RAW_HTML */
	"LOWDOWN_ENTITY",               /* LOWDOWN_ENTITY */
	"LOWDOWN_NORMAL_TEXT",          /* LOWDOWN_NORMAL_TEXT */
	"LOWDOWN_BACKSPACE",            /* LOWDOWN_BACKSPACE */
	"LOWDOWN_DOC_HEADER",           /* LOWDOWN_DOC_HEADER */
	"LOWDOWN_DOC_FOOTER",           /* LOWDOWN_DOC_FOOTER */
};

static void
rndr(hbuf *ob, const struct lowdown_node *root, size_t indent)
{
	const struct lowdown_node *n;
	hbuf	*tmp;
	size_t	 i, j;

	for (i = 0; i < indent; i++)
		HBUF_PUTSL(ob, "  ");
	hbuf_puts(ob, names[root->type]);
	HBUF_PUTSL(ob, "\n");

	switch (root->type) {
	case (LOWDOWN_DOC_HEADER):
		for (i = 0; i < root->rndr_doc_header.msz; i++) {
			for (j = 0; j < indent + 1; j++)
				HBUF_PUTSL(ob, "  ");
			hbuf_printf(ob, "metadata: %s, %zu Bytes\n",
				root->rndr_doc_header.m[i].key,
				strlen(root->rndr_doc_header.m[i].value));
		}
		break;
	case (LOWDOWN_NORMAL_TEXT):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "data: %zu Bytes\n",
			root->rndr_normal_text.text.size);
	default:
		break;
	}

	tmp = hbuf_new(64);
	TAILQ_FOREACH(n, &root->children, entries)
		rndr(tmp, n, indent + 1);
	hbuf_put(ob, tmp->data, tmp->size);
	hbuf_free(tmp);
}

void
lowdown_tree_rndr(hbuf *ob, void *ref, const struct lowdown_node *root)
{

	assert(NULL == ref);
	rndr(ob, root, 0);
}

void *
hrend_tree_new(void)
{

	return(NULL);
}

void
hrend_tree_free(void *renderer)
{
	/* Do nothing. */
}
