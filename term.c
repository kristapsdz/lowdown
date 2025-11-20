/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <wchar.h>

#include "lowdown.h"
#include "extern.h"

struct tstack {
	const struct lowdown_node *n; /* node in question */
	size_t			   lines; /* times emitted */
};

struct term {
	unsigned int		  opts; /* oflags from lowdown_cfg */
	size_t			  col; /* output column from zero */
	ssize_t			  last_blank; /* line breaks or -1 (start) */
	struct tstack		 *stack; /* stack of nodes */
	size_t			  stackmax; /* size of stack */
	size_t			  stackpos; /* position in stack */
	size_t			  width; /* soft width of content */
	size_t			  hmargin; /* left of content */
	size_t			  hpadding; /* left of content */
	size_t			  vmargin; /* before/after content */
	struct lowdown_buf	 *tmp; /* for temporary allocations */
	wchar_t			 *buf; /* buffer for counting wchar */
	size_t			  bufsz; /* size of buf */
	struct lowdown_buf	**foots; /* footnotes */
	size_t			  footsz; /* footnotes size  */
	int			  footoff; /* don't collect (tables) */
	struct lowdown_metaq	  metaq; /* metadata */
	const struct lowdown_node*in_link; /* in an OSC8 hyperlink */
};

/*
 * How to style the output on the screen.
 */
struct sty {
	int		 italic; /* italic */
	int		 strike; /* strikethrough */
	int		 bold; /* bold */
	int		 under; /* underline */
	size_t		 bcolour; /* not inherited */
	size_t		 colour; /* not inherited */
	int		 override; /* don't inherit... */
#define	OSTY_UNDER	 0x01 /* underlining */
#define	OSTY_BOLD	 0x02 /* bold */
};

/*
 * Prefixes to put before each line.  This only applies to very specific
 * circumstances.
 */
struct	pfx {
	const char	*text;
	size_t		 cols;
};

#include "term.h"

static const struct sty *stys[LOWDOWN__MAX] = {
	NULL, /* LOWDOWN_ROOT */
	&sty_blockcode, /* LOWDOWN_BLOCKCODE */
	NULL, /* LOWDOWN_BLOCKQUOTE */
	NULL, /* LOWDOWN_DEFINITION */
	NULL, /* LOWDOWN_DEFINITION_TITLE */
	NULL, /* LOWDOWN_DEFINITION_DATA */
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
	&sty_blockhtml, /* LOWDOWN_BLOCKHTML */
	&sty_autolink, /* LOWDOWN_LINK_AUTO */
	&sty_codespan, /* LOWDOWN_CODESPAN */
	&sty_d_emph, /* LOWDOWN_DOUBLE_EMPHASIS */
	&sty_emph, /* LOWDOWN_EMPHASIS */
	&sty_highlight, /* LOWDOWN_HIGHLIGHT */
	&sty_img, /* LOWDOWN_IMAGE */
	NULL, /* LOWDOWN_LINEBREAK */
	&sty_link, /* LOWDOWN_LINK */
	&sty_t_emph, /* LOWDOWN_TRIPLE_EMPHASIS */
	&sty_strike, /* LOWDOWN_STRIKETHROUGH */
	NULL, /* LOWDOWN_SUBSCRIPT */
	NULL, /* LOWDOWN_SUPERSCRIPT */
	NULL, /* LOWDOWN_FOOTNOTE */
	NULL, /* LOWDOWN_MATH_BLOCK */
	&sty_rawhtml, /* LOWDOWN_RAW_HTML */
	NULL, /* LOWDOWN_ENTITY */
	NULL, /* LOWDOWN_NORMAL_TEXT */
	NULL, /* LOWDOWN_DOC_HEADER */
	NULL, /* LOWDOWN_META */
};

/*
 * Whether the style is not empty (i.e., has style attributes).
 */
#define	STY_NONEMPTY(_s) \
	((_s)->colour || (_s)->bold || (_s)->italic || \
	 (_s)->under || (_s)->strike || (_s)->bcolour || \
	 (_s)->override)

/* Forward declaration. */

static int
rndr(struct lowdown_buf *, struct term *, const struct lowdown_node *);

/*
 * Get the column width of a multi-byte sequence.  The sequence should
 * be self-contained, i.e., not straddle multi-byte borders, because the
 * calculation for UTF-8 columns is local to this function: a split
 * multi-byte sequence will fail to return the correct number of
 * printable columns.  If the sequence is bad, return the number of raw
 * bytes to print.  Return <0 on failure (memory), >=0 otherwise with
 * the number of printable columns.
 */
static ssize_t
rndr_mbswidth(struct term *term, const char *buf, size_t sz)
{
	size_t	 	 wsz, csz;
	const char	*cp;
	void		*pp;
	mbstate_t	 mbs;

	memset(&mbs, 0, sizeof(mbstate_t));
	cp = buf;
	wsz = mbsnrtowcs(NULL, &cp, sz, 0, &mbs);
	if (wsz == (size_t)-1)
		return sz;

	if (term->bufsz < wsz) {
		term->bufsz = wsz;
		pp = reallocarray(term->buf, wsz, sizeof(wchar_t));
		if (pp == NULL)
			return -1;
		term->buf = pp;
	}

	memset(&mbs, 0, sizeof(mbstate_t));
	cp = buf;
	mbsnrtowcs(term->buf, &cp, sz, wsz, &mbs);
	csz = wcswidth(term->buf, wsz);
	return csz == (size_t)-1 ? sz : csz;
}


/*
 * Copy the buffer into "out", escaping along the width.
 * Returns the number of actual printed columns, which in the case of
 * multi-byte glyphs, may be less than the given bytes.
 * Return <0 on failure (memory), >= 0 otherwise.
 */
static ssize_t
rndr_escape(struct term *term, struct lowdown_buf *out,
	const char *buf, size_t sz)
{
	size_t		 i, start = 0, cols = 0;
	ssize_t		 ret;
	unsigned char	 ch;

	/* Don't allow control characters through. */

	for (i = 0; i < sz; i++) {
		ch = (unsigned char)buf[i];
		if (ch < 0x80 && iscntrl(ch)) {
			ret = rndr_mbswidth (term, buf + start,
				i - start);
			if (ret < 0)
				return -1;
			cols += ret;
			if (!hbuf_put(out, buf + start, i - start))
				return -1;
			start = i + 1;
		}
	}

	/* Remaining bytes. */

	if (start < sz) {
		ret = rndr_mbswidth(term, buf + start, sz - start);
		if (ret < 0)
			return -1;
		cols += ret;
		if (!hbuf_put(out, buf + start, sz - start))
			return -1;
	}

	return cols;
}

