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
#include <time.h>

#include "lowdown.h"
#include "extern.h"

#if 0
Not done yet:
LOWDOWN_TABLE_HEADER		-> 
LOWDOWN_TABLE_BODY		-> 
LOWDOWN_TABLE_ROW		-> done
LOWDOWN_TABLE_CELL		-> 
#endif

struct tstack {
	const struct lowdown_node 	*n; /* node in question */
	size_t				 lines; /* times emitted */
};

struct term {
	unsigned int	 opts; /* oflags from lowdown_cfg */
	size_t		 col; /* output column from zero */
	ssize_t		 last_blank; /* line breaks or -1 (start) */
	struct tstack	*stack; /* stack of nodes */
	size_t		 stackmax; /* size of stack */
	size_t		 stackpos; /* position in stack */
	size_t		 maxcol; /* soft limit */
	size_t		 hmargin; /* left of content */
	size_t		 vmargin; /* before/after content */
	hbuf		*tmp; /* for temporary allocations */
};

/*
 * How to style the output on the screen.
 */
struct sty {
	int	 italic;
	int	 strike;
	int	 bold;
	int	 under;
	size_t	 bcolour; /* not inherited */
	size_t	 colour; /* not inherited */
	int	 override; /* don't inherit */
#define	OSTY_ITALIC	0x01
#define	OSTY_BOLD	0x02
};

/* Per-node styles. */

static const struct sty sty_image =	{ 0, 0, 1, 0,   0, 92, 1 };
static const struct sty sty_foot_ref =	{ 0, 0, 1, 0,   0, 92, 1 };
static const struct sty sty_codespan = 	{ 0, 0, 0, 0,  47, 31, 0 };
static const struct sty sty_hrule = 	{ 0, 0, 0, 0,   0, 37, 0 };
static const struct sty sty_blockhtml =	{ 0, 0, 0, 0,   0, 37, 0 };
static const struct sty sty_rawhtml = 	{ 0, 0, 0, 0,   0, 37, 0 };
static const struct sty sty_strike = 	{ 0, 1, 0, 0,   0,  0, 0 };
static const struct sty sty_emph = 	{ 1, 0, 0, 0,   0,  0, 0 };
static const struct sty sty_highlight =	{ 0, 0, 1, 0,   0,  0, 0 };
static const struct sty sty_d_emph = 	{ 0, 0, 1, 0,   0,  0, 0 };
static const struct sty sty_t_emph = 	{ 1, 0, 1, 0,   0,  0, 0 };
static const struct sty sty_link = 	{ 0, 0, 0, 1,   0, 32, 0 };
static const struct sty sty_autolink =	{ 0, 0, 0, 1,   0, 32, 0 };
static const struct sty sty_header =	{ 0, 0, 1, 0,   0,  0, 0 };

