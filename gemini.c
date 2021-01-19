/*	$Id$ */
/*
 * Copyright (c) 2020--2021 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <time.h>

#include "lowdown.h"
#include "extern.h"

/*
 * A standalone link is one that lives in its own paragraph.
 */
#define	IS_STANDALONE_LINK(_n, _prev) \
	((_n)->parent != NULL && \
	 (_n)->parent->type == LOWDOWN_PARAGRAPH && \
	 (_n)->parent->parent != NULL && \
	 (_n)->parent->parent->type == LOWDOWN_ROOT && \
	 (_prev) == NULL && \
	 TAILQ_NEXT((_n), entries) == NULL)

/*
 * A link queued for display.
 * This only happens when using footnote or endnote links.
 */
struct link {
	const struct lowdown_node	*n; /* node needing link */
	size_t				 id; /* link-%zu */
	TAILQ_ENTRY(link)		 entries;
};

TAILQ_HEAD(linkq, link);

struct gemini {
	unsigned int		 flags; /* output flags */
	ssize_t			 last_blank; /* line breaks or -1 (start) */
	struct lowdown_buf	*tmp; /* for temporary allocations */
	struct linkq		 linkq; /* link queue */
	size_t			 linkqsz; /* position in link queue */
};

/*
 * Forward declaration.
 */
static int
rndr(struct lowdown_buf *, struct lowdown_metaq *, 
	struct gemini *, const struct lowdown_node *);

/*
 * Convert newlines to spaces, elide control characters.
 * If a newline follows a period, it's converted to two spaces.
 */
static void
rndr_escape(struct lowdown_buf *out, const char *buf, size_t sz)
{
	size_t	 i, start = 0;

	for (i = 0; i < sz; i++) {
		if (buf[i] == '\n') {
			hbuf_put(out, buf + start, i - start);
			if (out->size && out->data[out->size - 1] == '.')
				hbuf_putc(out, ' ');
			hbuf_putc(out, ' ');
			start = i + 1;
		} else if (iscntrl((unsigned char)buf[i])) {
			hbuf_put(out, buf + start, i - start);
			start = i + 1;
		}
	}

	if (start < sz) 
		hbuf_put(out, buf + start, sz - start);
}

/*
 * Output optional number of newlines before or after content.
 */
static void
rndr_buf_vspace(struct gemini *gemini, struct lowdown_buf *out,
	const struct lowdown_node *n, size_t sz)
{

	if (gemini->last_blank < 0)
		return;
	while ((size_t)gemini->last_blank < sz) {
		HBUF_PUTSL(out, "\n");
		gemini->last_blank++;
	}
}

/*
 * Emit text in "in" the current line with output "out".
 */
static void
rndr_buf(struct gemini *gemini, struct lowdown_buf *out, 
	const struct lowdown_node *n, const struct lowdown_buf *in)
{
	const struct lowdown_node	*nn;
	size_t				 i = 0;

	for (nn = n; nn != NULL; nn = nn->parent)
		if (nn->type == LOWDOWN_BLOCKCODE ||
	  	    nn->type == LOWDOWN_BLOCKHTML) {
			hbuf_putb(out, in);
			gemini->last_blank = 1;
			return;
		}

	/* 
	 * If we last printed some space and we're not in literal mode,
	 * suppress any leading blanks.
	 * This is only likely to happen around links.
	 */

	if (gemini->last_blank != 0)
		for ( ; i < in->size; i++)
			if (!isspace((unsigned char)in->data[i]))
				break;

	rndr_escape(out, in->data + i, in->size - i);
	if (in->size && gemini->last_blank != 0)
		gemini->last_blank = 0;
}

/*
 * Output the unicode entry "val", which must be strictly greater than
 * zero, as a UTF-8 sequence.
 * This does no error checking.
 */