static void
rndr_free_footnotes(struct term *st)
{
	size_t	 i;

	for (i = 0; i < st->footsz; i++)
		hbuf_free(st->foots[i]);

	free(st->foots);
	st->foots = NULL;
	st->footsz = 0;
	st->footoff = 0;
}

/*
 * If there's an active style in "s" or s is NULL), then emit an
 * unstyling escape sequence.  Return zero on failure (memory), non-zero
 * on success.
 */
static int
rndr_buf_unstyle(const struct term *term,
	struct lowdown_buf *out, const struct sty *s)
{

	if (term->opts & LOWDOWN_TERM_NOANSI)
		return 1;
	if (s != NULL && !STY_NONEMPTY(s))
		return 1;
	return HBUF_PUTSL(out, "\033[0m");
}

/*
 * Start sequence for a terminal link (only in ANSI mode).
 */
static int
rndr_buf_osc8_open(const struct term *term, struct lowdown_buf *out,
    const struct lowdown_node *n)
{
	const struct lowdown_buf	*uri = NULL;

	if (term->opts & LOWDOWN_TERM_NOANSI)
		return 1;

	if (n->type == LOWDOWN_LINK_AUTO)
		uri = &n->rndr_autolink.link;
	else if (n->type == LOWDOWN_LINK)
		uri = &n->rndr_link.link;
	else if (n->type == LOWDOWN_IMAGE)
		uri = &n->rndr_image.link;

	/*
	 * Don't output an id for the link.  It's trivial to have a
	 * random per-page value for this identifier (e.g., to
	 * initialise a random number then append the node identifier),
	 * but let the terminal handle this.
	 */

	assert(uri != NULL);
	return HBUF_PUTSL(out, "\033]8;;") &&
	    hbuf_putb(out, uri) &&
	    HBUF_PUTSL(out, "\033\\");
}

/*
 * Close a currently-open link.
 */
static int
rndr_buf_osc8_close(const struct term *term, struct lowdown_buf *out)
{

	if (term->opts & LOWDOWN_TERM_NOANSI)
		return 1;

	/*
	 * It would be trivial to crawl up our parent chain and either
	 * switch from the current link or close out the link context
	 * entirely, but lowdown(5) stipulates that nested links are not
	 * possible.
	 */

	return HBUF_PUTSL(out, "\033]8;;\033\\");
}

/*
 * Output style "s" into "out" as an ANSI escape.  If "s" does not have
 * any style information or is NULL, output nothing.  Return zero on
 * failure (memory), non-zero on success.
 */
static int
rndr_buf_style(const struct term *term,
	struct lowdown_buf *out, const struct sty *s)
{
	int	has = 0;

	if (term->opts & LOWDOWN_TERM_NOANSI)
		return 1;
	if (s == NULL || !STY_NONEMPTY(s))
		return 1;
	if (!HBUF_PUTSL(out, "\033["))
		return 0;

	if (s->bold) {
		if (!HBUF_PUTSL(out, "1"))
			return 0;
		has++;
	}
	if (s->under) {
		if (has++ && !HBUF_PUTSL(out, ";"))
			return 0;
		if (!HBUF_PUTSL(out, "4"))
			return 0;
	}
	if (s->italic) {
		if (has++ && !HBUF_PUTSL(out, ";"))
			return 0;
		if (!HBUF_PUTSL(out, "3"))
			return 0;
	}
	if (s->strike) {
		if (has++ && !HBUF_PUTSL(out, ";"))
			return 0;
		if (!HBUF_PUTSL(out, "9"))
			return 0;
	}
	if (s->bcolour && !(term->opts & LOWDOWN_TERM_NOCOLOUR) &&
	    ((s->bcolour >= 40 && s->bcolour <= 47) ||
	     (s->bcolour >= 100 && s->bcolour <= 107))) {
		if (has++ && !HBUF_PUTSL(out, ";"))
			return 0;
		if (!hbuf_printf(out, "%zu", s->bcolour))
			return 0;
	}
	if (s->colour && !(term->opts & LOWDOWN_TERM_NOCOLOUR) &&
	    ((s->colour >= 30 && s->colour <= 37) ||
	     (s->colour >= 90 && s->colour <= 97))) {
		if (has++ && !HBUF_PUTSL(out, ";"))
			return 0;
		if (!hbuf_printf(out, "%zu", s->colour))
			return 0;
	}
	return HBUF_PUTSL(out, "m");
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
	else if ((from->override & OSTY_UNDER))
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
		if (n->rndr_header.level > 0)
			rndr_node_style_apply(s, &sty_header_n);
		else
			rndr_node_style_apply(s, &sty_header_1);
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
 * style "osty", if applies.  Return zero on failure (memory), non-zero
 * on success.
 */
static int
rndr_buf_endwords(struct term *term, struct lowdown_buf *out,
	const struct lowdown_node *n, const struct sty *osty)
{
	/*
	 * If an OSC8 hyperlink should be closed, do it now (it doesn't
	 * matter where this appears in relation to other styling).
	 */

	if (rndr_buf_endstyle(n) && !rndr_buf_unstyle(term, out, NULL))
        	return 0;
	if (osty != NULL && !rndr_buf_unstyle(term, out, osty))
        	return 0;
	if (term->in_link && !rndr_buf_osc8_close(term, out))
        	return 0;
	return 1;
}

/*
 * Like rndr_buf_endwords(), but also terminating the line itself.
 * Return zero on failure (memory), non-zero on success.
 */
static int
rndr_buf_endline(struct term *term, struct lowdown_buf *out,
	const struct lowdown_node *n, const struct sty *osty)
{

	if (!rndr_buf_endwords(term, out, n, osty))
		return 0;

	/*
	 * We can legit be at col == 0 if, for example, we're in a
	 * literal context with a blank line.
	 * assert(term->col > 0);
	 * assert(term->last_blank == 0);
	 */

	term->col = 0;
	term->last_blank = 1;
	return HBUF_PUTSL(out, "\n");
}

/*
 * Return the printed width of the number up to six digits (we're
 * probably not going to have more list items than that).
 */
static size_t
rndr_numlen(size_t sz)
{

	if (sz > 100000)
		return 6;
	if (sz > 10000)
		return 5;
	if (sz > 1000)
		return 4;
	if (sz > 100)
		return 3;
	if (sz > 10)
		return 2;
	return 1;
}

/*
 * Output prefixes of the given node in the style further accumulated
 * from the parent nodes.  "Depth" is set to how deep we are, starting
 * at -1 (the root).
 * Return zero on failure (memory), non-zero on success.
 */
static int
rndr_buf_startline_prefixes(struct term *term,
	struct sty *s, const struct lowdown_node *n,
	struct lowdown_buf *out, size_t *depth)
{
	struct sty			 sinner;
	const struct pfx		*pfx;
	size_t	 			 i, emit, len;
	int	 		 	 pstyle = 0;
	enum hlist_fl			 fl;