static const struct sty *stys[LOWDOWN__MAX] = {
	NULL, /* LOWDOWN_ROOT */
	NULL, /* LOWDOWN_BLOCKCODE */
	NULL, /* LOWDOWN_BLOCKQUOTE */
	&sty_header, /* LOWDOWN_HEADER */
	&sty_hrule, /* LOWDOWN_HRULE */
	NULL, /* LOWDOWN_LIST */
	NULL, /* LOWDOWN_LISTITEM */
	NULL, /* LOWDOWN_PARAGRAPH */
	NULL, /* LOWDOWN_TABLE_BLOCK */
	NULL, /* LOWDOWN_TABLE_HEADER */
	NULL, /* LOWDOWN_TABLE_BODY */
	NULL, /* LOWDOWN_TABLE_ROW */
	NULL, /* LOWDOWN_TABLE_CELL */
	NULL, /* LOWDOWN_FOOTNOTES_BLOCK */
	NULL, /* LOWDOWN_FOOTNOTE_DEF */
	&sty_blockhtml, /* LOWDOWN_BLOCKHTML */
	&sty_autolink, /* LOWDOWN_LINK_AUTO */
	&sty_codespan, /* LOWDOWN_CODESPAN */
	&sty_d_emph, /* LOWDOWN_DOUBLE_EMPHASIS */
	&sty_emph, /* LOWDOWN_EMPHASIS */
	&sty_highlight, /* LOWDOWN_HIGHLIGHT */
	&sty_image, /* LOWDOWN_IMAGE */
	NULL, /* LOWDOWN_LINEBREAK */
	&sty_link, /* LOWDOWN_LINK */
	&sty_t_emph, /* LOWDOWN_TRIPLE_EMPHASIS */
	&sty_strike, /* LOWDOWN_STRIKETHROUGH */
	NULL, /* LOWDOWN_SUPERSCRIPT */
	&sty_foot_ref, /* LOWDOWN_FOOTNOTE_REF */
	NULL, /* LOWDOWN_MATH_BLOCK */
	&sty_rawhtml, /* LOWDOWN_RAW_HTML */
	NULL, /* LOWDOWN_ENTITY */
	NULL, /* LOWDOWN_NORMAL_TEXT */
	NULL, /* LOWDOWN_DOC_HEADER */
	NULL, /* LOWDOWN_META */
	NULL /* LOWDOWN_DOC_FOOTER */
};

/* 
 * Special styles.
 * These are invoked in key places, below.
 */

static const struct sty sty_h1 = 	{ 0, 0, 0, 0, 104, 37, 0 };
static const struct sty sty_hn = 	{ 0, 0, 0, 0,   0, 36, 0 };
static const struct sty sty_linkalt =	{ 0, 0, 1, 0,   0, 92, 1|2 };
static const struct sty sty_imgurl = 	{ 0, 0, 0, 1,   0, 32, 2 };
static const struct sty sty_imgurlbox =	{ 0, 0, 0, 0,   0, 37, 2 };
static const struct sty sty_foots_div =	{ 0, 0, 0, 0,   0, 37, 0 };
static const struct sty sty_meta_key =	{ 0, 0, 0, 0,   0, 37, 0 };
static const struct sty sty_bad_ent = 	{ 0, 0, 0, 0,   0, 37, 0 };
static const struct sty sty_chng_ins =	{ 0, 0, 0, 0,  47, 30, 0 };
static const struct sty sty_chng_del =	{ 0, 0, 0, 0, 100,  0, 0 };

/*
 * Whether the style is not empty (i.e., has style attributes).
 */
#define	STY_NONEMPTY(_s) \
	((_s)->colour || (_s)->bold || (_s)->italic || \
	 (_s)->under || (_s)->strike || (_s)->bcolour || \
	 (_s)->override)

static void
rndr_escape(struct hbuf *out, const char *buf, size_t sz)
{
	size_t	 i, start = 0;

	for (i = 0; i < sz; i++) {
		if (iscntrl((unsigned char)buf[i])) {
			hbuf_put(out, buf + start, i - start);
			start = i + 1;
		}
	}
	if (start < sz) 
		hbuf_put(out, buf + start, sz - start);
}

/*
 * Link shortener.
 * This only shows the domain name and last path/filename.
 * It uses the following algorithm:
 *
 *   (1) strip schema (if none, print in full)
 *   (2) print domain following
 *   (3) if no path, return
 *   (4) if path, look for final path component
 *   (5) print final path component with /.../ if shortened
 */