static void
rndr_entity(struct lowdown_buf *buf, int32_t val)
{

	assert(val > 0);
	if (val < 0x80) {
		hbuf_putc(buf, val);
		return;
	}
       	if (val < 0x800) {
		hbuf_putc(buf, 192 + val / 64);
		hbuf_putc(buf, 128 + val % 64);
		return;
	}
       	if (val - 0xd800u < 0x800) 
		return;
       	if (val < 0x10000) {
		hbuf_putc(buf, 224 + val / 4096);
		hbuf_putc(buf, 128 + val / 64 % 64);
		hbuf_putc(buf, 128 + val % 64);
		return;
	}
       	if (val < 0x110000) {
		hbuf_putc(buf, 240 + val / 262144);
		hbuf_putc(buf, 128 + val / 4096 % 64);
		hbuf_putc(buf, 128 + val / 64 % 64);
		hbuf_putc(buf, 128 + val % 64);
		return;
	}
}

/*
 * Render the key and value, then store the results in our "mq"
 * conditional to it existing.
 */
static int
rndr_meta(struct gemini *gemini, struct lowdown_buf *out,
	const struct lowdown_node *n, struct lowdown_metaq *mq)
{
	ssize_t				 last_blank;
	struct lowdown_buf		*tmp = NULL;
	struct lowdown_meta		*m;
	const struct lowdown_node	*child;

	rndr_buf(gemini, out, n, &n->rndr_meta.key);
	HBUF_PUTSL(gemini->tmp, ": ");
	rndr_buf(gemini, out, n, gemini->tmp);

	if (mq == NULL)
		return 1;

	/*
	 * Manually render the children of the meta into a
	 * buffer and use that as our value.  Start by zeroing
	 * our terminal position and using another output buffer
	 * (gemini->tmp would be clobbered by children).
	 */

	last_blank = gemini->last_blank;
	gemini->last_blank = -1;

	if ((tmp = hbuf_new(128)) == NULL)
		goto err;
	if ((m = calloc(1, sizeof(struct lowdown_meta))) == NULL)
		goto err;
	TAILQ_INSERT_TAIL(mq, m, entries);

	m->key = strndup(n->rndr_meta.key.data,
		n->rndr_meta.key.size);
	if (m->key == NULL)
		goto err;

	TAILQ_FOREACH(child, &n->children, entries)
		if (!rndr(tmp, mq, gemini, child))
			goto err;

	m->value = strndup(tmp->data, tmp->size);
	if (m->value == NULL)
		goto err;

	hbuf_free(tmp);
	gemini->last_blank = last_blank;
	return 1;
err:
	hbuf_free(tmp);
	return 0;
}

static void
rndr_flush_linkq(struct gemini *gemini, struct lowdown_buf *out)
{
	struct link	*l;

	while ((l = TAILQ_FIRST(&gemini->linkq)) != NULL) {
		TAILQ_REMOVE(&gemini->linkq, l, entries);
		HBUF_PUTSL(out, "=> ");
		if (l->n->type == LOWDOWN_LINK)
			hbuf_putb(out, &l->n->rndr_link.link);
		else if (l->n->type == LOWDOWN_LINK_AUTO)
			hbuf_putb(out, &l->n->rndr_autolink.link);
		else if (l->n->type == LOWDOWN_IMAGE)
			hbuf_putb(out, &l->n->rndr_image.link);
		hbuf_printf(out, " [Reference: link-%zu]\n", l->id);
		gemini->last_blank = 1;
		free(l);
	}
}

static int
rndr(struct lowdown_buf *ob, struct lowdown_metaq *mq,
	struct gemini *p, const struct lowdown_node *n)
{
	const struct lowdown_node	*child, *prev;
	int32_t				 entity;
	size_t				 i;
	struct link			*l;
	
	prev = n->parent == NULL ? NULL :
		TAILQ_PREV(n, lowdown_nodeq, entries);
	
	/* Vertical space before content. */