	if (n->parent != NULL &&
	    !rndr_buf_startline_prefixes(term, s, n->parent, out, depth))
		return 0;

	if (n->parent == NULL) {
		assert(n->type == LOWDOWN_ROOT);
		*depth = -1;
	}

	/*
	 * The "sinner" value is temporary for only this function.
	 * This allows us to set a temporary style mask that only
	 * applies to the prefix data.
	 * Otherwise "s" propagates to the subsequent line.
	 */

	rndr_node_style(s, n);
	sinner = *s;

	/*
	 * Look up the current node in the list of node's we're
	 * servicing so we can get how many times we've output the
	 * prefix.  This is used for (e.g.) lists, where we only output
	 * the list prefix once.  XXX: read backwards for faster perf?
	 */

	for (i = 0; i <= term->stackpos; i++)
		if (term->stack[i].n == n)
			break;

	/*
	 * If we can't find the node, then we're in a "faked" context
	 * like footnotes within a table.  Ignore this.  XXX: is there a
	 * non-hacky way for this?
	 */

	if (i > term->stackpos)
		return 1;

	emit = term->stack[i].lines++;

	/*
	 * Output any prefixes.
	 * Any output must have rndr_buf_style() and set pstyle so that
	 * we close out the style afterward.
	 */

	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
		rndr_node_style_apply(&sinner, &sty_bkcd_pfx);
		if (!rndr_buf_style(term, out, &sinner))
			return 0;
		pstyle = 1;
		if (!hbuf_puts(out, pfx_bkcd.text))
			return 0;
		rndr_buf_advance(term, pfx_bkcd.cols);
		break;
	case LOWDOWN_ROOT:
		if (!rndr_buf_style(term, out, &sinner))
			return 0;
		pstyle = 1;
		for (i = 0; i < term->hmargin; i++)
			if (!HBUF_PUTSL(out, " "))
				return 0;
		for (i = 0; i < term->hpadding; i++)
			if (!HBUF_PUTSL(out, " "))
				return 0;
		break;
	case LOWDOWN_BLOCKQUOTE:
		rndr_node_style_apply(&sinner, &sty_bkqt_pfx);
		if (!rndr_buf_style(term, out, &sinner))
			return 0;
		pstyle = 1;
		if (!hbuf_puts(out, pfx_bkqt.text))
			return 0;
		rndr_buf_advance(term, pfx_bkqt.cols);
		break;
	case LOWDOWN_DEFINITION_DATA:
		rndr_node_style_apply(&sinner, &sty_dli_pfx);
		if (!rndr_buf_style(term, out, &sinner))
			return 0;
		pstyle = 1;
		if (emit == 0) {
			if (!hbuf_puts(out, pfx_dli_1.text))
				return 0;
			rndr_buf_advance(term, pfx_dli_1.cols);
		} else {
			if (!hbuf_puts(out, pfx_dli_n.text))
				return 0;
			rndr_buf_advance(term, pfx_dli_n.cols);
		}
		break;
	case LOWDOWN_FOOTNOTE:
		rndr_node_style_apply(&sinner, &sty_fdef_pfx);
		if (!rndr_buf_style(term, out, &sinner))
			return 0;
		pstyle = 1;
		if (emit == 0) {
			if (!hbuf_printf(out, "%2zu. ",
			    term->footsz + 1))
				return 0;
			len = rndr_numlen(term->footsz + 1);
			if (len + 2 > pfx_fdef_1.cols)
				len += 2;
			else
				len = pfx_fdef_1.cols;
			rndr_buf_advance(term, len);
		} else {
			if (!hbuf_puts(out, pfx_fdef_n.text))
				return 0;
			rndr_buf_advance(term, pfx_fdef_n.cols);
		}
		break;
	case LOWDOWN_HEADER:
		if (n->rndr_header.level == 0)
			pfx = &pfx_header_1;
		else
			pfx = &pfx_header_n;
		if (!rndr_buf_style(term, out, &sinner))
			return 0;
		pstyle = 1;
		for (i = 0; i < n->rndr_header.level + 1; i++) {
			if (!hbuf_puts(out, pfx->text))
				return 0;
			rndr_buf_advance(term, pfx->cols);
		}
		if (pfx->cols) {
			if (!HBUF_PUTSL(out, " "))
				return 0;
			rndr_buf_advance(term, 1);
		}
		break;
	case LOWDOWN_LISTITEM:
		if (n->parent == NULL ||
		    n->parent->type == LOWDOWN_DEFINITION_DATA)
			break;

		/* Don't print list item prefix after first. */

		if (emit) {
			if (!hbuf_puts(out, pfx_li_n.text))
				return 0;
			rndr_buf_advance(term, pfx_li_n.cols);
			break;
		}

		/* List item prefix depends upon type. */

		fl = n->rndr_list.flags;
		rndr_node_style_apply(&sinner, &sty_li_pfx);
		if (!rndr_buf_style(term, out, &sinner))
			return 0;
		pstyle = 1;

		if (fl & HLIST_FL_CHECKED)
			pfx = &pfx_uli_c1;
		else if (fl & HLIST_FL_UNCHECKED)
			pfx = &pfx_uli_nc1;
		else if (fl & HLIST_FL_UNORDERED)
			pfx = &pfx_uli_1;
		else
			pfx = &pfx_oli_1;

		if (pfx == &pfx_oli_1) {
			if (!hbuf_printf(out, "%2zu. ",
			     n->rndr_listitem.num))
				return 0;
			len = rndr_numlen(n->rndr_listitem.num);
			if (len + 2 > pfx->cols)
				len += 2;
			else
				len = pfx->cols;
		} else {
			if (pfx->text != NULL &&
			    !hbuf_puts(out, pfx->text))
				return 0;
			len = pfx->cols;
		}
		rndr_buf_advance(term, len);
		break;
	default:
		break;
	}

	if (pstyle && !rndr_buf_unstyle(term, out, &sinner))
		return 0;

	(*depth)++;
	return 1;
}

/*
 * Like rndr_buf_startwords(), but at the start of a line.  (Unlike
 * rndr_buf_endline(), which calls rndr_buf_endwords(), this does not
 * call rndr_buf_startwords().)  This also outputs all line prefixes of
 * the block context.  Return zero on failure (memory), non-zero on
 * success.
 */