static void
rndr_short_link(struct hbuf *out, const struct hbuf *link)
{
	size_t		 start = 0, sz;
	const char	*cp, *rcp;

	/* 
	 * Skip the leading protocol.
	 * If we don't find a protocol, leave it be.
	 */

	if (link->size > 7 && strncmp(link->data, "http://", 7) == 0)
		start = 7;
	else if (link->size > 8 && strncmp(link->data, "https://", 8) == 0)
		start = 8;
	else if (link->size > 7 && strncmp(link->data, "file://", 7) == 0)
		start = 7;
	else if (link->size > 7 && strncmp(link->data, "mailto:", 7) == 0)
		start = 7;
	else if (link->size > 6 && strncmp(link->data, "ftp://", 6) == 0)
		start = 6;

	if (start == 0) {
		hbuf_putb(out, link);
		return;
	}

	sz = link->size;
	if (link->data[link->size - 1] == '/')
		sz--;

	/* 
	 * Look for the end of the domain name. 
	 * If we don't have an end, then print the whole thing.
	 */

	cp = memchr(link->data + start, '/', sz - start);
	if (cp == NULL) {
		hbuf_put(out, link->data + start, sz - start);
		return;
	}

	hbuf_put(out, link->data + start, cp - (link->data + start));

	/* 
	 * Look for the filename.
	 * If it's the same as the end of the domain, then print the
	 * whole thing.
	 * Otherwise, use a "..." between.
	 */

	rcp = memrchr(link->data + start, '/', sz - start);

	if (rcp == cp) {
		hbuf_put(out, cp, sz - (cp - link->data));
		return;
	}

	HBUF_PUTSL(out, "/...");
	hbuf_put(out, rcp, sz - (rcp - link->data));
}

/*
 * Output style "s" into "out" as an ANSI escape.
 * If "s" does not have any style information, output nothing.
 */
static void
rndr_buf_style(struct hbuf *out, const struct sty *s)
{
	int	has = 0;

	if (!STY_NONEMPTY(s))
		return;

	HBUF_PUTSL(out, "\033[");
	if (s->bold) {
		HBUF_PUTSL(out, "1");
		has++;
	}
	if (s->under) {
		if (has++)
			HBUF_PUTSL(out, ";");
		HBUF_PUTSL(out, "4");
	}
	if (s->italic) {
		if (has++)
			HBUF_PUTSL(out, ";");
		HBUF_PUTSL(out, "3");
	}
	if (s->strike) {
		if (has++)
			HBUF_PUTSL(out, ";");
		HBUF_PUTSL(out, "9");
	}
	if (s->bcolour) {
		if (has++)
			HBUF_PUTSL(out, ";");
		hbuf_printf(out, "%zu", s->bcolour);
	}
	if (s->colour) {
		if (has++)
			HBUF_PUTSL(out, ";");
		hbuf_printf(out, "%zu", s->colour);
	}
	HBUF_PUTSL(out, "m");
}

/*
 * Take the given style "from" and apply it to "to".
 * This accumulates styles: unless an override has been set, it adds to
 * the existing style in "to" instead of overriding it.
 * The one exception is TODO colours, which override each other.
 */
static void
rndr_node_style_apply(struct sty *to, const struct sty *from)
{

	if (from->italic)
		to->italic = 1;
	if (from->strike)
		to->strike = 1;
	if (from->bold)
		to->bold = 1;
	else if ((from->override & OSTY_BOLD))
		to->bold = 0;
	if (from->under)
		to->under = 1;
	else if ((from->override & OSTY_ITALIC))
		to->under = 0;
	if (from->bcolour)
		to->bcolour = from->bcolour;
	if (from->colour)
		to->colour = from->colour;
}

/*
 * Apply the style for only the given node to the current style.
 * This *augments* the current style: see rndr_node_style_apply().
 * (This does not ascend to the parent node.)
 */
static void
rndr_node_style(struct sty *s, const struct lowdown_node *n)
{

	/* The basic node itself. */

	if (stys[n->type] != NULL)
		rndr_node_style_apply(s, stys[n->type]);

	/* Any special node situation that overrides. */

	switch (n->type) {
	case LOWDOWN_HEADER:
		if (n->rndr_header.level > 1)
			rndr_node_style_apply(s, &sty_hn);
		else
			rndr_node_style_apply(s, &sty_h1);
		break;
	default:
		/* FIXME: crawl up nested? */
		if (n->parent != NULL && 
		    n->parent->type == LOWDOWN_LINK)
			rndr_node_style_apply(s, &sty_linkalt);
		break;
	}

	if (n->chng == LOWDOWN_CHNG_INSERT) 
		rndr_node_style_apply(s, &sty_chng_ins);
	if (n->chng == LOWDOWN_CHNG_DELETE) 
		rndr_node_style_apply(s, &sty_chng_del);
}

