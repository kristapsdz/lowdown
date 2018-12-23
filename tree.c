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
	"LOWDOWN_DOC_HEADER",           /* LOWDOWN_DOC_HEADER */
	"LOWDOWN_DOC_FOOTER",           /* LOWDOWN_DOC_FOOTER */
};

static void
rndr_short(hbuf *ob, const hbuf *b)
{
	size_t	 i;

	for (i = 0; i < 10 && i < b->size; i++)
		if ('\n' == b->data[i])
			HBUF_PUTSL(ob, "\\n");
		else if ('\t' == b->data[i])
			HBUF_PUTSL(ob, "\\t");
		else
			hbuf_putc(ob, b->data[i]);

	if (b->size >= 10)
		HBUF_PUTSL(ob, "...");
}

static void
rndr(hbuf *ob, const struct lowdown_node *root, size_t indent)
{
	const struct lowdown_node *n;
	hbuf	*tmp;
	size_t	 i, j;

	for (i = 0; i < indent; i++)
		HBUF_PUTSL(ob, "  ");
	if (LOWDOWN_CHNG_INSERT == root->chng)
		HBUF_PUTSL(ob, "INSERT: ");
	else if (LOWDOWN_CHNG_DELETE == root->chng)
		HBUF_PUTSL(ob, "DELETE: ");
	hbuf_puts(ob, names[root->type]);
	HBUF_PUTSL(ob, "\n");

	switch (root->type) {
	case (LOWDOWN_HEADER):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "level: %zu\n",
			root->rndr_header.level);
		break;
	case (LOWDOWN_FOOTNOTE_REF):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "number: %zu\n",
			root->rndr_footnote_ref.num);
		break;
	case (LOWDOWN_FOOTNOTE_DEF):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "number: %zu\n",
			root->rndr_footnote_def.num);
		break;
	case (LOWDOWN_BLOCKCODE):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "data: %zu Bytes: ",
			root->rndr_blockcode.text.size);
		rndr_short(ob, &root->rndr_blockcode.text);
		HBUF_PUTSL(ob, "\n");
		break;
	case (LOWDOWN_LISTITEM):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "item scope: %s\n",
			HLIST_FL_BLOCK & root->rndr_listitem.flags ? 
			"block" : "span");
		break;
	case (LOWDOWN_LIST):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "list type: %s\n",
			HLIST_FL_ORDERED & root->rndr_list.flags ? 
			"ordered" : "unordered");
		break;
	case (LOWDOWN_DOC_HEADER):
		for (i = 0; i < root->rndr_doc_header.msz; i++) {
			for (j = 0; j < indent + 1; j++)
				HBUF_PUTSL(ob, "  ");
			hbuf_printf(ob, "metadata: %zu Bytes: %s\n",
				strlen(root->rndr_doc_header.m[i].value),
				root->rndr_doc_header.m[i].key);
		}
		break;
	case (LOWDOWN_MATH_BLOCK):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "displaymode: %s\n",
			root->rndr_math.displaymode ?
			"block" : "inline");
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "data: %zu Bytes: ",
			root->rndr_math.text.size);
		rndr_short(ob, &root->rndr_math.text);
		HBUF_PUTSL(ob, "\n");
		break;
	case (LOWDOWN_NORMAL_TEXT):
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "data: %zu Bytes: ",
			root->rndr_normal_text.text.size);
		rndr_short(ob, &root->rndr_normal_text.text);
		HBUF_PUTSL(ob, "\n");
		break;
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
lowdown_tree_rndr(hbuf *ob, void *ref, struct lowdown_node *root)
{

	assert(NULL == ref);
	rndr(ob, root, 0);
}

void *
lowdown_tree_new(void)
{

	return(NULL);
}

void
lowdown_tree_free(void *renderer)
{
	/* Do nothing. */
}