static int
rndr_buf_startline(struct term *term, struct lowdown_buf *out,
	const struct lowdown_node *n, const struct sty *osty)
{
	struct sty	 s;
	size_t		 depth = 0;

	assert(term->last_blank);
	assert(term->col == 0);

	memset(&s, 0, sizeof(struct sty));
	if (!rndr_buf_startline_prefixes(term, &s, n, out, &depth))
		return 0;

	/*
	 * If an OSC8 hyperlink should be printed, do it now (it doesn't
	 * matter where this appears in relation to other styling).
	 */

	if (term->in_link != NULL &&
	    !rndr_buf_osc8_open(term, out, term->in_link))
		return 0;

	if (osty != NULL)
		rndr_node_style_apply(&s, osty);
	return rndr_buf_style(term, out, &s);
}

/*
 * Output optional number of newlines before or after content.
 * Return zero on failure, non-zero on success.
 */
static int
rndr_buf_vspace(struct term *term, struct lowdown_buf *out,
	const struct lowdown_node *n, size_t sz)
{
	const struct lowdown_node	*prev;

	if (term->last_blank == -1)
		return 1;

	prev = n->parent == NULL ? NULL :
		TAILQ_PREV(n, lowdown_nodeq, entries);

	assert(sz > 0);
	while ((size_t)term->last_blank < sz) {
		if (term->col || prev == NULL) {
			if (!HBUF_PUTSL(out, "\n"))
				return 0;
		} else {
			if (!rndr_buf_startline
			    (term, out, n->parent, NULL))
				return 0;
			if (!rndr_buf_endline
			    (term, out, n->parent, NULL))
				return 0;
		}
		term->last_blank++;
		term->col = 0;
	}
	return 1;
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
 * Return zero on failure, non-zero on success.
 */
static int
rndr_buf_startwords(struct term *term, struct lowdown_buf *out,
	const struct lowdown_node *n, const struct sty *osty)
{
	struct sty	 s;

	/*
	 * If an OSC8 hyperlink should be printed, do it now (it doesn't
	 * matter where this appears in relation to other styling).
	 */

	if (term->in_link != NULL &&
	    !rndr_buf_osc8_open(term, out, term->in_link))
		return 0;

	assert(!term->last_blank);
	assert(term->col > 0);

	memset(&s, 0, sizeof(struct sty));
	rndr_buf_startwords_style(n, &s);
	if (osty != NULL)
		rndr_node_style_apply(&s, osty);
	return rndr_buf_style(term, out, &s);
}

/*
 * Return zero on failure, non-zero on success.
 */
static int
rndr_buf_literal(struct term *term, struct lowdown_buf *out,
    const struct lowdown_node *n, const struct lowdown_buf *in,
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
		if (!rndr_buf_startline(term, out, n, osty))
			return 0;

		/*
		 * No need to record the column width here because we're
		 * going to reset to zero anyway.
		 */

		if (rndr_escape(term, out, start, len) < 0)
			return 0;
		rndr_buf_advance(term, len);
		if (!rndr_buf_endline(term, out, n, osty))
			return 0;
	}

	return 1;
}

/*
 * Emit text in "in" the current line with output "out".
 * Use "n" and its ancestry to determine our context.
 * Return zero on failure, non-zero on success.
 */
static int
rndr_buf(struct term *term, struct lowdown_buf *out,
	const struct lowdown_node *n, const struct lowdown_buf *in,
	const struct sty *osty)
{
	size_t				 i = 0, len, cols, nlen;
	ssize_t				 ret;
	int				 needspace, hasspace,
					 begin = 1, end = 0;
	const char			*start;
	const struct lowdown_node	*nn;

	for (nn = n; nn != NULL; nn = nn->parent)
		if (nn->type == LOWDOWN_BLOCKCODE ||
	  	    nn->type == LOWDOWN_BLOCKHTML)
			return rndr_buf_literal(term, out, n, in, osty);

	while (i < in->size) {
		/*
		 * Whether we need a space (word begins with space) and
		 * have a space (current printed content ends with one).
		 */

		needspace = isspace((unsigned char)in->data[i]);
		hasspace = out->size > 0 &&
			isspace((unsigned char)out->data[out->size - 1]);

		/* Skip to next word, then see how long the word is. */

		while (i < in->size &&
		       isspace((unsigned char)in->data[i]))
			i++;
		start = &in->data[i];
		while (i < in->size &&
		       !isspace((unsigned char)in->data[i]))
			i++;

		/* Get length and adjusted length (includes space). */

		len = &in->data[i] - start;
		nlen = len + (needspace ? 1 : 0);

		/*
		 * If we cross our maximum width and are preceded by a
		 * space, then break.
		 * (Leaving out the check for a space will cause
		 * adjacent text or punctuation to have a preceding
		 * newline.)
		 * This will also unset the current style.
		 */

		if ((needspace || hasspace) &&
		    term->col > 0 &&
		    term->col + nlen >= term->width) {
			if (!rndr_buf_endline(term, out, n, osty))
				return 0;
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
			if (!rndr_buf_startline(term, out, n, osty))
				return 0;
			begin = 0;
			end = 1;
		} else if (!term->last_blank) {
			if (begin && len) {
				if (!rndr_buf_startwords
				    (term, out, n, osty))
					return 0;
				begin = 0;
				end = 1;
			}
			if (needspace) {
				if (!HBUF_PUTSL(out, " "))
					return 0;
				rndr_buf_advance(term, 1);
			}
		}

		/* Emit the word itself. */

		if ((ret = rndr_escape(term, out, start, len)) < 0)
			return 0;
		cols = ret;
		rndr_buf_advance(term, cols);
	}

	if (end) {
		assert(begin == 0);
		if (!rndr_buf_endwords(term, out, n, osty))
			return 0;
	}

	return 1;
}

/*
 * Output the unicode entry "val", which must be strictly greater than
 * zero, as a UTF-8 sequence.
 * This does no error checking.
 * Return zero on failure (memory), non-zero on success.
 */
static int
rndr_entity(struct lowdown_buf *buf, int32_t val)
{

	assert(val > 0);

	if (val < 0x80)
		return hbuf_putc(buf, val);

       	if (val < 0x800)
		return hbuf_putc(buf, 192 + val / 64) &&
			hbuf_putc(buf, 128 + val % 64);

	if (val - 0xd800u < 0x800)
		return 1;

       	if (val < 0x10000)
		return hbuf_putc(buf, 224 + val / 4096) &&
			hbuf_putc(buf, 128 + val / 64 % 64) &&
			hbuf_putc(buf, 128 + val % 64);

       	if (val < 0x110000)
		return hbuf_putc(buf, 240 + val / 262144) &&
			hbuf_putc(buf, 128 + val / 4096 % 64) &&
			hbuf_putc(buf, 128 + val / 64 % 64) &&
			hbuf_putc(buf, 128 + val % 64);

	return 1;
}