/*
 * Bookkeep that we've put "len" characters into the current line.
 */
static void
rndr_buf_advance(struct term *term, size_t len)
{

	term->col += len;
	if (term->col && term->last_blank != 0)
		term->last_blank = 0;
}

/*
 * Return non-zero if "n" or any of its ancestors require resetting the
 * output line mode, otherwise return zero.
 * This applies to both block and inline styles.
 */
static int
rndr_buf_endstyle(const struct lowdown_node *n)
{
	struct sty	s;

	if (n->parent != NULL)
		if (rndr_buf_endstyle(n->parent))
			return 1;

	memset(&s, 0, sizeof(struct sty));
	rndr_node_style(&s, n);
	return STY_NONEMPTY(&s);
}

/*
 * Unsets the current style context given "n" and an optional terminal
 * style "osty", if applies.
 */
static void
rndr_buf_endwords(struct term *term, hbuf *out,
	const struct lowdown_node *n, const struct sty *osty)
{

	if (rndr_buf_endstyle(n) ||
	    (osty != NULL && STY_NONEMPTY(osty)))
		HBUF_PUTSL(out, "\033[0m");
}

/*
 * Like rndr_buf_endwords(), but also terminating the line itself.
 */
static void
rndr_buf_endline(struct term *term, hbuf *out,
	const struct lowdown_node *n, const struct sty *osty)
{

	rndr_buf_endwords(term, out, n, osty);

	/* 
	 * We can legit be at col == 0 if, for example, we're in a
	 * literal context with a blank line.
	 * assert(term->col > 0);
	 * assert(term->last_blank == 0);
	 */

	HBUF_PUTSL(out, "\n");
	term->col = 0;
	term->last_blank = 1;
}

/*
 * Output optional number of newlines before or after content.
 */
static void
rndr_buf_vspace(struct term *term, hbuf *out,
	const struct lowdown_node *n, size_t sz)
{

	if (term->last_blank == -1)
		return;
	while ((size_t)term->last_blank < sz) {
		HBUF_PUTSL(out, "\n");
		term->last_blank++;
	}
	term->col = 0;
}

/*
 * Output prefixes of the given node in the style further accumulated
 * from the parent nodes.
 */
static void
rndr_buf_startline_prefixes(struct term *term,
	struct sty *s, const struct lowdown_node *n, hbuf *out)
{
	size_t	 			 i, emit;
	int	 		 	 pstyle = 0;
	const struct lowdown_node	*np;

	if (n->parent != NULL)
		rndr_buf_startline_prefixes(term, s, n->parent, out);

	rndr_node_style(s, n);

	/*
	 * Look up the current node in the list of node's we're
	 * servicing so we can get how many times we've output the
	 * prefix.
	 * This is used for (e.g.) lists, where we only output the list
	 * prefix once.
	 */

	for (i = 0; i <= term->stackpos; i++)
		if (term->stack[i].n == n)
			break;
	assert(i <= term->stackpos);
	emit = term->stack[i].lines++;
	
	/*
	 * Output any prefixes.
	 * Any output must have rndr_buf_style() and set pstyle so that
	 * we close out the style afterward.
	 */

