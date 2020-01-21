/*	$Id$ */
/*
 * Copyright (c) 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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

enum entity {
	ENT_COPY,
	ENT_REG,
	ENT_TMARK,
	ENT_SMARK,
	ENT_ELLIP,
	ENT_MDASH,
	ENT_NDASH,
	ENT_LDQUO,
	ENT_RDQUO,
	ENT_LSQUO,
	ENT_RSQUO,
	ENT_FRAC14,
	ENT_FRAC12,
	ENT_FRAC34,
	ENT__MAX
};

struct sym {
	const char	*key; /* input in markdown */
	enum entity	 ent; /* output entity */
};

static const char *ent_htmls[ENT__MAX] = {
	"&copy;", /* ENT_COPY */
	"&reg;", /* ENT_REG */
	"&trade;", /* ENT_TMARK */
	"&#8480;", /* ENT_SMARK */
	"&hellip;", /* ENT_ELLIP */
	"&mdash;", /* ENT_MDASH */
	"&ndash;", /* ENT_NDASH */
	"&ldquo;", /* ENT_LDQUO */
	"&rdquo;", /* ENT_RDQUO */
	"&lsquo;", /* ENT_LSQUO */
	"&rsquo;", /* ENT_RSQUO */
	"&frac14;", /* ENT_FRAC14 */
	"&frac12;", /* ENT_FRAC12 */
	"&frac34;", /* ENT_FRAC34 */
};

/*
 * Order is important: check the longest subset first.
 * (So basically "---" comes before "--".)
 */
static const struct sym syms[] = {
	{ "(c)",	ENT_COPY },
	{ "(C)",	ENT_COPY },
	{ "(r)",	ENT_REG },
	{ "(R)",	ENT_REG },
	{ "(tm)",	ENT_TMARK },
	{ "(TM)",	ENT_TMARK },
	{ "(sm)",	ENT_SMARK },
	{ "(SM)",	ENT_SMARK },
	{ "...",	ENT_ELLIP },
	{ ". . .",	ENT_ELLIP },
	{ "---",	ENT_MDASH },
	{ "--",		ENT_NDASH },
	{ NULL,		ENT__MAX }
};

/*
 * Symbols that require word-break on both sides.
 * Again, order is important: longest-first.
 */
static const struct sym syms2[] = {
	{ "1/4th",	ENT_FRAC14 },
	{ "1/4",	ENT_FRAC14 },
	{ "3/4ths",	ENT_FRAC34 },
	{ "3/4th",	ENT_FRAC34 },
	{ "3/4",	ENT_FRAC34 },
	{ "1/2",	ENT_FRAC12 },
	{ NULL,		ENT__MAX }
};

struct smarty {
	int	 left_wb; /* left wordbreak */
};

enum type {
	TYPE_ROOT, /* root (LOWDOWN_ROOT) */
	TYPE_BLOCK, /* block-level */
	TYPE_SPAN, /* span-level */
	TYPE_OPAQUE, /* skip */
	TYPE_TEXT /* text (LOWDOWN_NORMAL_TEXT) */
};