/*
 * Render a horizontal rule by repeating the character(s) in "hr" until
 * the full screen width has been met.  This presumes that the caller
 * has started a new line.  The style may be NULL.  A zero-length rule
 * is silently ignored.
 */
static int
rndr_hrule(struct term *st, struct lowdown_buf *ob, const char *hr,
    const struct lowdown_node *n, const struct sty *sty)
{
	ssize_t	 ssz;
	size_t	 sz, i;

	if ((sz = strlen(hr)) == 0)
		return 1;
	if ((ssz = rndr_mbswidth(st, hr, sz)) < 0)
		return 0;
	if (ssz == 0)
		return 1;
	hbuf_truncate(st->tmp);
	for (i = 0; i + ssz <= st->width; i += ssz)
		if (!hbuf_puts(st->tmp, hr))
			return 0;
	return rndr_buf_literal(st, ob, n, st->tmp, sty);
}

static int
rndr_image(struct term *st, struct lowdown_buf *ob,
    const struct lowdown_node *n)
{
	const struct lowdown_node	*nn, *link = NULL;

	for (nn = n->parent; nn != NULL; nn = nn->parent)
		if (nn->type == LOWDOWN_LINK) {
			link = nn;
			break;
		}

	/*
	 * This is a bit more complicated than LOWDOWN_LINK
	 * because the image "alt" is in a buffer and not
	 * arranged as child nodes.  Begin with the image-left
	 * bracketing.
	 */

	hbuf_truncate(st->tmp);
	if (!hbuf_puts(st->tmp, ifx_imgbox_left) ||
	    !rndr_buf(st, ob, n, st->tmp, &sty_imgbox))
		return 0;

	if (link != NULL)
		st->in_link = link;
	if (!rndr_buf(st, ob, n, &n->rndr_image.alt, &sty_linkalt))
		return 0;
	st->in_link = n;

	/* If omitting the link, right-bracket and bail. */

	if ((st->opts & LOWDOWN_TERM_NOLINK) ||
	    ((st->opts & LOWDOWN_TERM_NORELLINK) &&
	     hbuf_isrellink(&n->rndr_image.link))) {
		hbuf_truncate(st->tmp);
		if (!hbuf_puts(st->tmp, ifx_imgbox_right) ||
		    !rndr_buf(st, ob, n, st->tmp, &sty_imgbox))
			return 0;
		return 1;
	}

	/* Separate between text and link address. */

	hbuf_truncate(st->tmp);
	if (!hbuf_puts(st->tmp, ifx_imgbox_sep) ||
	    !rndr_buf(st, ob, n, st->tmp, &sty_imgbox))
		return 0;

	/* Format link address. */

	if (st->opts & LOWDOWN_TERM_SHORTLINK) {
		if (!hbuf_shortlink
		    (st->tmp, &n->rndr_image.link))
			return 0;
		if (!rndr_buf(st, ob, n, st->tmp, &sty_imgurl))
			return 0;
	} else
		if (!rndr_buf(st, ob, n, &n->rndr_image.link,
		    &sty_imgurl))
			return 0;

	/* Right-bracket and end. */

	hbuf_truncate(st->tmp);
	if (!hbuf_puts(st->tmp, ifx_imgbox_right) ||
	    !rndr_buf(st, ob, n, st->tmp, &sty_imgbox))
		return 0;

	return 1;
}

/*
 * Adjust the stack of current nodes we're looking at.
 */
static int
rndr_stackpos_init(struct term *st, const struct lowdown_node *n)
{
	void	*pp;

	if (st->stackpos >= st->stackmax) {
		st->stackmax += 256;
		pp = reallocarray(st->stack,
			st->stackmax, sizeof(struct tstack));
		if (pp == NULL)
			return 0;
		st->stack = pp;
	}

	memset(&st->stack[st->stackpos], 0, sizeof(struct tstack));
	st->stack[st->stackpos].n = n;
	return 1;
}

/*
 * Return zero on failure (memory), non-zero on success.
 */