	switch (n->type) {
	case LOWDOWN_PARAGRAPH:
		/*
		 * Collapse leading white-space if we're already within
		 * a margin-bearing block statement.
		 */

		for (np = n->parent; np != NULL; np = np->parent)
			if (np->type == LOWDOWN_LISTITEM ||
			    np->type == LOWDOWN_BLOCKQUOTE ||
			    np->type == LOWDOWN_FOOTNOTE_DEF)
				break;
		if (np == NULL) {
			HBUF_PUTSL(out, "    ");
			rndr_buf_advance(term, 4);
		}
		break;
	case LOWDOWN_BLOCKCODE:
		rndr_buf_style(out, s);
		pstyle = 1;
		HBUF_PUTSL(out, "      ");
		rndr_buf_advance(term, 6);
		break;
	case LOWDOWN_ROOT:
		rndr_buf_style(out, s);
		pstyle = 1;
		for (i = 0; i < term->hmargin; i++)
			HBUF_PUTSL(out, " ");
		break;
	case LOWDOWN_BLOCKQUOTE:
		rndr_buf_style(out, s);
		pstyle = 1;
		HBUF_PUTSL(out, "  | ");
		rndr_buf_advance(term, 4);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rndr_buf_style(out, s);
		pstyle = 1;
		if (emit == 0)
			hbuf_printf(out, "%2zu. ", 
				n->rndr_footnote_def.num);
		else
			HBUF_PUTSL(out, "    ");
		rndr_buf_advance(term, 4);
		break;
	case LOWDOWN_HEADER:
		if (n->rndr_header.level == 1)
			break;
		rndr_buf_style(out, s);
		pstyle = 1;
		for (i = 0; i < n->rndr_header.level; i++)
			HBUF_PUTSL(out, "#");
		HBUF_PUTSL(out, " ");
		rndr_buf_advance(term, i + 1);
		break;
	case LOWDOWN_LISTITEM:
		rndr_buf_style(out, s);
		pstyle = 1;
		if (n->parent == NULL || 
		    n->parent->rndr_list.flags == 0) {
			hbuf_puts(out, emit == 0 ? 
				"  - " : "    ");
			rndr_buf_advance(term, 4);
			break;
		}
		if (emit == 0)
			hbuf_printf(out, "%2zu. ", 
				n->rndr_listitem.num);
		else
			HBUF_PUTSL(out, "    ");
		rndr_buf_advance(term, 4);
		break;
	default:
		break;
	}

	if (pstyle && STY_NONEMPTY(s))
		HBUF_PUTSL(out, "\033[0m");
}

/*
 * Like rndr_buf_startwords(), but at the start of a line.
 * This also outputs all line prefixes of the block context.
 */
static void
rndr_buf_startline(struct term *term, hbuf *out, 
	const struct lowdown_node *n, const struct sty *osty)
{
	struct sty	 s;

	assert(term->last_blank);
	assert(term->col == 0);

	memset(&s, 0, sizeof(struct sty));
	rndr_buf_startline_prefixes(term, &s, n, out);
	if (osty != NULL)
		rndr_node_style_apply(&s, osty);
	rndr_buf_style(out, &s);
}

/*
 * Ascend to the root of the parse tree from rndr_buf_startwords(),
 * accumulating styles as we do so.
 */
static void
rndr_buf_startwords_style(const struct lowdown_node *n, struct sty *s)
{

	if (n->parent != NULL)
		rndr_buf_startwords_style(n->parent, s);
	rndr_node_style(s, n);
}

/*
 * Accumulate and output the style at the start of one or more words.
 * Should *not* be called on the start of a new line, which calls for
 * rndr_buf_startline().
 */
static void
rndr_buf_startwords(struct term *term, hbuf *out,
	const struct lowdown_node *n, const struct sty *osty)
{
	struct sty	 s;

	assert(!term->last_blank);
	assert(term->col > 0);

	memset(&s, 0, sizeof(struct sty));
	rndr_buf_startwords_style(n, &s);
	if (osty != NULL)
		rndr_node_style_apply(&s, osty);
	rndr_buf_style(out, &s);
}

static void
rndr_buf_literal(struct term *term, hbuf *out, 
	const struct lowdown_node *n, const hbuf *in,
	const struct sty *osty)
{
	size_t		 i = 0, len;
	const char	*start;

	while (i < in->size) {
		start = &in->data[i];
		while (i < in->size && in->data[i] != '\n')
			i++;
		len = &in->data[i] - start;
		i++;
		rndr_buf_startline(term, out, n, osty);
		rndr_escape(out, start, len);
		rndr_buf_advance(term, len);
		rndr_buf_endline(term, out, n, osty);
	}
}