	switch (n->type) {
	case LOWDOWN_ROOT:
		p->last_blank = -1;
		break;
	case LOWDOWN_BLOCKCODE:
	case LOWDOWN_BLOCKHTML:
	case LOWDOWN_BLOCKQUOTE:
	case LOWDOWN_DEFINITION:
	case LOWDOWN_FOOTNOTES_BLOCK:
	case LOWDOWN_FOOTNOTE_DEF:
	case LOWDOWN_HEADER:
	case LOWDOWN_LIST:
	case LOWDOWN_PARAGRAPH:
	case LOWDOWN_TABLE_BLOCK:
		/*
		 * Blocks in a definition list get special treatment
		 * because we only put one newline between the title and
		 * the data regardless of its contents.
		 */

		if (n->parent != NULL && 
		    n->parent->type == LOWDOWN_LISTITEM &&
		    n->parent->parent != NULL &&
		    n->parent->parent->type == 
		      LOWDOWN_DEFINITION_DATA &&
		    prev == NULL)
			rndr_buf_vspace(p, ob, n, 1);
		else
			rndr_buf_vspace(p, ob, n, 2);
		break;
	case LOWDOWN_MATH_BLOCK:
		if (n->rndr_math.blockmode)
			rndr_buf_vspace(p, ob, n, 1);
		break;
	case LOWDOWN_DEFINITION_DATA:
		/* 
		 * Vertical space if previous block-mode data. 
		 */

		if (n->parent != NULL &&
		    n->parent->type == LOWDOWN_DEFINITION &&
		    (n->parent->rndr_definition.flags &
		     HLIST_FL_BLOCK) &&
		    prev != NULL &&
		    prev->type == LOWDOWN_DEFINITION_DATA)
			rndr_buf_vspace(p, ob, n, 2);
		else
			rndr_buf_vspace(p, ob, n, 1);
		break;
	case LOWDOWN_DEFINITION_TITLE:
	case LOWDOWN_HRULE:
	case LOWDOWN_LINEBREAK:
	case LOWDOWN_LISTITEM:
	case LOWDOWN_META:
	case LOWDOWN_TABLE_ROW:
		rndr_buf_vspace(p, ob, n, 1);
		break;
	case LOWDOWN_IMAGE:
	case LOWDOWN_LINK:
	case LOWDOWN_LINK_AUTO:
		if (p->flags & LOWDOWN_GEMINI_LINK_IN)
			rndr_buf_vspace(p, ob, n, 1);
		break;
	default:
		break;
	}

	/* Output leading content. */