static int
rndr_table(struct lowdown_buf *ob, struct term *st,
	const struct lowdown_node *n)
{
	size_t				*widths = NULL;
	const struct lowdown_node	*row, *top, *cell;
	struct lowdown_buf		*celltmp = NULL, *rowtmp = NULL;
	size_t				 col, i, j, maxcol, sz, footsz;
	ssize_t			 	 last_blank;
	unsigned int			 flags;
	int				 rc = 0;

	assert(n->type == LOWDOWN_TABLE_BLOCK);

	widths = calloc(n->rndr_table.columns, sizeof(size_t));
	if (widths == NULL)
		goto out;

	if ((rowtmp = hbuf_new(128)) == NULL ||
	    (celltmp = hbuf_new(128)) == NULL)
		goto out;

	/*
	 * Begin by counting the number of printable columns in each
	 * column in each row.  We don't want to collect additional
	 * footnotes, as we're going to do so in the next iteration, and
	 * keep the current size (which will otherwise advance).
	 */

	assert(!st->footoff);
	st->footoff = 1;
	footsz = st->footsz;

	TAILQ_FOREACH(top, &n->children, entries) {
		assert(top->type == LOWDOWN_TABLE_HEADER ||
			top->type == LOWDOWN_TABLE_BODY);
		TAILQ_FOREACH(row, &top->children, entries)
			TAILQ_FOREACH(cell, &row->children, entries) {
				i = cell->rndr_table_cell.col;
				assert(i < n->rndr_table.columns);
				hbuf_truncate(celltmp);

				/*
				 * Simulate that we're starting within
				 * the line by unsetting last_blank,
				 * having a non-zero column, and an
				 * infinite maximum column to prevent
				 * line wrapping.
				 */

				maxcol = st->width;
				last_blank = st->last_blank;
				col = st->col;

				st->last_blank = 0;
				st->width = SIZE_MAX;
				st->col = 1;
				if (!rndr(celltmp, st, cell))
					goto out;
				if (widths[i] < st->col)
					widths[i] = st->col;
				st->last_blank = last_blank;
				st->col = col;
				st->width = maxcol;
			}
	}

	/* Restore footnotes. */

	st->footsz = footsz;
	assert(st->footoff);
	st->footoff = 0;

	/* Now actually print, row-by-row into the output. */

	TAILQ_FOREACH(top, &n->children, entries) {
		assert(top->type == LOWDOWN_TABLE_HEADER ||
			top->type == LOWDOWN_TABLE_BODY);
		TAILQ_FOREACH(row, &top->children, entries) {
			hbuf_truncate(rowtmp);
			TAILQ_FOREACH(cell, &row->children, entries) {
				i = cell->rndr_table_cell.col;
				hbuf_truncate(celltmp);
				maxcol = st->width;
				last_blank = st->last_blank;
				col = st->col;

				st->last_blank = 0;
				st->width = SIZE_MAX;
				st->col = 1;
				if (!rndr(celltmp, st, cell))
					goto out;
				assert(widths[i] >= st->col);
				sz = widths[i] - st->col;

				/*
				 * Alignment is either beginning,
				 * ending, or splitting the remaining
				 * spaces around the word.
				 * Be careful about uneven splitting in
				 * the case of centre.
				 */

				flags = cell->rndr_table_cell.flags &
					HTBL_FL_ALIGNMASK;
				if (flags == HTBL_FL_ALIGN_RIGHT)
					for (j = 0; j < sz; j++)
						if (!HBUF_PUTSL(rowtmp, " "))
							goto out;
				if (flags == HTBL_FL_ALIGN_CENTER)
					for (j = 0; j < sz / 2; j++)
						if (!HBUF_PUTSL(rowtmp, " "))
							goto out;
				if (!hbuf_putb(rowtmp, celltmp))
					goto out;
				if (flags == 0 ||
				    flags == HTBL_FL_ALIGN_LEFT)
					for (j = 0; j < sz; j++)
						if (!HBUF_PUTSL(rowtmp, " "))
							goto out;
				if (flags == HTBL_FL_ALIGN_CENTER) {
					sz = (sz % 2) ?
						(sz / 2) + 1 : (sz / 2);
					for (j = 0; j < sz; j++)
						if (!HBUF_PUTSL(rowtmp, " "))
							goto out;
				}

				st->last_blank = last_blank;
				st->col = col;
				st->width = maxcol;

				if (TAILQ_NEXT(cell, entries) == NULL)
					continue;

				if (!HBUF_PUTSL(rowtmp, " ") ||
				    !rndr_buf_style(st, rowtmp, &sty_tbl) ||
				    !hbuf_puts(rowtmp, ifx_tbl_col) ||
				    !rndr_buf_unstyle(st, rowtmp, &sty_tbl) ||
				    !HBUF_PUTSL(rowtmp, " "))
					goto out;
			}

			/*
			 * Some magic here.
			 * First, emulate rndr() by setting the
			 * stackpos to the table, which is required for
			 * checking the line start.
			 * Then directly print, as we've already escaped
			 * all characters, and have embedded escapes of
			 * our own.  Then end the line.
			 */

			st->stackpos++;
			if (!rndr_stackpos_init(st, n))
				goto out;
			if (!rndr_buf_startline(st, ob, n, NULL))
				goto out;
			if (!hbuf_putb(ob, rowtmp))
				goto out;
			rndr_buf_advance(st, 1);
			if (!rndr_buf_endline(st, ob, n, NULL))
				goto out;
			if (!rndr_buf_vspace(st, ob, n, 1))
				goto out;
			st->stackpos--;
		}

		if (top->type == LOWDOWN_TABLE_HEADER) {
			st->stackpos++;
			if (!rndr_stackpos_init(st, n))
				goto out;
			if (!rndr_buf_startline(st, ob, n, &sty_tbl))
				goto out;

			/*
			 * Output the row line.  This consists of:
			 *
			 *   inter    padding
			 *       |    | |
			 *       |    | |
			 *   ----+-----+-----
			 *   xyz   xyz   xyz
			 *   |     |
			 *   content
			 *
			 * So starting with the content, ending with a
			 * padding of one byte (encompassed in the
			 * width), the inter mark or nothing if at the
			 * end, then another padding or nothing if at
			 * the end.
			 */
			for (i = 0; i < n->rndr_table.columns; i++) {
				/* Pre-padding. */
				if (i > 0 && !hbuf_puts(ob, ifx_tbl_row))
					goto out;
				/* Content and post-padding. */
				for (j = 0; j < widths[i]; j++)
					if (!hbuf_puts(ob, ifx_tbl_row))
						goto out;
				/* Inter. */
				if (i < n->rndr_table.columns - 1 &&
				    !hbuf_puts(ob, ifx_tbl_mcol))
					goto out;
			}
			rndr_buf_advance(st, 1);
			if (!rndr_buf_endline(st, ob, n, &sty_tbl))
				goto out;
			if (!rndr_buf_vspace(st, ob, n, 1))
				goto out;
			st->stackpos--;
		}
	}

	rc = 1;
out:
	hbuf_free(celltmp);
	hbuf_free(rowtmp);
	free(widths);
	return rc;
}

/*
 * Output a title-value pair.  If "multi" is specified, break up into
 * multiple title-value lines.
 *
 * Return zero on failure (memory), non-zero otherwise.
 */
static int
rndr_doc_header_meta(struct lowdown_buf *ob, struct term *st,
	const struct lowdown_node *n, const char *title,
	const char *value, int multi)
{
	const char	*start, *end;

	for (start = value; *start != '\0';) {
		if (multi) {
			for (end = start + 1; *end != '\0'; end++)
				if (isspace((unsigned char)end[0]) &&
				    isspace((unsigned char)end[1]))
					break;
		} else
			end = start + strlen(start);

		if (!rndr_buf_vspace(st, ob, n, 1))
			return 0;
		hbuf_truncate(st->tmp);
		if (!hbuf_puts(st->tmp, title) ||
		    !rndr_buf(st, ob, n, st->tmp, &sty_meta_key))
			return 0;
		hbuf_truncate(st->tmp);
		if (!hbuf_puts(st->tmp, ifx_meta_key) ||
		    !rndr_buf(st, ob, n, st->tmp, &sty_meta_key))
			return 0;
		hbuf_truncate(st->tmp);
		if (!hbuf_put(st->tmp, start, (size_t)(end - start)) ||
		    !rndr_buf(st, ob, n, st->tmp, NULL))
			return 0;

		start = end;
		while (*start != '\0' && isspace((unsigned char)*start))
			start++;
	}

	return 1;
}

/*
 * Conditionally emit a document header containing the title, author,
 * and date.
 */