/*
 * Emit text in "in" the current line with output "out".
 * Use "n" and its ancestry to determine our context.
 */
static void
rndr_buf(struct term *term, hbuf *out, 
	const struct lowdown_node *n, const hbuf *in,
	const struct sty *osty)
{
	size_t	 	 i = 0, len;
	int 		 needspace, begin = 1, end = 0;
	const char	*start;
	const struct lowdown_node *nn;

	for (nn = n; nn != NULL; nn = nn->parent)
		if (nn->type == LOWDOWN_BLOCKCODE ||
	  	    nn->type == LOWDOWN_BLOCKHTML) {
			rndr_buf_literal(term, out, n, in, osty);
			return;
		}

	/* Start each word by seeing if it has leading space. */

	while (i < in->size) {
		needspace = isspace((unsigned char)in->data[i]);

		while (i < in->size && 
		       isspace((unsigned char)in->data[i]))
			i++;

		/* See how long it the coming word (may be 0). */

		start = &in->data[i];
		while (i < in->size &&
		       !isspace((unsigned char)in->data[i]))
			i++;
		len = &in->data[i] - start;

		/* 
		 * If we cross our maximum width and are preceded by a
		 * space, then break.
		 * (Leaving out the check for a space will cause
		 * adjacent text or punctuation to have a preceding
		 * newline.)
		 * This will also unset the current style.
		 */

		if ((needspace || 
	 	     (out->size && isspace
		      ((unsigned char)out->data[out->size - 1]))) &&
		    term->col && term->col + len > term->maxcol) {
			rndr_buf_endline(term, out, n, osty);
			end = 0;
		}

		/*
		 * Either emit our new line prefix (only if we have a
		 * word that will follow!) or, if we need space, emit
		 * the spacing.  In the first case, or if we have
		 * following text and are starting this node, emit our
		 * current style.
		 */

		if (term->last_blank && len) {
			rndr_buf_startline(term, out, n, osty);
			begin = 0;
			end = 1;
		} else if (!term->last_blank) {
			if (begin && len) {
				rndr_buf_startwords(term, out, n, osty);
				begin = 0;
				end = 1;
			}
			if (needspace) {
				HBUF_PUTSL(out, " ");
				rndr_buf_advance(term, 1);
			}
		}

		/* Emit the word itself. */

		rndr_escape(out, start, len);
		rndr_buf_advance(term, len);
	}

	if (end) {
		assert(begin == 0);
		rndr_buf_endwords(term, out, n, osty);
	}
}

/*
 * Output the unicode entry "val", which must be strictly greater than
 * zero, as a UTF-8 sequence.
 * This does no error checking.
 */
static void
rndr_entity(hbuf *buf, int32_t val)
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

static void
rndr(hbuf *ob, struct lowdown_metaq *mq,
	struct term *p, const struct lowdown_node *n)
{
	const struct lowdown_node	*child;
	struct lowdown_meta		*m;
	hbuf				*metatmp;
	int32_t				 entity;
	size_t				 i, col;
	ssize_t			 	 last_blank;
	
	/* Current nodes we're servicing. */

	if (p->stackpos >= p->stackmax) {
		p->stackmax += 256;
		p->stack = xreallocarray(p->stack,
			p->stackmax, sizeof(struct tstack));
	}
	memset(&p->stack[p->stackpos], 0, sizeof(struct tstack));
	p->stack[p->stackpos].n = n;

	/* Vertical space before content. */

	switch (n->type) {
	case LOWDOWN_ROOT:

		/* Emit vmargin. */

		for (i = 0; i < p->vmargin; i++)
			HBUF_PUTSL(ob, "\n");
		p->last_blank = -1;
		break;
	case LOWDOWN_BLOCKCODE:
	case LOWDOWN_BLOCKHTML:
	case LOWDOWN_BLOCKQUOTE:
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
	case LOWDOWN_HRULE:
	case LOWDOWN_LINEBREAK:
	case LOWDOWN_LISTITEM:
	case LOWDOWN_META:
	case LOWDOWN_TABLE_ROW:
		rndr_buf_vspace(p, ob, n, 1);
		break;
	default:
		break;
	}