	hbuf_truncate(p->tmp);
	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
	case LOWDOWN_BLOCKHTML:
		HBUF_PUTSL(p->tmp, "```\n");
		rndr_buf(p, ob, n, p->tmp);
		break;
	case LOWDOWN_BLOCKQUOTE:
		HBUF_PUTSL(p->tmp, "> ");
		rndr_buf(p, ob, n, p->tmp);
		p->last_blank = -1;
		break;
	case LOWDOWN_HEADER:
		for (i = 0; i <= n->rndr_header.level; i++)
			HBUF_PUTSL(p->tmp, "#");
		HBUF_PUTSL(p->tmp, " ");
		rndr_buf(p, ob, n, p->tmp);
		p->last_blank = -1;
		break;
	case LOWDOWN_FOOTNOTES_BLOCK:
		HBUF_PUTSL(p->tmp, "~~~~~~~~");
		rndr_buf(p, ob, n, p->tmp);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		hbuf_printf(p->tmp, "[%zu] ", 
			n->rndr_footnote_def.num);
		rndr_buf(p, ob, n, p->tmp);
		p->last_blank = -1;
		break;
	case LOWDOWN_IMAGE:
	case LOWDOWN_LINK:
	case LOWDOWN_LINK_AUTO:
		if (IS_STANDALONE_LINK(n, prev) ||
		    (p->flags & LOWDOWN_GEMINI_LINK_IN)) {
			HBUF_PUTSL(p->tmp, "=> ");
			if (n->type == LOWDOWN_LINK_AUTO)
				hbuf_putb(p->tmp, &n->rndr_autolink.link);
			else if (n->type == LOWDOWN_LINK)
				hbuf_putb(p->tmp, &n->rndr_link.link);
			else if (n->type == LOWDOWN_IMAGE)
				hbuf_putb(p->tmp, &n->rndr_image.link);
			HBUF_PUTSL(p->tmp, " ");
			rndr_buf(p, ob, n, p->tmp);
			p->last_blank = -1;
		}
		break;
	case LOWDOWN_LISTITEM:
		if (n->rndr_listitem.flags & HLIST_FL_ORDERED)
			hbuf_printf(p->tmp, "%zu. ", 
				n->rndr_listitem.num);
		else
			HBUF_PUTSL(p->tmp, "* ");
		rndr_buf(p, ob, n, p->tmp);
		p->last_blank = -1;
		break;
	case LOWDOWN_META:
		if (!rndr_meta(p, ob, n, mq))
			return 0;
		break;
	case LOWDOWN_SUPERSCRIPT:
		HBUF_PUTSL(p->tmp, "^");
		rndr_buf(p, ob, n, p->tmp);
		break;
	default:
		break;
	}

	/* Descend into children. */

	TAILQ_FOREACH(child, &n->children, entries)
		if (!rndr(ob, mq, p, child))
			return 0;

	/* Output non-child or trailing content. */

	hbuf_truncate(p->tmp);
	switch (n->type) {
	case LOWDOWN_HRULE:
		HBUF_PUTSL(p->tmp, "~~~~~~~~");
		rndr_buf(p, ob, n, p->tmp);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		hbuf_printf(p->tmp, "[%zu]", 
			n->rndr_footnote_ref.num);
		rndr_buf(p, ob, n, p->tmp);
		break;
	case LOWDOWN_RAW_HTML:
		rndr_buf(p, ob, n, &n->rndr_raw_html.text);
		break;
	case LOWDOWN_MATH_BLOCK:
		rndr_buf(p, ob, n, &n->rndr_math.text);
		break;
	case LOWDOWN_ENTITY:
		entity = entity_find_iso(&n->rndr_entity.text);
		if (entity > 0) {
			rndr_entity(p->tmp, entity);
			rndr_buf(p, ob, n, p->tmp);
		} else
			rndr_buf(p, ob, n, &n->rndr_entity.text);
		break;
	case LOWDOWN_BLOCKCODE:
		rndr_buf(p, ob, n, &n->rndr_blockcode.text);
		break;
	case LOWDOWN_BLOCKHTML:
		rndr_buf(p, ob, n, &n->rndr_blockhtml.text);
		break;
	case LOWDOWN_CODESPAN:
		rndr_buf(p, ob, n, &n->rndr_codespan.text);
		break;
	case LOWDOWN_IMAGE:
		rndr_buf(p, ob, n, &n->rndr_image.alt);
		/* FALLTHROUGH */
	case LOWDOWN_LINK:
	case LOWDOWN_LINK_AUTO:
		if (IS_STANDALONE_LINK(n, prev) ||
		    (p->flags & LOWDOWN_GEMINI_LINK_IN))
			break;
		if ((l = calloc(1, sizeof(struct link))) == NULL)
			return 0;
		l->n = n;
		l->id = ++p->linkqsz;
		TAILQ_INSERT_TAIL(&p->linkq, l, entries);
		hbuf_printf(p->tmp, "[Reference: link-%zu]", l->id);
		rndr_buf(p, ob, n, p->tmp);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rndr_buf(p, ob, n, &n->rndr_normal_text.text);
		break;
	case LOWDOWN_ROOT:
		if (TAILQ_EMPTY(&p->linkq) || 
		    !(p->flags & LOWDOWN_GEMINI_LINK_END))
			break;
		rndr_buf_vspace(p, ob, n, 2);
		rndr_flush_linkq(p, ob);
		break;
	default:
		break;
	}

	/* Trailing block spaces. */

	hbuf_truncate(p->tmp);
	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
	case LOWDOWN_BLOCKHTML:
		HBUF_PUTSL(p->tmp, "```");
		rndr_buf(p, ob, n, p->tmp);
		p->last_blank = 0;
		rndr_buf_vspace(p, ob, n, 2);
		break;
	case LOWDOWN_BLOCKQUOTE:
	case LOWDOWN_DEFINITION:
	case LOWDOWN_FOOTNOTES_BLOCK:
	case LOWDOWN_FOOTNOTE_DEF:
	case LOWDOWN_HEADER:
	case LOWDOWN_LIST:
	case LOWDOWN_PARAGRAPH:
	case LOWDOWN_TABLE_BLOCK:
		rndr_buf_vspace(p, ob, n, 2);
		break;
	case LOWDOWN_MATH_BLOCK:
		if (n->rndr_math.blockmode)
			rndr_buf_vspace(p, ob, n, 1);
		break;
	case LOWDOWN_DOC_HEADER:
		if (!TAILQ_EMPTY(&n->children))
			rndr_buf_vspace(p, ob, n, 2);
		break;
	case LOWDOWN_DEFINITION_DATA:
	case LOWDOWN_DEFINITION_TITLE:
	case LOWDOWN_HRULE:
	case LOWDOWN_LISTITEM:
	case LOWDOWN_META:
	case LOWDOWN_TABLE_ROW:
		rndr_buf_vspace(p, ob, n, 1);
		break;
	case LOWDOWN_IMAGE:
	case LOWDOWN_LINK:
	case LOWDOWN_LINK_AUTO:
		if (IS_STANDALONE_LINK(n, prev) ||
		    (p->flags & LOWDOWN_GEMINI_LINK_IN))
			rndr_buf_vspace(p, ob, n, 1);
		break;
	case LOWDOWN_ROOT:
		/*
		 * Special case: snip any trailing newlines that may
		 * have been printed as trailing vertical space.
		 * This tidies up the output.
		 */

		rndr_buf_vspace(p, ob, n, 1);
		while (ob->size && ob->data[ob->size - 1] == '\n')
			ob->size--;
		HBUF_PUTSL(ob, "\n");
		break;
	default:
		break;
	}

	if (p->last_blank > 1 && !TAILQ_EMPTY(&p->linkq) &&
	    !(p->flags & LOWDOWN_GEMINI_LINK_END)) {
		rndr_flush_linkq(p, ob);
		HBUF_PUTSL(ob, "\n");
		p->last_blank = 2;
	}

	return 1;
}