static int
rndr_doc_header(struct lowdown_buf *ob, struct term *st,
	const struct lowdown_node *n)
{
	const char			*title = NULL, *author = NULL,
	      				*date = NULL, *rcsdate = NULL,
					*rcsauthor = NULL;
	const struct lowdown_meta	*m;

	if (!(st->opts & LOWDOWN_STANDALONE))
		return 1;

	if (st->opts & LOWDOWN_TERM_ALL_META) {
		TAILQ_FOREACH(m, &st->metaq, entries)
			if (!rndr_doc_header_meta(ob, st, n, m->key,
			    m->value, 0))
				return 0;
		return 1;
	}

	TAILQ_FOREACH(m, &st->metaq, entries)
		if (strcasecmp(m->key, "title") == 0)
			title = m->value;
		else if (strcasecmp(m->key, "author") == 0)
			author = m->value;
		else if (strcasecmp(m->key, "date") == 0)
			date = m->value;
		else if (strcasecmp(m->key, "rcsauthor") == 0)
			rcsauthor = rcsauthor2str(m->value);
		else if (strcasecmp(m->key, "rcsdate") == 0)
			rcsdate = rcsdate2str(m->value);

	/* Overrides. */

	if (rcsdate != NULL)
		date = rcsdate;
	if (rcsauthor != NULL)
		author = rcsauthor;

	if (title != NULL &&
	    !rndr_doc_header_meta(ob, st, n, "title", title, 0))
		return 0;
	if (author != NULL &&
	    !rndr_doc_header_meta(ob, st, n, "author", author, 1))
		return 0;
	if (date != NULL &&
	    !rndr_doc_header_meta(ob, st, n, "date", date, 0))
		return 0;

	return 1;
}

static int
rndr(struct lowdown_buf *ob, struct term *st,
	const struct lowdown_node *n)
{
	const struct lowdown_node	*child, *nn, *in_link = st->in_link;
	struct lowdown_buf		*metatmp;
	void				*pp;
	size_t				 i, col, vs;
	ssize_t			 	 last_blank;
	int32_t				 entity;

	/* Current nodes we're servicing. */

	if (!rndr_stackpos_init(st, n))
		return 0;

	/*
	 * Vertical space before content.  Vertical space (>1 space) is
	 * suppressed for normal blocks when in a non-block list, as the
	 * list item handles any spacing.  Furthermore, definition list
	 * data also has its spaces suppressed because this is relegated
	 * to the title.  The root gets the vertical margin as well.
	 */

	vs = 0;
	switch (n->type) {
	case LOWDOWN_ROOT:
		for (i = 0; i < st->vmargin; i++)
			if (!HBUF_PUTSL(ob, "\n"))
				return 0;
		st->last_blank = -1;
		break;
	case LOWDOWN_BLOCKCODE:
	case LOWDOWN_BLOCKHTML:
	case LOWDOWN_BLOCKQUOTE:
	case LOWDOWN_DEFINITION:
	case LOWDOWN_DEFINITION_TITLE:
	case LOWDOWN_HEADER:
	case LOWDOWN_LIST:
	case LOWDOWN_TABLE_BLOCK:
	case LOWDOWN_PARAGRAPH:
		vs = 2;
		for (nn = n->parent; nn != NULL; nn = nn->parent) {
			if (nn->type != LOWDOWN_LISTITEM)
				continue;
			vs = (nn->rndr_listitem.flags & HLIST_FL_BLOCK) ? 2 : 1;
			break;
		}
		break;
	case LOWDOWN_MATH_BLOCK:
		vs = n->rndr_math.blockmode ? 1 : 0;
		break;
	case LOWDOWN_DEFINITION_DATA:
	case LOWDOWN_LINEBREAK:
		vs = 1;
		break;
	case LOWDOWN_HRULE:
		vs = 2;
		break;
	case LOWDOWN_LISTITEM:
		vs = 1;
		if (n->rndr_listitem.flags & HLIST_FL_BLOCK) {
			for (nn = n->parent; nn != NULL; nn = nn->parent)
				if (nn->type == LOWDOWN_LISTITEM ||
				    nn->type == LOWDOWN_DEFINITION_DATA)
					break;
			vs = nn == NULL ? 2 : 1;
		}
		break;
	default:
		break;
	}

	if (vs > 0 && !rndr_buf_vspace(st, ob, n, vs))
		return 0;

	/* Output leading content. */

	switch (n->type) {
	case LOWDOWN_IMAGE:
	case LOWDOWN_LINK:
	case LOWDOWN_LINK_AUTO:
		st->in_link = n;
		break;
	case LOWDOWN_SUPERSCRIPT:
		/*
		 * Output the superscript character.
		 */
		hbuf_truncate(st->tmp);
		if (!hbuf_puts(st->tmp, ifx_super) ||
		    !rndr_buf(st, ob, n, st->tmp, NULL))
			return 0;
		break;
	default:
		break;
	}

	/* Descend into children. */

	switch (n->type) {
	case LOWDOWN_FOOTNOTE:
		if (st->footoff) {
			st->footsz++;
			break;
		}
		last_blank = st->last_blank;
		st->last_blank = -1;
		col = st->col;
		st->col = 0;
		if ((metatmp = hbuf_new(128)) == NULL)
			return 0;
		TAILQ_FOREACH(child, &n->children, entries) {
			st->stackpos++;
			if (!rndr(metatmp, st, child))
				return 0;
			st->stackpos--;
		}
		st->last_blank = last_blank;
		st->col = col;
		pp = recallocarray(st->foots, st->footsz,
			st->footsz + 1, sizeof(struct lowdown_buf *));
		if (pp == NULL)
			return 0;
		st->foots = pp;
		st->foots[st->footsz++] = metatmp;
		break;
	case LOWDOWN_TABLE_BLOCK:
		if (!rndr_table(ob, st, n))
			return 0;
		break;
	case LOWDOWN_META:
		if (lowdown_get_meta(n, &st->metaq) == NULL)
			return 0;
		break;
	default:
		TAILQ_FOREACH(child, &n->children, entries) {
			st->stackpos++;
			if (!rndr(ob, st, child))
				return 0;
			st->stackpos--;
		}
		break;
	}

	/* Output content. */

	switch (n->type) {
	case LOWDOWN_DOC_HEADER:
		if (!rndr_doc_header(ob, st, n))
			return 0;
		break;
	case LOWDOWN_HRULE:
		if (!rndr_hrule(st, ob, ifx_hrule, n, NULL))
			return 0;
		break;
	case LOWDOWN_FOOTNOTE:
		hbuf_truncate(st->tmp);
		if (!hbuf_printf(st->tmp, "%s%zu%s", ifx_fref_left,
		    st->footsz, ifx_fref_right))
			return 0;
		if (!rndr_buf(st, ob, n, st->tmp, &sty_fref))
			return 0;
		break;
	case LOWDOWN_RAW_HTML:
		if (!rndr_buf(st, ob, n, &n->rndr_raw_html.text, NULL))
			return 0;
		break;
	case LOWDOWN_MATH_BLOCK:
		if (!rndr_buf(st, ob, n, &n->rndr_math.text, NULL))
			return 0;
		break;
	case LOWDOWN_ENTITY:
		entity = entity_find_iso(&n->rndr_entity.text);
		if (entity > 0) {
			hbuf_truncate(st->tmp);
			if (!rndr_entity(st->tmp, entity))
				return 0;
			if (!rndr_buf(st, ob, n, st->tmp, NULL))
				return 0;
		} else {
			if (!rndr_buf(st, ob, n,
			     &n->rndr_entity.text, &sty_bad_ent))
				return 0;
		}
		break;
	case LOWDOWN_BLOCKCODE:
		if (!rndr_buf(st, ob, n, &n->rndr_blockcode.text, NULL))
			return 0;
		break;
	case LOWDOWN_BLOCKHTML:
		if (!rndr_buf(st, ob, n, &n->rndr_blockhtml.text, NULL))
			return 0;
		break;
	case LOWDOWN_CODESPAN:
		if (!rndr_buf(st, ob, n, &n->rndr_codespan.text, NULL))
			return 0;
		break;
	case LOWDOWN_LINK_AUTO:
		if (st->opts & LOWDOWN_TERM_SHORTLINK) {
			hbuf_truncate(st->tmp);
			if (!hbuf_shortlink
			    (st->tmp, &n->rndr_autolink.link))
				return 0;
			if (!rndr_buf(st, ob, n, st->tmp, NULL))
				return 0;
		} else {
			if (!rndr_buf(st, ob, n,
			     &n->rndr_autolink.link, NULL))
				return 0;
		}
		break;
	case LOWDOWN_LINK:
		/*
		 * The child content of the link has already been
		 * produced to the output buffer.  Inhibit printing the
		 * link address if requested for all links or if a
		 * relative address and requested only for those.
		 */

		if ((st->opts & LOWDOWN_TERM_NOLINK) ||
		    ((st->opts & LOWDOWN_TERM_NORELLINK) &&
		     hbuf_isrellink(&n->rndr_link.link)))
			break;

		/* Separate between text and link address. */

		hbuf_truncate(st->tmp);
		if (!hbuf_puts(st->tmp, ifx_link_sep) ||
		    !rndr_buf(st, ob, n, st->tmp, NULL))
			return 0;

		/* Format the link address. */

		if (st->opts & LOWDOWN_TERM_SHORTLINK) {
			hbuf_truncate(st->tmp);
			if (!hbuf_shortlink
			    (st->tmp, &n->rndr_link.link))
				return 0;
			if (!rndr_buf(st, ob, n, st->tmp, NULL))
				return 0;
		} else {
			if (!rndr_buf(st, ob, n,
			     &n->rndr_link.link, NULL))
				return 0;
		}
		break;
	case LOWDOWN_IMAGE:
		if (!rndr_image(st, ob, n))
			return 0;
		break;
	case LOWDOWN_NORMAL_TEXT:
		if (!rndr_buf(st, ob, n, &n->rndr_normal_text.text,
		    NULL))
			return 0;
		break;
	default:
		break;
	}

	switch (n->type) {
	case LOWDOWN_IMAGE:
	case LOWDOWN_LINK:
	case LOWDOWN_LINK_AUTO:
		st->in_link = in_link;
		break;
	case LOWDOWN_ROOT:
		/*
		 * If there are footnotes, begin by offsetting with
		 * vertical space.  Then, if there's a footnote block
		 * header, output that followed by vertical space.
		 * Lastly, output the footnotes themselves.
		 */

		if (st->footsz && !rndr_buf_vspace(st, ob, n, 2))
			return 0;
		if (st->footsz) {
			if (!rndr_hrule(st, ob, ifx_foot, n, &sty_foot))
				return 0;
			if (!rndr_buf_vspace(st, ob, n, 2))
				return 0;
		}
		for (i = 0; i < st->footsz; i++)
			if (!hbuf_putb(ob, st->foots[i]) ||
			    !HBUF_PUTSL(ob, "\n"))
				return 0;
		if (!rndr_buf_vspace(st, ob, n, 1))
			return 0;

		/* Strip trailing newlines but for the vmargin. */

		while (ob->size && ob->data[ob->size - 1] == '\n')
			ob->size--;
		if (!HBUF_PUTSL(ob, "\n"))
			return 0;
		for (i = 0; i < st->vmargin; i++)
			if (!HBUF_PUTSL(ob, "\n"))
				return 0;
		break;
	default:
		break;
	}

	return 1;
}