	/* Output leading content. */

	switch (n->type) {
	case LOWDOWN_FOOTNOTES_BLOCK:
		hbuf_truncate(p->tmp);
		HBUF_PUTSL(p->tmp, "~~~~~~~~");
		rndr_buf(p, ob, n, p->tmp, &sty_foots_div);
		break;
	case LOWDOWN_SUPERSCRIPT:
		hbuf_truncate(p->tmp);
		HBUF_PUTSL(p->tmp, "^");
		rndr_buf(p, ob, n, p->tmp, NULL);
		break;
	case LOWDOWN_META:
		rndr_buf(p, ob, n, &n->rndr_meta.key, &sty_meta_key);
		hbuf_truncate(p->tmp);
		HBUF_PUTSL(p->tmp, ": ");
		rndr_buf(p, ob, n, p->tmp, &sty_meta_key);
		if (mq == NULL)
			break;

		/*
		 * Manually render the children of the meta into a
		 * buffer and use that as our value.  Start by zeroing
		 * our terminal position and using another output buffer
		 * (p->tmp would be clobbered by children).
		 */

		last_blank = p->last_blank;
		p->last_blank = -1;
		col = p->col;
		p->col = 0;
		metatmp = hbuf_new(128);
		m = xcalloc(1, sizeof(struct lowdown_meta));
		TAILQ_INSERT_TAIL(mq, m, entries);
		m->key = xstrndup(n->rndr_meta.key.data,
			n->rndr_meta.key.size);
		TAILQ_FOREACH(child, &n->children, entries) {
			p->stackpos++;
			rndr(metatmp, mq, p, child);
			p->stackpos--;
		}
		m->value = xstrndup(metatmp->data, metatmp->size);
		hbuf_free(metatmp);
		p->last_blank = last_blank;
		p->col = col;
		break;
	default:
		break;
	}

	/* Descend into children. */

	TAILQ_FOREACH(child, &n->children, entries) {
		p->stackpos++;
		rndr(ob, mq, p, child);
		p->stackpos--;
	}

	/* Output content. */