static const enum type types[LOWDOWN__MAX] = {
	TYPE_ROOT, /* LOWDOWN_ROOT */
	TYPE_OPAQUE, /* LOWDOWN_BLOCKCODE */
	TYPE_BLOCK, /* LOWDOWN_BLOCKQUOTE */
	TYPE_BLOCK, /* LOWDOWN_HEADER */
	TYPE_BLOCK, /* LOWDOWN_HRULE */
	TYPE_BLOCK, /* LOWDOWN_LIST */
	TYPE_BLOCK, /* LOWDOWN_LISTITEM */
	TYPE_BLOCK, /* LOWDOWN_PARAGRAPH */
	TYPE_BLOCK, /* LOWDOWN_TABLE_BLOCK */
	TYPE_BLOCK, /* LOWDOWN_TABLE_HEADER */
	TYPE_BLOCK, /* LOWDOWN_TABLE_BODY */
	TYPE_BLOCK, /* LOWDOWN_TABLE_ROW */
	TYPE_BLOCK, /* LOWDOWN_TABLE_CELL */
	TYPE_BLOCK, /* LOWDOWN_FOOTNOTES_BLOCK */
	TYPE_BLOCK, /* LOWDOWN_FOOTNOTE_DEF */
	TYPE_OPAQUE, /* LOWDOWN_BLOCKHTML */
	TYPE_OPAQUE, /* LOWDOWN_LINK_AUTO */
	TYPE_OPAQUE, /* LOWDOWN_CODESPAN */
	TYPE_SPAN, /* LOWDOWN_DOUBLE_EMPHASIS */
	TYPE_SPAN, /* LOWDOWN_EMPHASIS */
	TYPE_SPAN, /* LOWDOWN_HIGHLIGHT */
	TYPE_SPAN, /* LOWDOWN_IMAGE */
	TYPE_SPAN, /* LOWDOWN_LINEBREAK */
	TYPE_SPAN, /* LOWDOWN_LINK */
	TYPE_SPAN, /* LOWDOWN_TRIPLE_EMPHASIS */
	TYPE_SPAN, /* LOWDOWN_STRIKETHROUGH */
	TYPE_SPAN, /* LOWDOWN_SUPERSCRIPT */
	TYPE_SPAN, /* LOWDOWN_FOOTNOTE_REF */
	TYPE_OPAQUE, /* LOWDOWN_MATH_BLOCK */
	TYPE_OPAQUE, /* LOWDOWN_RAW_HTML */
	TYPE_OPAQUE, /* LOWDOWN_ENTITY */
	TYPE_TEXT, /* LOWDOWN_NORMAL_TEXT */
	TYPE_BLOCK, /* LOWDOWN_DOC_HEADER */
	TYPE_BLOCK, /* LOWDOWN_DOC_FOOTER */
};

/*
 * Given the sequence in "n" starting at "start" and ending at "end",
 * split "n" around the sequence and replace it with "entity".
 * This behaves properly if the leading or trailing sequence is
 * zero-length.
 * It may modify the subtree rooted at the parent of "n".
 */
static void
smarty_entity(struct lowdown_node *n, size_t *maxn,
	size_t start, size_t end, enum entity entity)
{
	struct lowdown_node	*nn, *nent;

	assert(n->type == LOWDOWN_NORMAL_TEXT);

	/* Allocate the subsequent entity. */

	nent = xcalloc(1, sizeof(struct lowdown_node));
	nent->id = (*maxn)++;
	nent->type = LOWDOWN_ENTITY;
	nent->parent = n->parent;
	TAILQ_INIT(&nent->children);
	nent->rndr_entity.text.data = 
		xstrdup(ent_htmls[entity]);
	nent->rndr_entity.text.size = 
		strlen(ent_htmls[entity]);
	TAILQ_INSERT_AFTER(&n->parent->children, n, nent, entries);

	/* Allocate the remaining bits, if applicable. */

	if (n->rndr_normal_text.text.size - end > 0) {
		nn = xcalloc(1, sizeof(struct lowdown_node));
		nn->id = (*maxn)++;
		nn->type = LOWDOWN_NORMAL_TEXT;
		nn->parent = n->parent;
		TAILQ_INIT(&nn->children);
		nn->rndr_entity.text.data = xstrdup
			(n->rndr_normal_text.text.data + end);
		nn->rndr_entity.text.size = 
			n->rndr_normal_text.text.size - end;
		TAILQ_INSERT_AFTER(&n->parent->children, 
			nent, nn, entries);
	}

	n->rndr_normal_text.text.size = start;
}

static int
smarty_iswb(char c)
{

	return isspace((unsigned char)c) ||
		ispunct((unsigned char)c);
}

static void
buf_append(char **buf, size_t *sz, size_t *max, const char *val)
{
	size_t	 valsz;

	valsz = strlen(val);
	if (*sz + valsz + 1 > *max) {
		*max += valsz + 128;
		*buf = xrealloc(*buf, *max);
	}

	strlcat(*buf, val, *max);
}

static void
buf_appendc(char **buf, size_t *sz, size_t *max, char val)
{

	if (*sz + 2 > *max) {
		*max += 128;
		*buf = xrealloc(*buf, *max);
	}
	(*buf)[(*sz)++] = val;
	(*buf)[(*sz)++] = '\0';
}