int
lowdown_term_rndr(struct lowdown_buf *ob,
	void *arg, const struct lowdown_node *n)
{
	struct term	*st = arg;
	int		 rc;

	TAILQ_INIT(&st->metaq);
	st->stackpos = 0;
	st->in_link = NULL;
	rc = rndr(ob, st, n);
	rndr_free_footnotes(st);
	lowdown_metaq_free(&st->metaq);
	return rc;
}

void *
lowdown_term_new(const struct lowdown_opts *opts)
{
	struct term	*st;

	if ((st = calloc(1, sizeof(struct term))) == NULL)
		return NULL;

	if (opts != NULL) {
		/*
		 * Compute the width of the content pre-padding.  If
		 * zero, limit to 80 or the number of terminal columns.
		 * Otherwise, truncate to the number of columns.
		 */

		if (opts->term.width == 0) {
			if ((st->width = opts->term.cols) > 80)
				st->width = 80;
		} else if (opts->term.width > opts->term.cols) {
			st->width = opts->term.cols;
		} else
			st->width = opts->term.width;

		/*
		 * Compute the horizontal margin: either as given or, if
		 * centred, computed from the content width.
		 */

		if (opts->term.centre && st->width < opts->term.cols)
			st->hmargin = (opts->term.cols - st->width) / 2;
		else
			st->hmargin = opts->term.hmargin;

		st->hpadding = opts->term.hpadding;
		st->vmargin = opts->term.vmargin;
		st->opts = opts->oflags;
	} else {
		st->width = 80;
		st->hpadding = 4;
	}

	if (st->hpadding >= st->width)
		st->width = 1;
	else
		st->width -= st->hpadding;

	if ((st->tmp = hbuf_new(32)) == NULL) {
		free(st);
		return NULL;
	}
	return st;
}

void
lowdown_term_free(void *arg)
{
	struct term	*st = arg;

	if (st == NULL)
		return;

	hbuf_free(st->tmp);
	free(st->buf);
	free(st->stack);
	free(st);
}