int
lowdown_gemini_rndr(struct lowdown_buf *ob,
	struct lowdown_metaq *mq, void *arg, 
	const struct lowdown_node *n)
{

	return rndr(ob, mq, (struct gemini *)arg, n);
}

void *
lowdown_gemini_new(const struct lowdown_opts *opts)
{
	struct gemini	*p;

	if ((p = calloc(1, sizeof(struct gemini))) == NULL)
		return NULL;

	TAILQ_INIT(&p->linkq);
	p->flags = opts != NULL ? opts->oflags : 0;

	/* Only use one kind of flag output. */

	if ((p->flags & LOWDOWN_GEMINI_LINK_IN) &&
	    (p->flags & LOWDOWN_GEMINI_LINK_END))
		p->flags &= ~LOWDOWN_GEMINI_LINK_IN;

	if ((p->tmp = hbuf_new(32)) == NULL) {
		free(p);
		return NULL;
	}

	return p;
}

void
lowdown_gemini_free(void *arg)
{
	struct gemini	*p = arg;
	struct link	*l;
	
	if (p == NULL)
		return;

	while ((l = TAILQ_FIRST(&p->linkq)) != NULL) {
		TAILQ_REMOVE(&p->linkq, l, entries);
		free(l);
	}

	hbuf_free(p->tmp);
	free(p);
}