	switch (n->type) {
	case LOWDOWN_HRULE:
		hbuf_truncate(p->tmp);
		HBUF_PUTSL(p->tmp, "~~~~~~~~");
		rndr_buf(p, ob, n, p->tmp, NULL);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		hbuf_truncate(p->tmp);
		hbuf_printf(p->tmp, "[%zu]", 
			n->rndr_footnote_ref.num);
		rndr_buf(p, ob, n, p->tmp, NULL);
		break;
	case LOWDOWN_RAW_HTML:
		rndr_buf(p, ob, n, &n->rndr_raw_html.text, NULL);
		break;
	case LOWDOWN_MATH_BLOCK:
		rndr_buf(p, ob, n, &n->rndr_math.text, NULL);
		break;
	case LOWDOWN_ENTITY:
		entity = entity_find(&n->rndr_entity.text);
		if (entity > 0) {
			hbuf_truncate(p->tmp);
			rndr_entity(p->tmp, entity);
			rndr_buf(p, ob, n, p->tmp, NULL);
		} else
			rndr_buf(p, ob, n, &n->rndr_entity.text, 
				&sty_bad_ent);
		break;
	case LOWDOWN_BLOCKCODE:
		rndr_buf(p, ob, n, &n->rndr_blockcode.text, NULL);
		break;
	case LOWDOWN_BLOCKHTML:
		rndr_buf(p, ob, n, &n->rndr_blockhtml.text, NULL);
		break;
	case LOWDOWN_CODESPAN:
		rndr_buf(p, ob, n, &n->rndr_codespan.text, NULL);
		break;
	case LOWDOWN_LINK_AUTO:
		if ((p->opts & LOWDOWN_TERM_SHORTLINK)) {
			hbuf_truncate(p->tmp);
			rndr_short_link(p->tmp, &n->rndr_autolink.link);
			rndr_buf(p, ob, n, p->tmp, NULL);
		} else 
			rndr_buf(p, ob, n, &n->rndr_autolink.link, NULL);
		break;
	case LOWDOWN_LINK:
		hbuf_truncate(p->tmp);
		HBUF_PUTSL(p->tmp, " "); 
		rndr_buf(p, ob, n, p->tmp, NULL);
		if ((p->opts & LOWDOWN_TERM_SHORTLINK)) {
			hbuf_truncate(p->tmp);
			rndr_short_link(p->tmp, &n->rndr_link.link);
			rndr_buf(p, ob, n, p->tmp, NULL);
		} else 
			rndr_buf(p, ob, n, &n->rndr_link.link, NULL);
		break;
	case LOWDOWN_IMAGE:
		rndr_buf(p, ob, n, &n->rndr_image.alt, NULL);
		if (n->rndr_image.alt.size) {
			hbuf_truncate(p->tmp);
			HBUF_PUTSL(p->tmp, " "); 
			rndr_buf(p, ob, n, p->tmp, NULL);
		}
		hbuf_truncate(p->tmp);
		HBUF_PUTSL(p->tmp, "[Image: ");
		rndr_buf(p, ob, n, p->tmp, &sty_imgurlbox);
		if ((p->opts & LOWDOWN_TERM_SHORTLINK)) {
			hbuf_truncate(p->tmp);
			rndr_short_link(p->tmp, &n->rndr_image.link);
			rndr_buf(p, ob, n, p->tmp, &sty_imgurl);
		} else
			rndr_buf(p, ob, n, &n->rndr_image.link, &sty_imgurl);
		hbuf_truncate(p->tmp);
		HBUF_PUTSL(p->tmp, "]");
		rndr_buf(p, ob, n, p->tmp, &sty_imgurlbox);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rndr_buf(p, ob, n, &n->rndr_normal_text.text, NULL);
		break;
	default:
		break;
	}

	/* Trailing block spaces. */

	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
	case LOWDOWN_BLOCKHTML:
	case LOWDOWN_BLOCKQUOTE:
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
	case LOWDOWN_HRULE:
	case LOWDOWN_LISTITEM:
	case LOWDOWN_META:
	case LOWDOWN_TABLE_ROW:
		rndr_buf_vspace(p, ob, n, 1);
		break;
	case LOWDOWN_ROOT:
		rndr_buf_vspace(p, ob, n, 1);
		while (ob->size && ob->data[ob->size - 1] == '\n')
			ob->size--;
		HBUF_PUTSL(ob, "\n");

		/* Strip breaks but for the vmargin. */

		for (i = 0; i < p->vmargin; i++)
			HBUF_PUTSL(ob, "\n");
		break;
	default:
		break;
	}
}

void
lowdown_term_rndr(hbuf *ob, struct lowdown_metaq *mq,
	void *arg, const struct lowdown_node *n)
{
	struct term	*p = arg;

	p->stackpos = 0;
	rndr(ob, mq, p, n);
}

void *
lowdown_term_new(const struct lowdown_opts *opts)
{
	struct term	*p;

	p = xcalloc(1, sizeof(struct term));

	/* Give us 80 columns by default. */

	p->maxcol = opts == NULL || opts->cols == 0 ? 80 : opts->cols;
	p->hmargin = opts == NULL ? 0 : opts->hmargin;
	p->vmargin = opts == NULL ? 0 : opts->vmargin;
	p->opts = opts == NULL ? 0 : opts->oflags;
	p->tmp = hbuf_new(32);
	return p;
}

void
lowdown_term_free(void *arg)
{
	struct term	*p = arg;
	
	if (p == NULL)
		return;

	hbuf_free(p->tmp);
	free(p->stack);
	free(p);
}
