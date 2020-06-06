/*	$Id$ */
/*
 * Copyright (c) 2017, 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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
	"LOWDOWN_DEFINITION",		/* LOWDOWN_DEFINITION */
	"LOWDOWN_DEFINITION_TITLE",	/* LOWDOWN_DEFINITION_TITLE */
	"LOWDOWN_DEFINITION_DATA",	/* LOWDOWN_DEFINITION_DATA */
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
	"LOWDOWN_META",			/* LOWDOWN_META */
	"LOWDOWN_DOC_FOOTER",           /* LOWDOWN_DOC_FOOTER */
};

static void
rndr_short(hbuf *ob, const hbuf *b)
{
	size_t	 i;

	for (i = 0; i < 20 && i < b->size; i++)
		if (b->data[i] == '\n')
			HBUF_PUTSL(ob, "\\n");
		else if (b->data[i] == '\t')
			HBUF_PUTSL(ob, "\\t");
		else if (iscntrl((unsigned char)b->data[i]))
			hbuf_putc(ob, '?');
		else
			hbuf_putc(ob, b->data[i]);

	if (i < b->size)
		HBUF_PUTSL(ob, "...");
}

static void
rndr(hbuf *ob, const struct lowdown_node *root, size_t indent)
{
	const struct lowdown_node	*n;
	hbuf				*tmp;
	size_t	 			 i, j;

	for (i = 0; i < indent; i++)
		HBUF_PUTSL(ob, "  ");
	if (root->chng == LOWDOWN_CHNG_INSERT)
		HBUF_PUTSL(ob, "INSERT: ");
	else if (root->chng == LOWDOWN_CHNG_DELETE)
		HBUF_PUTSL(ob, "DELETE: ");
	hbuf_puts(ob, names[root->type]);
	HBUF_PUTSL(ob, "\n");

	switch (root->type) {
	case LOWDOWN_PARAGRAPH:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "lines: %zu, blank-after: %d\n", 
			root->rndr_paragraph.lines,
			root->rndr_paragraph.beoln);
		break;
	case LOWDOWN_IMAGE:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "source: ");
		rndr_short(ob, &root->rndr_image.link);
		if (root->rndr_image.dims.size) {
			HBUF_PUTSL(ob, "(");
			rndr_short(ob, &root->rndr_image.dims);
			HBUF_PUTSL(ob, ")");
		}
		HBUF_PUTSL(ob, "\n");
		if (root->rndr_image.title.size) {
			for (i = 0; i < indent + 1; i++)
				HBUF_PUTSL(ob, "  ");
			hbuf_printf(ob, "title: ");
			rndr_short(ob, &root->rndr_image.title);
			HBUF_PUTSL(ob, "\n");
		}
		break;
	case LOWDOWN_HEADER:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "level: %zu\n",
			root->rndr_header.level);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "number: %zu\n",
			root->rndr_footnote_ref.num);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "number: %zu\n",
			root->rndr_footnote_def.num);
		break;
	case LOWDOWN_RAW_HTML:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "data: %zu Bytes: ",
			root->rndr_raw_html.text.size);
		rndr_short(ob, &root->rndr_raw_html.text);
		HBUF_PUTSL(ob, "\n");
		break;
	case LOWDOWN_BLOCKHTML:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "data: %zu Bytes: ",
			root->rndr_blockhtml.text.size);
		rndr_short(ob, &root->rndr_blockhtml.text);
		HBUF_PUTSL(ob, "\n");
		break;
	case LOWDOWN_BLOCKCODE:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "data: %zu Bytes: ",
			root->rndr_blockcode.text.size);
		rndr_short(ob, &root->rndr_blockcode.text);
		HBUF_PUTSL(ob, "\n");
		break;
	case LOWDOWN_DEFINITION:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "iem scope: %s\n",
			HLIST_FL_BLOCK & root->rndr_definition.flags ? 
			"block" : "span");
		break;
	case LOWDOWN_LISTITEM:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "item scope: %s\n",
			HLIST_FL_BLOCK & root->rndr_listitem.flags ? 
			"block" : "span");
		break;
	case LOWDOWN_LIST:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "list type: %s\n",
			HLIST_FL_ORDERED & root->rndr_list.flags ? 
			"ordered" : "unordered");
		break;
	case LOWDOWN_META:
		for (j = 0; j < indent + 1; j++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "key: ");
		rndr_short(ob, &root->rndr_meta.key);
		HBUF_PUTSL(ob, "\n");
		break;
	case LOWDOWN_MATH_BLOCK:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "blockmode: %s\n",
			root->rndr_math.blockmode ?
			"block" : "inline");
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "data: %zu Bytes: ",
			root->rndr_math.text.size);
		rndr_short(ob, &root->rndr_math.text);
		HBUF_PUTSL(ob, "\n");
		break;
	case LOWDOWN_ENTITY:
		for (i = 0; i < indent + 1; i++)
			HBUF_PUTSL(ob, "  ");
		hbuf_printf(ob, "value: ");
		rndr_short(ob, &root->rndr_entity.text);
		HBUF_PUTSL(ob, "\n");
		break;
	case LOWDOWN_LINK:
		if (root->rndr_link.title.size) {
			for (i = 0; i < indent + 1; i++)
				HBUF_PUTSL(ob, "  ");
			HBUF_PUTSL(ob, "title: ");
			rndr_short(ob, &root->rndr_link.title);
			HBUF_PUTSL(ob, "\n");
		}
		break;
	case LOWDOWN_NORMAL_TEXT:
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
	hbuf_putb(ob, tmp);
	hbuf_free(tmp);
}

void
lowdown_tree_rndr(hbuf *ob, struct lowdown_metaq *metaq,
	void *ref, const struct lowdown_node *root)
{

	assert(ref == NULL);
	rndr(ob, root, 0);
}

void *
lowdown_tree_new(void)
{

	return NULL;
}

void
lowdown_tree_free(void *arg)
{
}
