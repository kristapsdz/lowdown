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
#include <stdio.h>
#include <stdlib.h>

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
	size_t	 i;

	for (i = 0; i < indent; i++)
		HBUF_PUTSL(ob, "  ");
	hbuf_puts(ob, names[root->type]);
	HBUF_PUTSL(ob, "\n");

	tmp = hbuf_new(64);

	TAILQ_FOREACH(n, &root->children, entries)
		rndr(tmp, n, indent + 1);
#if 0
	switch (root->type) {
	case (LOWDOWN_BLOCKCODE):
		rndr_blockcode(ob, 
			&root->rndr_blockcode.text, 
			&root->rndr_blockcode.lang, 
			ref->opaque);
		break;
	case (LOWDOWN_BLOCKQUOTE):
		rndr_blockquote(ob, tmp, ref->opaque);
		break;
	case (LOWDOWN_HEADER):
		rndr_header(ob, tmp, 
			root->rndr_header.level,
			ref->opaque);
		break;
	case (LOWDOWN_HRULE):
		rndr_hrule(ob, ref->opaque);
		break;
	case (LOWDOWN_LIST):
		rndr_list(ob, tmp, 
			root->rndr_list.flags, 
			ref->opaque);
		break;
	case (LOWDOWN_LISTITEM):
		rndr_listitem(ob, tmp, 
			root->rndr_listitem.flags,
			ref->opaque, root->rndr_listitem.num);
		break;
	case (LOWDOWN_PARAGRAPH):
		rndr_paragraph(ob, tmp, ref->opaque, nln);
		break;
	case (LOWDOWN_TABLE_BLOCK):
		rndr_table(ob, tmp, ref->opaque);
		break;
	case (LOWDOWN_TABLE_HEADER):
		rndr_table_header(ob, tmp, ref->opaque,
			root->rndr_table_header.flags,
			root->rndr_table_header.columns);
		break;
	case (LOWDOWN_TABLE_BODY):
		rndr_table_body(ob, tmp, ref->opaque);
		break;
	case (LOWDOWN_TABLE_ROW):
		rndr_tablerow(ob, tmp, ref->opaque);
		break;
	case (LOWDOWN_TABLE_CELL):
		rndr_tablecell(ob, tmp, 
			root->rndr_table_cell.flags,
			ref->opaque,
			root->rndr_table_cell.col,
			root->rndr_table_cell.columns);
		break;
	case (LOWDOWN_FOOTNOTES_BLOCK):
		rndr_footnotes(ob, tmp, ref->opaque);
		break;
	case (LOWDOWN_FOOTNOTE_DEF):
		rndr_footnote_def(ob, tmp, 
			root->rndr_footnote_def.num,
			ref->opaque);
		break;
	case (LOWDOWN_BLOCKHTML):
		rndr_raw_block(ob, tmp, ref->opaque);
		break;
	case (LOWDOWN_LINK_AUTO):
		rndr_autolink(ob, 
			&root->rndr_autolink.link,
			root->rndr_autolink.type,
			ref->opaque, nln);
		break;
	case (LOWDOWN_CODESPAN):
		rndr_codespan(ob, 
			&root->rndr_codespan.text, 
			ref->opaque, nln);
		break;
	case (LOWDOWN_DOUBLE_EMPHASIS):
		rndr_double_emphasis(ob, tmp, ref->opaque, nln);
		break;
	case (LOWDOWN_EMPHASIS):
		rndr_emphasis(ob, tmp, ref->opaque, nln);
		break;
	case (LOWDOWN_HIGHLIGHT):
		rndr_highlight(ob, tmp, ref->opaque, nln);
		break;
	case (LOWDOWN_IMAGE):
		rndr_image(ob, 
			&root->rndr_image.link,
			&root->rndr_image.title,
			&root->rndr_image.dims,
			&root->rndr_image.alt,
			ref->opaque);
		break;
	case (LOWDOWN_LINEBREAK):
		rndr_linebreak(ob, ref->opaque);
		break;
	case (LOWDOWN_LINK):
		rndr_link(ob, tmp,
			&root->rndr_link.link,
			&root->rndr_link.title,
			ref->opaque, nln);
		break;
	case (LOWDOWN_TRIPLE_EMPHASIS):
		rndr_triple_emphasis(ob, tmp, ref->opaque, nln);
		break;
	case (LOWDOWN_STRIKETHROUGH):
		rndr_strikethrough(ob, tmp, ref->opaque, nln);
		break;
	case (LOWDOWN_SUPERSCRIPT):
		rndr_superscript(ob, tmp, ref->opaque, nln);
		break;
	case (LOWDOWN_FOOTNOTE_REF):
		rndr_footnote_ref(ob, 
			root->rndr_footnote_ref.num, 
			ref->opaque);
		break;
	case (LOWDOWN_MATH_BLOCK):
		rndr_math(ob, tmp, 
			root->rndr_math.displaymode,
			ref->opaque);
		break;
	case (LOWDOWN_RAW_HTML):
		rndr_raw_html(ob, tmp, ref->opaque);
		break;
	case (LOWDOWN_NORMAL_TEXT):
		rndr_normal_text(ob, 
			&root->rndr_normal_text.text, 
			ref->opaque, nln);
		break;
	case (LOWDOWN_ENTITY):
		hbuf_put(ob,
			root->rndr_entity.text.data,
			root->rndr_entity.text.size);
		break;
	default:
		hbuf_put(ob, tmp->data, tmp->size);
		break;
	}
#endif
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