static char *
smarty_buf(const char *buf, enum lowdown_type type)
{
	char	*out = NULL;
	size_t	 outsz = 0, outmax = 0, i, j, bufsz, sz;
	int	 left_wb = 1;

	bufsz = strlen(buf);

	for (i = 0; i < bufsz; i++) {
		switch (buf[i]) {
		case '.':
		case '(':
		case '-':
			for (j = 0; syms[j].key != NULL; j++) {
				sz = strlen(syms[j].key);
				if (i + sz - 1 >= bufsz)
					continue;
				if (memcmp(syms[j].key, &buf[i], sz))
					continue;
				buf_append(&out, &outsz, &outmax,
					ent_htmls[syms[j].ent]);
				left_wb = 0;
				i += sz - 1;
				break;
			}
			if (syms[j].key != NULL)
				continue;
			break;
		case '"':
			if (!left_wb) {
				if (i + 1 < bufsz - 1 &&
				    !smarty_iswb(buf[i + 1]))
					break;
				buf_append(&out, &outsz, &outmax,
					ent_htmls[ENT_RDQUO]);
				left_wb = 0;
				continue;
			}
			buf_append(&out, &outsz, &outmax,
				ent_htmls[ENT_LDQUO]);
			left_wb = 0;
			continue;
		case '\'':
			if (!left_wb) {
				if (i + 1 < bufsz - 1 &&
				    !smarty_iswb(buf[i + 1]))
				buf_append(&out, &outsz, &outmax,
					ent_htmls[ENT_RSQUO]);
				left_wb = 0;
				continue;
			}
			buf_append(&out, &outsz, &outmax,
				ent_htmls[ENT_LDQUO]);
			left_wb = 0;
			continue;
		case '1':
		case '3':
			if (!left_wb)
				break;
			for (j = 0; syms2[j].key != NULL; j++) {
				sz = strlen(syms2[j].key);
				if (i + sz - 1 >= bufsz)
					continue;
				if (memcmp(syms2[j].key, &buf[i], sz))
					continue;
				if (i + sz < bufsz - 1 &&
				    !smarty_iswb(buf[i + sz]))
				buf_append(&out, &outsz, &outmax,
					ent_htmls[syms2[j].ent]);
				left_wb = 0;
				break;
			}
			if (syms2[j].key != NULL)
				continue;
			break;
		default:
			break;
		}

		buf_appendc(&out, &outsz, &outmax, buf[i]);
		left_wb = smarty_iswb(buf[i]);
	}

	return out;
}

/*
 * Recursive scan for next white-space.
 * If "skip" is set, we're on the starting node and shouldn't do a check
 * for white-space in ourselves.
 */
static int
smarty_right_wb_r(const struct lowdown_node *n, int skip)
{
	const hbuf			*b;
	const struct lowdown_node	*nn;

	/* Check type of node. */

	if (types[n->type] == TYPE_BLOCK)
		return 1;
	if (types[n->type] == TYPE_OPAQUE)
		return 0;

	if (!skip &&
	    types[n->type] == TYPE_TEXT &&
	    n->rndr_normal_text.text.size) {
		assert(n->type == LOWDOWN_NORMAL_TEXT);
		b = &n->rndr_normal_text.text;
		return smarty_iswb(b->data[0]);
	}

	/* First scan down. */

	if ((nn = TAILQ_FIRST(&n->children)) != NULL)
		return smarty_right_wb_r(nn, 0);

	/* Now scan back up. */

	do {
		if ((nn = TAILQ_NEXT(n, entries)) != NULL)
			return smarty_right_wb_r(nn, 0);
	} while ((n = n->parent) != NULL);

	return 1;
}

static int
smarty_right_wb(const struct lowdown_node *n, size_t pos)
{
	const hbuf	*b;

	assert(n->type == LOWDOWN_NORMAL_TEXT);
	b = &n->rndr_normal_text.text;

	if (pos + 1 < b->size)
		return smarty_iswb(b->data[pos]);

	return smarty_right_wb_r(n, 1);
}

/*
 * FIXME: this can be faster with a table-based lookup instead of the
 * switch statement.
 */
static void
smarty_hbuf(struct lowdown_node *n, size_t *maxn, hbuf *b, struct smarty *s)
{
	size_t	 i = 0, j, sz;

	assert(n->type == LOWDOWN_NORMAL_TEXT);

	for (i = 0; i < b->size; i++) {
		switch (b->data[i]) {
		case '.':
		case '(':
		case '-':
			/* Symbols that don't need wordbreak. */

			for (j = 0; syms[j].key != NULL; j++) {
				sz = strlen(syms[j].key);
				if (i + sz - 1 >= b->size)
					continue;
				if (memcmp(syms[j].key, 
				    &b->data[i], sz))
					continue;
				smarty_entity(n, maxn, i, 
					i + sz, syms[j].ent);
				return;
			}
			break;
		case '"':
			/* Left-wb and right-wb differ. */

			if (!s->left_wb) {
				if (!smarty_right_wb(n, i + 1)) 
					break;
				smarty_entity(n, maxn, 
					i, i + 1, ENT_RDQUO);
				return;
			}
			smarty_entity(n, maxn, i, i + 1, ENT_LDQUO);
			return;
		case '\'':
			/* Left-wb and right-wb differ. */

			if (!s->left_wb) {
				if (!smarty_right_wb(n, i + 1))
					break;
				smarty_entity(n, maxn, 
					i, i + 1, ENT_RSQUO);
				return;
			}
			smarty_entity(n, maxn, i, i + 1, ENT_LSQUO);
			return;
		case '1':
		case '3':
			/* Symbols that require wb. */

			if (!s->left_wb)
				break;
			for (j = 0; syms2[j].key != NULL; j++) {
				sz = strlen(syms2[j].key);
				if (i + sz - 1 >= b->size)
					continue;
				if (memcmp(syms2[j].key, 
				    &b->data[i], sz))
					continue;
				if (!smarty_right_wb(n, i + sz)) 
					continue;
				smarty_entity(n, maxn, i, 
					i + sz, syms[j].ent);
				return;
			}
			break;
		default:
			break;
		}

		s->left_wb = smarty_iswb(b->data[i]);
	}
}

static void
smarty_span(struct lowdown_node *root, size_t *maxn, struct smarty *s)
{
	struct lowdown_node	*n;

	TAILQ_FOREACH(n, &root->children, entries)  {
		switch (types[n->type]) {
		case TYPE_TEXT:
			assert(n->type == LOWDOWN_NORMAL_TEXT);
			smarty_hbuf(n, maxn, 
				&n->rndr_normal_text.text, s);
			break;
		case TYPE_SPAN:
			smarty_span(n, maxn, s);
			break;
		case TYPE_OPAQUE:
			s->left_wb = 0;
			break;
		case TYPE_ROOT:
		case TYPE_BLOCK:
			abort();
			break;
		}
	}
}

static void
smarty_block(struct lowdown_node *root,
	size_t *maxn, enum lowdown_type type)
{
	struct smarty		 s;
	struct lowdown_node	*n;
	size_t			 i;
	char			*v;

	if (root->type == LOWDOWN_DOC_HEADER)
		for (i = 0; i < root->rndr_doc_header.msz; i++) {
			v = smarty_buf
				(root->rndr_doc_header.m[i].value,
				 type);
			free(root->rndr_doc_header.m[i].value);
			root->rndr_doc_header.m[i].value = v;
		}

	s.left_wb = 1;

	TAILQ_FOREACH(n, &root->children, entries) {
		switch (types[n->type]) {
		case TYPE_ROOT:
		case TYPE_BLOCK:
			s.left_wb = 1;
			smarty_block(n, maxn, type);
			break;
		case TYPE_TEXT:
			assert(n->type == LOWDOWN_NORMAL_TEXT);
			smarty_hbuf(n, maxn, 
				&n->rndr_normal_text.text, &s);
			break;
		case TYPE_SPAN:
			smarty_span(n, maxn, &s);
			break;
		case TYPE_OPAQUE:
			s.left_wb = 0;
			break;
		default:
			break;
		}
	}

	s.left_wb = 1;
}

void
smarty(struct lowdown_node *n,
	size_t maxn, enum lowdown_type type)
{

	if (n == NULL)
		return;
	assert(types[n->type] == TYPE_ROOT);
	smarty_block(n, &maxn, type);
}
