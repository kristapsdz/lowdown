/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"
#include "roff.h"

/*
 * Expression for whether this node is after a header.  This is
 * important in mdoc(7) to see if leading paragraphs would be
 * suppressed.
 */
#define NODE_AFTER_HEAD(_n) \
	(TAILQ_PREV((_n), lowdown_nodeq, entries) != NULL && \
	 TAILQ_PREV((_n), lowdown_nodeq, entries)->type == LOWDOWN_HEADER)
/*
 * Return a static buffer with the colour name describing the given
 * BFONT_xxx colour (e.g., BFONT_BLUE).  If the colour could not be
 * looked up, this just returns "default", which is the device's default
 * colour according to the groff info page.
 */
static const char *
nstate_colour_buf(unsigned int ft)
{
	static char 	 fonts[10];

	fonts[0] = '\0';
	if (ft == BFONT_BLUE)
		strlcat(fonts, "blue", sizeof(fonts));
	else if (ft == BFONT_RED)
		strlcat(fonts, "red", sizeof(fonts));
	else
		strlcat(fonts, "default", sizeof(fonts));
	return fonts;
}

/*
 * For compatibility with traditional troff, return non-block font code
 * using the correct sequence of \fX, \f(xx, and \f[xxx].
 */
static int
nstate_font(const struct nroff *st, struct lowdown_buf *ob,
    unsigned int ft, int enclose)
{
	const char	*font = NULL;
	char		 fonts[3];
	char		*cp = fonts;
	size_t		 sz;

	if (ft & BFONT_FIXED) {
		if ((ft & BFONT_BOLD) && (ft & BFONT_ITALIC))
			font = st->cbi;
		else if (ft & BFONT_BOLD)
			font = st->cb;
		else if (ft & BFONT_ITALIC)
			font = st->ci;
		else
			font = st->cr;
	} else {
		font = cp = fonts;
		if (ft & BFONT_BOLD)
			(*cp++) = 'B';
		if (ft & BFONT_ITALIC)
			(*cp++) = 'I';
		if (ft == 0)
			(*cp++) = 'R';
		*cp = '\0';
	}

	assert(font != NULL);
	sz = strlen(font);
	assert(sz > 0);

	if (!enclose || sz == 1)
		return hbuf_puts(ob, font);

	if (sz > 2)
		return HBUF_PUTSL(ob, "[") &&
		    hbuf_puts(ob, font) && HBUF_PUTSL(ob, "]");

	return HBUF_PUTSL(ob, "(") && hbuf_puts(ob, font);
}

/*
 * Add a colour change into the token queue.  If "close" is set, the
 * colour reverts to standard; otherwise, it's set according to whether
 * it's an insertion or deletion ("chng").  Returns zero on failure
 * (memory), non-zero on success.  See bqueue_flush().
 */
static int
bqueue_colour(struct bnodeq *bq, enum lowdown_chng chng, int close)
{
	struct bnode	*bn;

	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(bq, bn, entries);
	bn->scope = BSCOPE_COLOUR;
	bn->close = close;
	bn->colour = close ? 0 :
		chng == LOWDOWN_CHNG_INSERT ? 
		BFONT_BLUE : BFONT_RED;
	return 1;
}

/* 
 * Like bqueue_font(), but also pushing to or popping from the stack of
 * open fonts.
 */
int
bqueue_font_mod(struct nroff *st, struct bnodeq *bq, int close,
    enum nfont font)
{
	struct bnode	*bn;

	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return 0;

	if (close) {
		assert(st->fonts[font] > 0);
		st->fonts[font]--;
	} else
		st->fonts[font]++;

	TAILQ_INSERT_TAIL(bq, bn, entries);
	bn->scope = BSCOPE_FONT;
	bn->close = close;
	if (st->fonts[NFONT_FIXED])
		bn->font |= BFONT_FIXED;
	if (st->fonts[NFONT_BOLD])
		bn->font |= BFONT_BOLD;
	if (st->fonts[NFONT_ITALIC])
		bn->font |= BFONT_ITALIC;
	return 1;
}

/* 
 * Add zero or more font changes into the font queue.  If "close" is
 * set, the font reverts to the standard.  Returns zero on failure
 * (memory), non-zero on success.  See bqueue_flush().
 */
int
bqueue_font(const struct nroff *st, struct bnodeq *bq, int close)
{
	struct bnode	*bn;

	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(bq, bn, entries);
	bn->scope = BSCOPE_FONT;
	bn->close = close;
	if (st->fonts[NFONT_FIXED])
		bn->font |= BFONT_FIXED;
	if (st->fonts[NFONT_BOLD])
		bn->font |= BFONT_BOLD;
	if (st->fonts[NFONT_ITALIC])
		bn->font |= BFONT_ITALIC;
	return 1;
}

/*
 * Add a node with the given scope to the token queue.  If "text" is not
 * NULL, it's added as a safe (nbuf) macro name or data, depending on
 * the scope.  Returns the node on success or NULL on failure (memory).
 * On success, the node has already been appended to the queue.  See
 * bqueue_flush().
 */
static struct bnode *
bqueue_node(struct bnodeq *bq, enum bscope scope, const char *text)
{
	struct bnode	*bn;

	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return NULL;
	bn->scope = scope;
	if (text != NULL && (bn->nbuf = strdup(text)) == NULL) {
		free(bn);
		return NULL;
	}
	TAILQ_INSERT_TAIL(bq, bn, entries);
	return bn;
}

/*
 * Add a span with optional safe text to the token queue and return the
 * allocated node, returning NULL on failure (memory).  On success, the
 * node has already been appended to the queue.  See bqueue_flush().
 */
struct bnode *
bqueue_span(struct bnodeq *bq, const char *text)
{

	return bqueue_node(bq, BSCOPE_SPAN, text);
}

/*
 * Like bqueue_span(), but accepting the "nbuf" option.  If failure
 * occurs, the "nbuf" is freed.  If "nbuf" is NULL, returns failure.
 */
struct bnode *
bqueue_spann(struct bnodeq *bq, char *nbuf)
{
	struct bnode	*bn;

	if (nbuf == NULL)
		return NULL;

	if ((bn = bqueue_node(bq, BSCOPE_SPAN, NULL)) == NULL) {
		free(nbuf);
		return NULL;
	}

	bn->nbuf = nbuf;
	return bn;
}

/*
 * Like bqueue_span(), but from variable-length arguments.
 */
struct bnode *
bqueue_spanv(struct bnodeq *bq, const char *fmt, ...)
{
	struct bnode	*bn;
	va_list		 ap;
	int		 rc;

	if ((bn = bqueue_span(bq, NULL)) == NULL)
		return NULL;

	va_start(ap, fmt);
	rc = vasprintf(&bn->nbuf, fmt, ap);
	va_end(ap);

	if (rc == -1) {
		bn->nbuf = NULL;
		return NULL;
	}
	return bn;
}

/*
 * Add a block with optional safe macro name to the token queue and
 * return the allocated node, returning NULL on failure (memory).  On
 * success, the node has already been appended to the queue.  See
 * bqueue_flush().
 */
struct bnode *
bqueue_block(struct bnodeq *bq, const char *text)
{

	return bqueue_node(bq, BSCOPE_BLOCK, text);
}

/*
 * Add a semiblock with optional safe macro name to the token queue and
 * return the allocated node, returning NULL on failure (memory).  On
 * success, the node has already been appended to the queue.  See
 * bqueue_flush().
 */
struct bnode *
bqueue_sblock(struct bnodeq *bq, const char *text)
{

	return bqueue_node(bq, BSCOPE_SEMI, text);
}

/*
 * Like bqueue_sblock(), but accepting the "nargs" option.  If failure
 * occurs, the "nargs" is freed.  If "nargs" is NULL, returns failure.
 */
struct bnode *
bqueue_sblockn(struct bnodeq *bq, const char *text, char *nargs)
{
	struct bnode	*bn;

	if (nargs == NULL)
		return NULL;

	if ((bn = bqueue_sblock(bq, text)) == NULL) {
		free(nargs);
		return NULL;
	}
	bn->nargs = nargs;
	return bn;
}

/*
 * Like bqueue_block(), but accepting the "nargs" option.  If failure
 * occurs, the "nargs" is freed.  If "nargs" is NULL, returns failure.
 */
struct bnode *
bqueue_blockn(struct bnodeq *bq, const char *text, char *nargs)
{
	struct bnode	*bn;

	if (nargs == NULL)
		return NULL;

	if ((bn = bqueue_block(bq, text)) == NULL) {
		free(nargs);
		return NULL;
	}
	bn->nargs = nargs;
	return bn;
}

/*
 * Like bqueue_block(), but accepting variable arguments for the "nargs"
 * option.
 */
struct bnode *
bqueue_blocknv(struct bnodeq *bq, const char *text, const char *fmt, ...)
{
	struct bnode	*bn;
	va_list		 ap;
	int		 rc;

	if ((bn = bqueue_block(bq, text)) == NULL)
		return NULL;

	va_start(ap, fmt);
	rc = vasprintf(&bn->nargs, fmt, ap);
	va_end(ap);
	if (rc == -1) {
		bn->nargs = NULL;
		return NULL;
	}
	return bn;
}

/*
 * Free individual node.  Safe to call with NULL node.
 */
static void
bnode_free(struct bnode *bn)
{

	if (bn != NULL) {
		free(bn->args);
		free(bn->nargs);
		free(bn->nbuf);
		free(bn->buf);
		free(bn);
	}
}

/*
 * Free node queue.  Safe to call with a NULL queue.  Does not free the
 * queue pointer itself.
 */
void
bqueue_free(struct bnodeq *bq)
{
	struct bnode	*bn;

	if (bq != NULL)
		while ((bn = TAILQ_FIRST(bq)) != NULL) {
			TAILQ_REMOVE(bq, bn, entries);
			bnode_free(bn);
		}
}

/*
 * Flush nodes from "bq" into "ob".  This does not remove any nodes from
 * "bq" (hence it being const).
 * 
 * Flushing works by serialising blocks (line-starting macros),
 * semiblocks (a block but having continuance from the last line, so
 * needing \c prior to invocation), literals and spans (text), and fonts
 * and colours (either line-starting or inline).  All of these are
 * serialised into a single buffer, which may contain multiple lines.
 *
 * This has to handle mdoc(7), man(7), and ms(7), all of which are kinda
 * different.  For example, man(7) and ms(7) don't allow macros to exist
 * in-line, while mdoc(7) does.
 *
 * This isn't meant to be rigorous, but just to get the job done.
 *
 * Input is read from "bq" and serialised into "ob".  If "linestrip" is
 * set to 1, a (usually) mdoc(7) context is assumed that's already open
 * on the line, so subsequent macros have their leading periods
 * stripped.  If it's set to "2", the same happens, but colours and
 * fonts are also stripped.
 */
int
bqueue_flush(const struct nroff *st, struct lowdown_buf *ob,
    const struct bnodeq *bq, unsigned int linestrip)
{
	const struct bnode	*bn, /* current node */
	      			*tmpbn; /* temporary node */
	const char		*cp; /* temporary array */
	int		 	 eoln, /* output eolnchar at end */
				 eomacro; /* end of macro=1, 2=nosp */
	char			 eolnchar; /* char at eoln */
	size_t			 offset = 0; /* offset in "buf" */
	size_t			 sz; /* temporary size */

	TAILQ_FOREACH(bn, bq, entries) {
		if (linestrip > 1 &&
		    ((st->type != LOWDOWN_MDOC &&
		     (bn->scope == BSCOPE_SEMI ||
		      bn->scope == BSCOPE_SEMI_CLOSE)) ||
		     bn->scope == BSCOPE_FONT))
			continue;

		eoln = eomacro = 0;

		/*
		 * The semi-block macro is a span (within text content),
		 * but has block syntax.  If there's no leading space,
		 * use "\c" to inhibit whitespace prior to the macro.
		 */

		if (st->type != LOWDOWN_MDOC &&
		    bn->scope == BSCOPE_SEMI &&
		    ob->size > 0 && 
		    ob->data[ob->size - 1] != '\n' &&
		    !hbuf_puts(ob, "\\c"))
			return 0;

		/* 
		 * Block/semi scopes start with a newline.
		 * Also have colours use their own block, as otherwise
		 * (bugs in groff?) inline colour selection after a
		 * hyperlink macro causes line breaks.
		 * Besides, having spaces around changing colour, which
		 * indicates differences, improves readability.
		 */

		if (bn->scope == BSCOPE_BLOCK ||
		    bn->scope == BSCOPE_SEMI ||
		    bn->scope == BSCOPE_SEMI_CLOSE) {
			if (linestrip) {
				if (ob->size > 0 && 
				    ob->data[ob->size - 1] != ' ' &&
				    !hbuf_puts(ob, " Ns "))
					return 0;
			} else {
				if (ob->size > 0 && 
				    ob->data[ob->size - 1] != '\n' &&
				    !hbuf_putc(ob, '\n'))
					return 0;
			}
			eoln = 1;
		}

		/* 
		 * Fonts can either be macros or inline depending upon
		 * where they set relative to a macro block.
		 */

		if (bn->scope == BSCOPE_FONT ||
		    bn->scope == BSCOPE_COLOUR) {
			tmpbn = bn->close ?
				TAILQ_PREV(bn, bnodeq, entries) :
				TAILQ_NEXT(bn, entries);
			if (tmpbn != NULL && 
			    (tmpbn->scope == BSCOPE_SEMI ||
			     tmpbn->scope == BSCOPE_SEMI_CLOSE ||
			     tmpbn->scope == BSCOPE_BLOCK)) {
				if (!linestrip &&
				    ob->size > 0 && 
				    ob->data[ob->size - 1] != '\n' &&
				    !hbuf_putc(ob, '\n'))
					return 0;
				eoln = 1;
			}
		}

		/* Print font and colour escapes. */

		if (bn->scope == BSCOPE_FONT && eoln && !linestrip) {
			if (!HBUF_PUTSL(ob, ".ft "))
				return 0;
			if (!nstate_font(st, ob, bn->font, 0))
				return 0;
		} else if (bn->scope == BSCOPE_FONT) {
			if (!HBUF_PUTSL(ob, "\\f"))
				return 0;
			if (!nstate_font(st, ob, bn->font, 1))
				return 0;
		} else if (bn->scope == BSCOPE_COLOUR && eoln && !linestrip) {
			if (!hbuf_printf(ob, ".gcolor %s", 
			    nstate_colour_buf(bn->colour)))
				return 0;
		} else if (bn->scope == BSCOPE_COLOUR) {
			if (!hbuf_printf(ob, "\\m[%s]", 
			    nstate_colour_buf(bn->colour)))
				return 0;
		}

		/* 
		 * A "tblhack" is used by a span macro to indicate
		 * that it should start its own line, but that data
		 * continues to flow after it.  This is only used in
		 * tables with T}, at this point.
		 */

		if (bn->scope == BSCOPE_SPAN && bn->tblhack &&
		    ob->size > 0 && ob->data[ob->size - 1] != '\n')
			if (!hbuf_putc(ob, '\n'))
				return 0;

		/*
		 * If we're a span, double-check to see whether we
		 * should introduce a line with an escape.
		 */

		if (bn->scope == BSCOPE_SPAN &&
		    bn->nbuf != NULL && ob->size > 0 &&
		    ob->data[ob->size - 1] == '\n' &&
		    (bn->nbuf[0] == '.' || bn->nbuf[0] == '\'') &&
		    !HBUF_PUTSL(ob, "\\&"))
			return 0;

		/* Safe data need not be escaped. */

		if (linestrip &&
		    (bn->scope == BSCOPE_BLOCK ||
		     bn->scope == BSCOPE_SEMI) && 
		    bn->nbuf != NULL && bn->nbuf[0] == '.') {
			if (!hbuf_puts(ob, bn->nbuf + 1))
				return 0;
		} else {
			if (bn->nbuf != NULL &&
			    !hbuf_puts(ob, &bn->nbuf[0]))
				return 0;
		}

		/* Unsafe data must be escaped. */

		if (bn->scope == BSCOPE_LITERAL) {
			assert(bn->buf != NULL);
			if (!lowdown_roff_esc(ob, bn->buf,
			    strlen(bn->buf), 0, 1))
				return 0;
		} else if (bn->buf != NULL) {
			/*
			 * For safe data in mdoc(7), see if the last
			 * character is opening punctuation.  Use
			 * mdoc(7)'s "Delimiters" as a guide.
			 * XXX: multiple delimiters...?
			 */
			sz = strlen(bn->buf);
			if (sz - 1 > offset &&
			    st->type == LOWDOWN_MDOC &&
		  	    (tmpbn = TAILQ_NEXT(bn, entries)) != NULL &&
			     tmpbn->scope == BSCOPE_SEMI &&
			     (bn->buf[sz - 1] == '(' ||
			      bn->buf[sz - 1] == '[')) {
				if (!lowdown_roff_esc(ob,
				    &bn->buf[offset], sz - offset - 1,
				    0, 0))
					return 0;
				if (!hbuf_printf(ob, "\n.No %c Ns",
				    bn->buf[sz - 1]))
					return 0;
			} else {
				if (!lowdown_roff_esc(ob,
				    &bn->buf[offset], sz - offset,
				    0, 0))
					return 0;
			}
		}

		/*
		 * Special handling of the semi-blocks, specifically
		 * either ".pdfhref" or ".UE", where the next text has
		 * no leading space.  In this case, we need to terminate
		 * the semi-block macro with "\c" to inhibit whitespace.
		 */

		if ((tmpbn = TAILQ_NEXT(bn, entries)) != NULL &&
		    tmpbn->scope == BSCOPE_SPAN &&
		    tmpbn->buf != NULL) {
			if (tmpbn->buf[0] != ' ' && tmpbn->buf[0] != '\n') {
				if (st->type == LOWDOWN_MS &&
				    bn->scope == BSCOPE_SEMI &&
				    !HBUF_PUTSL(ob, " -A \"\\c\""))
					return 0;
				if (st->type == LOWDOWN_MAN &&
				    bn->scope == BSCOPE_SEMI_CLOSE &&
				    !HBUF_PUTSL(ob, " \\c"))
					return 0;
				if (st->type == LOWDOWN_MDOC &&
				    (bn->scope == BSCOPE_BLOCK ||
				     bn->scope == BSCOPE_SEMI))
					eomacro = 1;
			} else {
				if (st->type == LOWDOWN_MDOC &&
				    (bn->scope == BSCOPE_BLOCK ||
				     bn->scope == BSCOPE_SEMI))
					eomacro = 2;
			}
		}

		/*
		 * FIXME: BSCOPE_SEMI_CLOSE in a font context needs to
		 * reopen the font (e.g., \fB), as it internally will
		 * unset any font context.
		 */

		/* 
		 * Macro arguments follow after space.  For links, these
		 * must all be printed on the same line.
		 */

		if (bn->nargs != NULL &&
		    (bn->scope == BSCOPE_BLOCK ||
		     bn->scope == BSCOPE_SEMI)) {
			assert(eoln);
			if (!hbuf_putc(ob, ' '))
				return 0;
			for (cp = bn->nargs; *cp != '\0'; cp++)
				if (!hbuf_putc(ob, 
				    *cp == '\n' ? ' ' : *cp))
					return 0;
		}

		if (bn->args != NULL) {
			assert(eoln);
			assert(bn->scope == BSCOPE_BLOCK ||
			    bn->scope == BSCOPE_SEMI);
			if (!hbuf_putc(ob, ' '))
				return 0;
			if (!lowdown_roff_esc(ob, bn->args,
			    strlen(bn->args), 1, 0))
				return 0;
		}

		/*
		 * If an mdoc(7) one-liner and the node after a block
		 * node is a span, we either (1) inhibit space and end
		 * the macro if there's no space after the block; or
		 * (2), simply end the macro if there is a space.
		 */

		if (linestrip && eomacro == 1)
			if (!hbuf_puts(ob, " Ns No "))
				return 0;
		if (linestrip && eomacro == 2)
			if (!hbuf_puts(ob, " No "))
				return 0;

		/*
		 * Semiblocks for mdoc(7) are within context, so examine
		 * if the next node is a span with closing puncatuation.
		 * Use mdoc(7)'s "Delimiters" section for which to use.
		 * XXX: multiple delimiters...?
		 */

		offset = 0;

		if (linestrip == 0 &&
		    st->type == LOWDOWN_MDOC &&
		    bn->scope == BSCOPE_SEMI &&
		    (tmpbn = TAILQ_NEXT(bn, entries)) != NULL &&
		    tmpbn->scope == BSCOPE_SPAN &&
		    tmpbn->buf != NULL &&
		    (tmpbn->buf[0] == '.' ||
		     tmpbn->buf[0] == ',' ||
		     tmpbn->buf[0] == ';' ||
		     tmpbn->buf[0] == ')' ||
		     tmpbn->buf[0] == ']' ||
		     tmpbn->buf[0] == '?' ||
		     tmpbn->buf[0] == '!') &&
		    (tmpbn->buf[1] == '\0' ||
		     isspace((unsigned char)tmpbn->buf[1]))) {
			offset = 1;
			HBUF_PUTSL(ob, " ");
			hbuf_putc(ob, tmpbn->buf[0]);
			while (tmpbn->buf[offset] != '\0' &&
			    isspace((unsigned char)tmpbn->buf[offset]))
				offset++;
		}

		/*
		 * The "headerhack" is used by SH block macros to
		 * indicate that their children are all normal text or
		 * entities, and so to output the content on the same
		 * line as the SH macro.  This is a hack because man(7)
		 * specifically allows for next-line content; however,
		 * buggy software (e.g., Mac OS X's makewhatis) don't
		 * correctly parse empty SH properly.
		 */

		eolnchar = bn->headerhack ? ' ' : '\n';

		if (eoln && ob->size > 0 && 
		    ob->data[ob->size - 1] != eolnchar &&
		    !hbuf_putc(ob, eolnchar))
			return 0;
	}

	return 1;
}

/*
 * If the queue starts with any known paragraph blocks, strip and free
 * the block(s).
 */
static void
bqueue_strip_paras(struct bnodeq *bq)
{
	struct bnode	*bn;

	while ((bn = TAILQ_FIRST(bq)) != NULL) {
		if (bn->scope != BSCOPE_BLOCK || bn->nbuf == NULL)
			break;
		if (strcmp(bn->nbuf, ".PP") &&
		    strcmp(bn->nbuf, ".IP") &&
		    strcmp(bn->nbuf, ".LP") &&
		    strcmp(bn->nbuf, ".Pp"))
			break;
		TAILQ_REMOVE(bq, bn, entries);
		bnode_free(bn);
	}
}

/*
 * Convert a link into a short-link and place the escaped output into a
 * returned string.  Returns NULL on memory allocation failure.
 */
static char *
rndr_shortlink(const struct lowdown_buf *link)
{
	struct lowdown_buf	*tmp = NULL, *slink = NULL;
	char			*ret = NULL;

	if ((tmp = hbuf_new(32)) == NULL)
		goto out;
	if ((slink = hbuf_new(32)) == NULL)
		goto out;
	if (!hbuf_shortlink(tmp, link))
		goto out;
	if (!lowdown_roff_esc(slink, tmp->data, tmp->size, 1, 0))
		goto out;
	ret = hbuf_string(slink);
out:
	hbuf_free(tmp);
	hbuf_free(slink);
	return ret;
}

/*
 * Escape a URL.  Return FALSE on error (memory), TRUE on success.
 * XXX: this is the same function found in latex.c.
 */
static int
rndr_url_content(struct lowdown_buf *ob, const struct lowdown_buf *link,
    enum halink_type *type, int shorten)
{
	size_t			 i = 0;
	unsigned char		 ch;
	struct lowdown_buf	*tmp;
	int			 rc = 0;

	if ((tmp = hbuf_new(32)) == NULL)
		return 0;

	if (shorten) {
		if (!hbuf_shortlink(tmp, link))
			goto out;
	} else if (!hbuf_putb(tmp, link))
		goto out;

	if (type != NULL && *type == HALINK_EMAIL &&
	    hbuf_strprefix(tmp, "mailto:"))
		i = strlen("mailto:");

	for ( ; i < tmp->size; i++) {
		ch = (unsigned char)tmp->data[i];
		if (!isprint(ch) || strchr(" <>\\^`{|}\"", ch) != NULL) {
			if (!hbuf_printf(ob, "%%%.2X", ch))
				goto out;
		} else if (!hbuf_putc(ob, ch))
			goto out;
	}

	rc = 1;
out:
	hbuf_free(tmp);
	return rc;
}

/*
 * Manage hypertext linking with the groff "pdfhref" macro, UR/UE, or
 * simply using italics.  Return FALSE on error (memory), TRUE on
 * success.
 */
static int
rndr_url(struct bnodeq *obq, struct nroff *st, 
    const struct lowdown_node *n, const struct lowdown_buf *link, 
    const struct lowdown_buf *id, struct bnodeq *bq,
    enum halink_type type)
{
	struct lowdown_buf	*ob = NULL;
	struct bnode		*bn;
	int			 rc = 0, classic = 0, inhibit = 0;
	char			*cp1, *cp2;

	/* Override type as e-mail if the link so resolves. */

	if (type != HALINK_EMAIL && hbuf_strprefix(link, "mailto:"))
		type = HALINK_EMAIL;

	if (st->type == LOWDOWN_MDOC) {
		if ((bn = bqueue_block(obq,
		     type == HALINK_EMAIL ? ".Mt" : ".Lk")) == NULL)
			return 0;

		if ((ob = hbuf_new(32)) == NULL ||
		    (bq != NULL && !bqueue_flush(st, ob, bq, 1)))
			goto out;

		cp1 = strndup(ob->data, ob->size);
		cp2 = strndup(link->data, link->size);

		if (cp1 == NULL || cp2 == NULL) {
			free(cp1);
			free(cp2);
			goto out;
		}
		if (asprintf(&bn->nargs, "%s %s", cp2, cp1) == -1) {
			free(cp1);
			free(cp2);
			bn->args = NULL;
			goto out;
		}

		rc = 1;
		goto out;
	}

	/*
	 * Classic means traditional mode, inhibiting links entirely, or
	 * -tman and in a section header or definition title.
	 * The last is because UR/UE or MT/ME don't work properly in at
	 * least mandoc when invoked with link text: the parser things
	 * that the content is the next line, then puts all UE content
	 * in the subsequent body.
	 */

	classic = !(st->flags & LOWDOWN_ROFF_GROFF);
	if (st->type == LOWDOWN_MAN && (st->flags & LOWDOWN_ROFF_GROFF))
		for ( ; n != NULL && !classic; n = n->parent)
			if (n->type == LOWDOWN_HEADER ||
			    n->type == LOWDOWN_DEFINITION_TITLE)
				classic = 1;

	/* Inhibit means that no link should be shown. */

	if ((st->flags & LOWDOWN_NOLINK) ||
	    ((st->flags & LOWDOWN_NORELLINK) &&
	     hbuf_isrellink(link)))
		classic = inhibit = 1;

	if (classic) {
		/*
		 * For output without .pdfhref ("groff") or UR/UE,
		 * format the link as-is, with text then link, or use
		 * the various shorteners.
		 */
		if (bq == NULL) {
			/*
			 * No link content: format the URL according to
			 * the user's style and output it in italics.
			 * Ignore if inhibiting the link since there's
			 * no content (show something).
			 */
			if (!bqueue_font_mod(st, obq, 0, NFONT_ITALIC))
				goto out;
			if ((bn = bqueue_span(obq, NULL)) == NULL)
				goto out;
			if (st->flags & LOWDOWN_SHORTLINK) {
				bn->nbuf = rndr_shortlink(link);
				if (bn->nbuf == NULL)
					goto out;
			} else {
				bn->buf = strndup(link->data, link->size);
				if (bn->buf == NULL)
					goto out;
			}
			if (!bqueue_font_mod(st, obq, 1, NFONT_ITALIC))
				goto out;
			rc = 1;
			goto out;
		}

		/* Link content exists: output it in bold. */

		if (!bqueue_font_mod(st, obq, 0, NFONT_BOLD))
			goto out;
		TAILQ_CONCAT(obq, bq, entries);
		if (!bqueue_font_mod(st, obq, 1, NFONT_BOLD))
			goto out;

		/* If inhibiting links, short-circuit here. */

		if (inhibit) {
			rc = 1;
			goto out;
		}

		/* Optionally output the link following the content. */

		if (bqueue_span(obq, " <") == NULL)
			goto out;
		if (!bqueue_font_mod(st, obq, 0, NFONT_ITALIC))
			goto out;
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			goto out;
		if (st->flags & LOWDOWN_SHORTLINK) {
			bn->nbuf = rndr_shortlink(link);
			if (bn->nbuf == NULL)
				goto out;
		} else {
 			bn->buf = strndup(link->data, link->size);
			if (bn->buf == NULL)
				goto out;
		}
		if (!bqueue_font_mod(st, obq, 1, NFONT_ITALIC))
			goto out;
		if (bqueue_span(obq, ">") == NULL)
			goto out;

		rc = 1;
		goto out;
	} else if (st->type == LOWDOWN_MAN) {
		/* 
		 * For man(7) documents, use UR/UE or MT/ME.  According
		 * to the groff documentation, these are supported by
		 * all modern formatters.  Both of these formats show
		 * the full URI, so don't respect the shortening
		 * directive.  Requesting not to show the link will not
		 * have routed to this block in the first place.
		 */
		if ((ob = hbuf_new(32)) == NULL)
			goto out;

		/*
		 * This will strip out the "mailto:", if defined, since
		 * it is not allowed by the MT/ME macros.
		 */

		if (!rndr_url_content(ob, link, &type, 0))
			goto out;

		/* Either MT or UR depending on if a URL. */

		bn = bqueue_node(obq, BSCOPE_SEMI, type == HALINK_EMAIL ?
			".MT" : ".UR");
		if (bn == NULL)
			goto out;
		if ((bn->nargs = strndup(ob->data, ob->size)) == NULL)
			goto out;

		/*
		 * Link text (optional).  This will be shown alongside
		 * the link itself in most (all?) viewers.
		 */

		if (bq != NULL)
			TAILQ_CONCAT(obq, bq, entries);

		/* Close out the link content. */

		bn = bqueue_node(obq, BSCOPE_SEMI_CLOSE,
			type == HALINK_EMAIL ?  ".ME" : ".UE");
		if (bn == NULL)
			goto out;

		rc = 1;
		goto out;
	}

	/* This is an ms document with groff extensions: use pdfhref. */

	if ((ob = hbuf_new(32)) == NULL)
		goto out;

	/* Encode the URL. */

	if (!HBUF_PUTSL(ob, "-D "))
		goto out;

	/*
	 * If this link an e-mail, make sure it doesn't already start
	 * with "mailto:", which can happen when we override te type by
	 * investigating whether the prefix is "mailto:".
	 */

	if (type == HALINK_EMAIL && !hbuf_strprefix(link, "mailto:") &&
	    !HBUF_PUTSL(ob, "mailto:"))
		goto out;
	if (!rndr_url_content(ob, link, NULL, 0))
		goto out;
	if (!HBUF_PUTSL(ob, " -- "))
		goto out;

	/*
	 * If no content, ouput the link (possibly stripping "mailto:").
	 * Otherwise, flush the content to the output.
	 */

	if (bq == NULL && !rndr_url_content(ob, link, &type,
	    st->flags & LOWDOWN_SHORTLINK))
		goto out;
	else if (bq != NULL && !bqueue_flush(st, ob, bq, 0))
		goto out;

	/*
	 * If we have an ID, emit it before the link part.  This is
	 * important because this isn't printed, so using "-A \c" will
	 * have no effect, so that's used on the subsequent link.
	 */

	if (id != NULL && id->size > 0) {
		bn = bqueue_node(obq, BSCOPE_SEMI, ".pdfhref M");
		if (bn == NULL)
			goto out;
		bn->args = strndup(id->data, id->size);
		if (bn->args == NULL)
			goto out;
	}

	/* Finally, emit the external or in-page link contents. */

	bn = bqueue_node(obq, BSCOPE_SEMI,
		(type != HALINK_EMAIL && link->size > 0 &&
		 link->data[0] == '#') ?
		".pdfhref L" : ".pdfhref W");
	if (bn == NULL)
		goto out;
	if ((bn->nargs = strndup(ob->data, ob->size)) == NULL)
		goto out;

	rc = 1;
out:
	hbuf_free(ob);
	return rc;
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_autolink(struct nroff *st, struct bnodeq *obq,
	const struct lowdown_node *n)
{

	return rndr_url(obq, st, n, &n->rndr_autolink.link, NULL, NULL,
		n->rndr_autolink.type);
}

static int
rndr_definition_title(struct nroff *st, struct bnodeq *obq,
     struct bnodeq *bq)
{
	char			 nbuf[32];
	struct bnode		*bn;
	struct lowdown_buf	*buf = NULL;
	int			 rc = 0;

	if (st->type == LOWDOWN_MDOC) {
		if ((bn = bqueue_block(obq, ".It")) == NULL ||
		    (buf = hbuf_new(32)) == NULL ||
		    !bqueue_flush(st, buf, bq, 1) ||
		    (bn->nargs = strndup(buf->data, buf->size)) == NULL)
			goto out;
	} else if (st->type == LOWDOWN_MAN) {
		snprintf(nbuf, sizeof(nbuf), ".TP %zu", st->indent);
		if (bqueue_block(obq, nbuf) == NULL)
			return 0;
		TAILQ_CONCAT(obq, bq, entries);
		if (bqueue_span(obq, "\n") == NULL)
			return 0;
	} else {
		if (bqueue_block(obq, ".XP") == NULL)
			return 0;
		TAILQ_CONCAT(obq, bq, entries);
		if (bqueue_block(obq, ".br") == NULL)
			return 0;
	}

	rc = 1;
out:
	hbuf_free(buf);
	return rc;
}

static int
rndr_definition_data(const struct nroff *st, struct bnodeq *obq,
    struct bnodeq *bq)
{
	/* Strip out leading paragraphs. */

	bqueue_strip_paras(bq);
	TAILQ_CONCAT(obq, bq, entries);
	return 1;
}

static int
rndr_list(struct nroff *st, struct bnodeq *obq, 
    const struct lowdown_node *n, struct bnodeq *bq)
{
	struct bnode			*bn;
	const char			*type;
	int				 compact;
	const struct lowdown_node	*pn;

	/* Lists for mdoc(7) are somewhat simple... */

	if (st->type == LOWDOWN_MDOC) {
		pn = TAILQ_FIRST(&n->children);

		/* Output Pp prior to compact lists. */

		compact = pn != NULL && pn->type == LOWDOWN_LISTITEM &&
			(pn->rndr_listitem.flags & HLIST_FL_BLOCK);

		if (compact && !NODE_AFTER_HEAD(n) &&
		    bqueue_block(obq, ".Pp") == NULL)
			return 0;
		if (n->rndr_list.flags & HLIST_FL_DEF)
			type = "-tag -width Ds";
		else if (n->rndr_list.flags & HLIST_FL_ORDERED)
			type = "-enum";
		else
			type = "-bullet";
		if ((bn = bqueue_block(obq, ".Bl")) == NULL)
			return 0;
		if (asprintf(&bn->nargs, "%s%s", type,
		    compact ?  " -compact" : "") == -1) {
			bn->nargs = NULL;
			return 0;
		}
		TAILQ_CONCAT(obq, bq, entries);
		return bqueue_block(obq, ".El") != NULL;
	}

	/* 
	 * For a nested man(7) or ms(7) list, use RS/RE to indent the
	 * nested component.  Otherwise the "IP" used for the titles and
	 * contained paragraphs won't indent properly.
	 */

	for (n = n->parent; n != NULL; n = n->parent)
		if (n->type == LOWDOWN_LISTITEM)
			break;

	if (n != NULL && bqueue_block(obq, ".RS") == NULL)
		return 0;
	TAILQ_CONCAT(obq, bq, entries);
	if (n != NULL && bqueue_block(obq, ".RE") == NULL)
		return 0;

	st->use_lp = 1;
	return 1;
}

static int
rndr_blockquote(struct nroff *st, struct bnodeq *obq,
    struct bnodeq *bq)
{
	struct bnode	*bn;

	if (st->type == LOWDOWN_MDOC) {
		if ((bn = bqueue_block(obq, ".Bd")) == NULL ||
		    (bn->nargs = strdup("-ragged -offset indent")) == NULL)
			return 0;
		bqueue_strip_paras(bq);
		TAILQ_CONCAT(obq, bq, entries);
		if (bqueue_block(obq, ".Ed") == NULL)
			return 0;
	} else {
		if (bqueue_block(obq, ".RS") == NULL)
			return 0;
		TAILQ_CONCAT(obq, bq, entries);
		if (bqueue_block(obq, ".RE") == NULL)
			return 0;
	}

	return 1;
}

/*
 * Return a font (e.g., *emphasis*).  Returns FALSE on failure, TRUE on
 * success.  If this is a mdoc(7) or man(7) file, try to format as a
 * manpage.  Failing that, just use a regular span of text.
 */
static int
rndr_font(struct nroff *st, struct bnodeq *obq,
    const struct lowdown_node *n, struct bnodeq *bq)
{
	int			 rc;

	if ((st->type != LOWDOWN_MDOC && st->type != LOWDOWN_MAN) ||
	    !(st->flags & LOWDOWN_ROFF_MANPAGE)) {
		TAILQ_CONCAT(obq, bq, entries);
		return 1;
	}
	if ((rc = roff_manpage_inline(st, n, obq, bq)) < 0)
		return 0;
	else if (rc == 0)
		TAILQ_CONCAT(obq, bq, entries);
	return 1;
}

/*
 * Return a `codespan`.  Returns FALSE on failure, TRUE on success.  If
 * this is a mdoc(7) or man(7) file, try to format as a manpage.
 * Failing that, just use a regular span of text.
 */
static int
rndr_codespan(struct nroff *st, struct bnodeq *obq,
    const struct rndr_codespan *param)
{
	struct bnode	*bn;

	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;
	bn->buf = strndup(param->text.data, param->text.size);
	return bn->buf != NULL;
}

static int
rndr_linebreak(struct nroff *st, struct bnodeq *obq)
{

	return bqueue_block(obq, ".br") != NULL;
}

static int
rndr_header(struct nroff *st, struct bnodeq *obq, struct bnodeq *bq,
    const struct lowdown_node *n)
{
	ssize_t				 level;
	struct bnode			*bn;
	struct lowdown_buf		*buf = NULL;
	const struct lowdown_buf	*nbuf;
	int			 	 rc = 0;
        const struct lowdown_node	*child;

	st->lastsec = n;

	level = (ssize_t)n->rndr_header.level + st->headers_offs;
	if (level < 1)
		level = 1;

	if (st->type == LOWDOWN_MAN) {
		/*
		 * For man(7), we use SH for the first-level section, SS
		 * for other sections.  TODO: use PP then italics or
		 * something for third-level etc.
		 */

		bn = level == 1 ?
			bqueue_block(obq, ".SH") :
			bqueue_block(obq, ".SS");
		if (bn == NULL)
			goto out;

		/*
		 * Do a scan of the contents of the SH.  If there's only
		 * normal text (or entities), then record this fact.  It
		 * will be used in bqueue_flush() for how the macro is
		 * serialised.
		 */

		if (level == 1) {
			bn->headerhack = 1;
			TAILQ_FOREACH(child, &n->children, entries) {
				if (child->type == LOWDOWN_ENTITY ||
				    child->type == LOWDOWN_NORMAL_TEXT)
					continue;
				bn->headerhack = 0;
				break;
			}
		}

		TAILQ_CONCAT(obq, bq, entries);
		st->use_lp = 1;
		rc = 1;
		goto out;
	} else if (st->type == LOWDOWN_MDOC) {
		/*
		 * Similar to mdoc(7), only use Sh for the first-level
		 * section, Ss for other sections.
		 */

		bn = level == 1 ?
			bqueue_block(obq, ".Sh") :
			bqueue_block(obq, ".Ss");
		if (bn == NULL ||
		    (buf = hbuf_new(32)) == NULL ||
		    !bqueue_flush(st, buf, bq, 1) ||
		    (bn->nargs = strndup(buf->data, buf->size)) == NULL)
			goto out;
		rc = 1;
		goto out;
	} 

	/*
	 * If we're using ms(7) w/groff extensions and w/o numbering,
	 * used the numbered version of the SH macro.
	 * If we're numbered ms(7), use NH.
	 */

	bn = (st->flags & LOWDOWN_ROFF_NUMBERED) ?
		bqueue_block(obq, ".NH") :
		bqueue_block(obq, ".SH");
	if (bn == NULL)
		goto out;

	if ((st->flags & LOWDOWN_ROFF_NUMBERED) ||
	    (st->flags & LOWDOWN_ROFF_GROFF)) 
		if (asprintf(&bn->nargs, "%zd", level) == -1) {
			bn->nargs = NULL;
			goto out;
		}

	TAILQ_CONCAT(obq, bq, entries);
	st->use_lp = 1;

	/*
	 * Used in -mspdf output for creating a TOC and intra-document
	 * linking.
	 */

	if (st->flags & LOWDOWN_ROFF_GROFF) {
		if ((buf = hbuf_new(32)) == NULL)
			goto out;
		if (!hbuf_extract_text(buf, n))
			goto out;

		if ((bn = bqueue_block(obq, ".pdfhref")) == NULL)
			goto out;
		if (asprintf(&bn->nargs, "O %zd", level) == -1) {
			bn->nargs = NULL;
			goto out;
		}

		/*
		 * No need to quote: quotes will be converted by
		 * escaping into roff.
		 */

		bn->args = buf->size == 0 ?
			strdup("") : strndup(buf->data, buf->size);
		if (bn->args == NULL)
			goto out;

		if ((bn = bqueue_block(obq, ".pdfhref M")) == NULL)
			goto out;

		/*
		 * If the identifier comes from the user, we need to
		 * escape it accordingly; otherwise, use it directly as
		 * the hbuf_id() function will take care of it.
		 */

		if (n->rndr_header.attr_id.size) {
			bn->args = strndup
				(n->rndr_header.attr_id.data,
				 n->rndr_header.attr_id.size);
			if (bn->args == NULL)
				goto out;
		} else {
			nbuf = hbuf_id(buf, NULL, &st->headers_used);
			if (nbuf == NULL)
				goto out;
			bn->nargs = strndup(nbuf->data, nbuf->size);
			if (bn->nargs == NULL)
				goto out;
		}
	}

	rc = 1;
out:
	hbuf_free(buf);
	return rc;
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static ssize_t
rndr_link(struct nroff *st, struct bnodeq *obq, struct bnodeq *bq,
	const struct lowdown_node *n)
{

	return rndr_url(obq, st, n, &n->rndr_link.link,
		&n->rndr_link.attr_id, bq, HALINK_NORMAL);
}

static int
rndr_listitem(struct nroff *st, struct bnodeq *obq,
    const struct lowdown_node *n, struct bnodeq *bq,
    const struct rndr_listitem *param)
{
	struct bnode	*bn;
	const char	*box;
	size_t		 total = 0, numsize;

	/* List items for mdoc(7) are trivially easy... */

	if (st->type == LOWDOWN_MDOC) {
		if (n->parent != NULL &&
		    n->parent->type == LOWDOWN_DEFINITION_DATA) {
			TAILQ_CONCAT(obq, bq, entries);
			return 1;
		}
		if ((bn = bqueue_block(obq, ".It")) == NULL)
			return 0;
		bqueue_strip_paras(bq);
		TAILQ_CONCAT(obq, bq, entries);
		return 1;
	}

	/*
	 * For man(7) and ms(7), when indenting for the number of bullet
	 * preceding the line of text, use "indent" spaces as the
	 * default.  For ordered lists stretching beyond the indent
	 * size, enlarge as necessary.
	 */

	if (param->flags & HLIST_FL_ORDERED) {
		if (n->parent != NULL &&
		    n->parent->type == LOWDOWN_LIST)
			total = n->parent->rndr_list.items +
				(n->parent->rndr_list.start - 1);

		/* Yes, a little ridiculous, but doesn't hurt. */

		numsize = total < 10 ? 3 :
			total < 100 ? 4 :
			total < 1000 ? 5 :
			total < 10000 ? 6 :
			total < 100000 ? 7 :
			total < 1000000 ? 8 :
			total < 10000000 ? 9 :
			10;

		if (numsize < st->indent)
			numsize = st->indent;

		if ((bn = bqueue_block(obq, ".IP")) == NULL)
			return 0;
		if (asprintf(&bn->nargs, 
		    "\"%zu.\" %zu", param->num, numsize) == -1)
			return 0;
	} else if (param->flags & HLIST_FL_UNORDERED) {
		if (param->flags & HLIST_FL_CHECKED)
			box = "[u2611]";
		else if (param->flags & HLIST_FL_UNCHECKED)
			box = "[u2610]";
		else
			box = "(bu";
		if ((bn = bqueue_block(obq, ".IP")) == NULL)
			return 0;
		if (asprintf(&bn->nargs, "\"\\%s\" %zu",
		    box, st->indent) == -1)
			return 0;
	}

	/* Strip out all leading redundant paragraphs. */

	bqueue_strip_paras(bq);
	TAILQ_CONCAT(obq, bq, entries);

	/* 
	 * Suppress trailing space if we're not in a block and there's a
	 * list item that comes after us (i.e., anything after us).
	 */

	if ((n->rndr_listitem.flags & HLIST_FL_BLOCK) ||
	    (n->rndr_listitem.flags & HLIST_FL_DEF))
		return 1;

	if (TAILQ_NEXT(n, entries) != NULL &&
	    (bqueue_block(obq, ".if n \\\n.sp -1") == NULL ||
	     bqueue_block(obq, ".if t \\\n.sp -0.25v\n") == NULL))
		return 0;

	return 1;
}

/*
 * Return TRUE if the parser is currently within a section, that is,
 * check the given name in the last header.  Return FALSE if not.
 */
int
roff_in_section(const struct nroff *st, const char *section)
{
	const struct lowdown_node	*pn;

	return st->lastsec != NULL &&
	    (pn = TAILQ_FIRST(&st->lastsec->children)) != NULL &&
	    pn->type == LOWDOWN_NORMAL_TEXT &&
	    hbuf_streq(&pn->rndr_normal_text.text, section);
}

static int
rndr_blockcode(struct nroff *st, struct bnodeq *obq,
    const struct lowdown_node *n)
{
	struct bnode	*bn;

	if (st->type == LOWDOWN_MDOC) {
		/*
		 * SYNOPSIS blocks are not indented.
		 */
		if ((bn = bqueue_block(obq, ".Bd")) == NULL)
			return 0;
		bn->nargs = roff_in_section(st, "SYNOPSIS") ?
		    strdup("-literal") : 
		    strdup("-literal -offset indent");
		if (bn->nargs == NULL)
			return 0;
	} else {
		/*
		 * XXX: intentionally don't use LD/DE because it
		 * introduces vertical space.  This means that
		 * subsequent blocks (paragraphs, etc.) will have a
		 * double-newline.
		 */
		if (bqueue_block(obq, ".LP") == NULL)
			return 0;
		if (st->type == LOWDOWN_MAN &&
		    (st->flags & LOWDOWN_ROFF_GROFF)) {
			if (bqueue_block(obq, ".EX") == NULL)
				return 0;
		} else {
			if (bqueue_block(obq, ".nf") == NULL ||
			    bqueue_block(obq, ".ft CR") == NULL)
				return 0;
		}
	}

	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(obq, bn, entries);
	bn->scope = BSCOPE_LITERAL;
	if ((bn->buf = hbuf_string(&n->rndr_blockcode.text)) == NULL)
		return 0;

	if (st->type == LOWDOWN_MDOC) {
		/*
		 * SYNOPSIS blocks are followed by a paragraph.
		 */
		if ((bn = bqueue_block(obq, ".Ed")) == NULL)
			return 0;
		if (roff_in_section(st, "SYNOPSIS") &&
		    bqueue_block(obq, ".Pp") == NULL)
			return 0;
	} else {
		if (st->type == LOWDOWN_MAN &&
		    (st->flags & LOWDOWN_ROFF_GROFF)) {
			if (bqueue_block(obq, ".EE") == NULL)
				return 0;
		} else {
			if (bqueue_block(obq, ".ft") == NULL ||
			    bqueue_block(obq, ".fi") == NULL)
				return 0;
		}
	}
	return 1;
}

static int
rndr_paragraph(struct nroff *st, const struct lowdown_node *n,
    struct bnodeq *obq, struct bnodeq *nbq)
{
	struct bnode	*bn;
	int		 rc = 0;

	/* 
	 * In man(7) and ms(7), subsequent paragraphs get a PP for the
	 * indentation; otherwise, use LP and forego the indentation.
	 * If in a list item, make sure not to reset the text indent by
	 * using an "IP".  mdoc(7) simply uses "Pp".
	 *
	 * Both mdoc(7) and man(7) might override the paragraph content
	 * if in manpage-specific context.
	 */

	if (st->type == LOWDOWN_MAN || st->type == LOWDOWN_MDOC)
		rc = roff_manpage_paragraph(st, n, obq, nbq);
	if (rc > 0)
		return 1;
	else if (rc < 0)
		return 0;

	if (st->type != LOWDOWN_MDOC) {
		for ( ; n != NULL; n = n->parent)
			if (n->type == LOWDOWN_LISTITEM)
				break;
		if (n != NULL)
			bn = bqueue_block(obq, ".IP");
		else if (st->use_lp)
			bn = bqueue_block(obq, ".LP");
		else
			bn = bqueue_block(obq, ".PP");
		if (bn == NULL)
			return 0;
		st->use_lp = 0;
	} else {
		if (!NODE_AFTER_HEAD(n) &&
		    bqueue_block(obq, ".Pp") == NULL)
			return 0;
	}

	TAILQ_CONCAT(obq, nbq, entries);
	return 1;
}

static int
rndr_raw_block(const struct nroff *st, struct bnodeq *obq,
    const struct rndr_blockhtml *param)
{
	struct bnode	*bn;

	if (st->flags & LOWDOWN_SKIP_HTML)
		return 1;
	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(obq, bn, entries);
	bn->scope = BSCOPE_LITERAL;
	bn->buf = strndup(param->text.data, param->text.size);
	return bn->buf != NULL;
}

static int
rndr_hrule(struct nroff *st, struct bnodeq *obq)
{

	if (st->type == LOWDOWN_MDOC) {
		if (bqueue_block(obq, ".Pp") == NULL)
			return 0;
		return bqueue_block(obq, "\\l\'2i'") != NULL;
	}

	/* The LP is to reset the margins. */

	if (bqueue_block(obq, ".LP") == NULL)
		return 0;

	/* Set use_lp so we get a following LP not PP. */

	st->use_lp = 1;

	if (st->type == LOWDOWN_MAN)
		return bqueue_block(obq, "\\l\'2i'") != NULL;

	return bqueue_block(obq,
	    ".ie d HR \\{\\\n"
	    ".HR\n"
	    "\\}\n"
	    ".el \\{\\\n"
	    ".sp 1v\n"
	    "\\l'\\n(.lu'\n"
	    ".sp 1v\n"
	    ".\\}") != NULL;
}

static int
rndr_image(struct nroff *st, struct bnodeq *obq, 
    const struct rndr_image *param)
{
	const char	*cp;
	size_t		 sz;
	struct bnode	*bn;

	if (st->type == LOWDOWN_MS) {
		cp = memrchr(param->link.data, '.', param->link.size);
		if (cp != NULL) {
			cp++;
			sz = param->link.size - (cp - param->link.data);
			if ((sz == 2 && memcmp(cp, "ps", 2) == 0) ||
			    (sz == 3 && memcmp(cp, "eps", 3) == 0)) {
				bn = bqueue_block(obq, ".PSPIC");
				if (bn == NULL)
					return 0;
				bn->args = strndup(param->link.data,
					param->link.size);
				return bn->args != NULL;
			}
		}
	}

	/* In -Tman, we have no images: treat as a link. */

	if (!bqueue_font_mod(st, obq, 0, NFONT_BOLD))
		return 0;
	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;
	bn->buf = strndup(param->alt.data, param->alt.size);
	if (bn->buf == NULL)
		return 0;
	if (!bqueue_font_mod(st, obq, 1, NFONT_BOLD))
		return 0;
	if ((st->flags & LOWDOWN_NOLINK) ||
	    ((st->flags & LOWDOWN_NORELLINK) &&
	     hbuf_isrellink(&param->link)))
		return bqueue_span(obq, " (Image)") != NULL;
	if (bqueue_span(obq, " (Image: ") == NULL)
		return 0;
	if (!bqueue_font_mod(st, obq, 0, NFONT_ITALIC))
		return 0;
	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;
	if (st->flags & LOWDOWN_SHORTLINK) {
		bn->nbuf = rndr_shortlink(&param->link);
		if (bn->nbuf == NULL)
			return 0;
	} else {
		bn->buf = strndup(param->link.data, param->link.size);
		if (bn->buf == NULL)
			return 0;
	}
	if (!bqueue_font_mod(st, obq, 1, NFONT_ITALIC))
		return 0;
	return bqueue_span(obq, ")") != NULL;
}

static int
rndr_raw_html(const struct nroff *st,
	struct bnodeq *obq, const struct rndr_raw_html *param)
{
	struct bnode	*bn;

	if (st->flags & LOWDOWN_SKIP_HTML)
		return 1;
	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(obq, bn, entries);
	bn->scope = BSCOPE_LITERAL;
	bn->buf = strndup(param->text.data, param->text.size);
	return bn->buf != NULL;
}

static int
rndr_table(struct nroff *st, struct bnodeq *obq, struct bnodeq *bq)
{
	const char	*macro;

	macro = st->type != LOWDOWN_MS ||
		!(st->flags & LOWDOWN_ROFF_GROFF) ? ".TS" : ".TS H";
	if (bqueue_block(obq, macro) == NULL)
		return 0;
	if (bqueue_block(obq, "tab(|) expand allbox;") == NULL)
		return 0;
	TAILQ_CONCAT(obq, bq, entries);
	return bqueue_block(obq, ".TE") != NULL;
}

static int
rndr_table_header(struct nroff *st, struct bnodeq *obq,
    struct bnodeq *bq, const struct rndr_table_header *param)
{
	size_t		 	 i;
	struct lowdown_buf	*ob;
	struct bnode		*bn;
	int			 rc = 0;

	if ((ob = hbuf_new(32)) == NULL)
		return 0;

	/* 
	 * This specifies the header layout.
	 * We make the header bold, but this is arbitrary.
	 */

	if ((bn = bqueue_block(obq, NULL)) == NULL)
		goto out;
	for (i = 0; i < param->columns; i++) {
		if (i > 0 && !HBUF_PUTSL(ob, " "))
			goto out;
		switch (param->flags[i] & HTBL_FL_ALIGNMASK) {
		case HTBL_FL_ALIGN_CENTER:
			if (!HBUF_PUTSL(ob, "cb"))
				goto out;
			break;
		case HTBL_FL_ALIGN_RIGHT:
			if (!HBUF_PUTSL(ob, "rb"))
				goto out;
			break;
		default:
			if (!HBUF_PUTSL(ob, "lb"))
				goto out;
			break;
		}
	}
	if ((bn->nbuf = strndup(ob->data, ob->size)) == NULL)
		goto out;

	/* Now the body layout. */

	hbuf_truncate(ob);
	if ((bn = bqueue_block(obq, NULL)) == NULL)
		goto out;
	for (i = 0; i < param->columns; i++) {
		if (i > 0 && !HBUF_PUTSL(ob, " "))
			goto out;
		switch (param->flags[i] & HTBL_FL_ALIGNMASK) {
		case HTBL_FL_ALIGN_CENTER:
			if (!HBUF_PUTSL(ob, "c"))
				goto out;
			break;
		case HTBL_FL_ALIGN_RIGHT:
			if (!HBUF_PUTSL(ob, "r"))
				goto out;
			break;
		default:
			if (!HBUF_PUTSL(ob, "l"))
				goto out;
			break;
		}
	}
	if (!hbuf_putc(ob, '.'))
		goto out;
	if ((bn->nbuf = strndup(ob->data, ob->size)) == NULL)
		goto out;

	TAILQ_CONCAT(obq, bq, entries);

	if (st->type == LOWDOWN_MS &&
	    (st->flags & LOWDOWN_ROFF_GROFF) &&
	    bqueue_block(obq, ".TH") == NULL)
		goto out;

	rc = 1;
out:
	hbuf_free(ob);
	return rc;
}

static int
rndr_table_row(struct nroff *st, struct bnodeq *obq, struct bnodeq *bq)
{

	TAILQ_CONCAT(obq, bq, entries);
	return bqueue_block(obq, NULL) != NULL;
}

static int
rndr_table_cell(struct bnodeq *obq, struct bnodeq *bq,
    const struct rndr_table_cell *param)
{
	struct bnode	*bn;

	if (param->col > 0 && bqueue_span(obq, "|") == NULL)
		return 0;
	if (bqueue_span(obq, "T{\n") == NULL)
		return 0;
	TAILQ_CONCAT(obq, bq, entries);
	if ((bn = bqueue_span(obq, "T}")) == NULL)
		return 0;
	bn->tblhack = 1;
	return 1;
}

static int
rndr_superscript(struct bnodeq *obq, struct bnodeq *bq,
    enum lowdown_rndrt type)
{
	const char	*p1, *p2;

	/* Lift pandoc's way of doing this. */

	p1 = (type == LOWDOWN_SUPERSCRIPT) ?
		"\\v\'-0.3m\'\\s[\\n[.s]*9u/12u]" : 
		"\\v\'0.3m\'\\s[\\n[.s]*9u/12u]";
	p2 = (type == LOWDOWN_SUPERSCRIPT) ?
		"\\s0\\v\'0.3m\'" :
		"\\s0\\v\'-0.3m\'";
	if (bqueue_span(obq, p1) == NULL)
		return 0;
	TAILQ_CONCAT(obq, bq, entries);
	return bqueue_span(obq, p2) != NULL;
}

static int
rndr_footnote_def(struct nroff *st, struct bnodeq *obq,
    struct bnodeq *bq, size_t num)
{
	struct bnode	*bn;

	/* 
	 * Use groff_ms(7)-style footnotes.
	 * We know that the definitions are delivered in the same order
	 * as the footnotes are made, so we can use the automatic
	 * ordering facilities.
	 */

	if (st->type == LOWDOWN_MS) {
		if (bqueue_block(obq, ".FS") == NULL)
			return 0;
		bn = bqueue_block(obq, ".pdfhref M");
		if (bn == NULL)
			return 0;
		if (asprintf(&bn->args, "footnote-%zu", num) == -1)
			return 0;
		bqueue_strip_paras(bq);
		TAILQ_CONCAT(obq, bq, entries);
		return bqueue_block(obq, ".FE") != NULL;
	}

	/*
	 * For man(7) and mdoc(7), just print as normal, with a leading
	 * footnote number in italics and superscripted.
	 */

	if (st->type == LOWDOWN_MAN &&
	    bqueue_block(obq, ".LP") == NULL)
		return 0;
	if (st->type == LOWDOWN_MDOC && 
	    bqueue_block(obq, ".Pp") == NULL)
		return 0;
	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;
	if (asprintf(&bn->nbuf, 
	    "\\0\\fI\\u\\s-3%zu\\s+3\\d\\fP\\0", num) == -1) {
		bn->nbuf = NULL;
		return 0;
	}
	bqueue_strip_paras(bq);
	TAILQ_CONCAT(obq, bq, entries);
	return 1;
}

/*
 * Flush out footnotes or (in some conditions) do nothing and return
 * success.  If "fin" is non-zero, this is the final invocation of
 * rndr_footnotes() at the root of the document after everything else;
 * otherwise, this is being called within the document.  Footnote
 * conditions are that footnotes must exist, mustn't already be printing
 * and parsing, and satisfy endnote/footnote criterion.  Returns zero on
 * failure (memory allocation), non-zero on success.
 */
static int
rndr_footnotes(struct nroff *st, struct bnodeq *obq, int fin)
{
	/* Already printing/parsing, or none to print. */

	if (st->footdepth > 0 || st->footpos >= st->footsz)
		return 1;

	/* Non-final and in -tman (-tman has only endnotes). */

	if (!fin && st->type != LOWDOWN_MS)
		return 1;

	/* Non-final and -tms with endnotes specified. */

	if (!fin && st->type == LOWDOWN_MS &&
	    (st->flags & LOWDOWN_ROFF_ENDNOTES))
		return 1;

	st->footdepth++;
	if (st->type == LOWDOWN_MAN) {
		if (bqueue_block(obq, ".LP") == NULL)
			return 0;
		if (bqueue_block(obq, ".sp 3") == NULL)
			return 0;
		if (bqueue_block(obq, "\\l\'2i'") == NULL)
			return 0;
	}

	for ( ; st->footpos < st->footsz; st->footpos++)
		if (!rndr_footnote_def(st, obq, st->foots[st->footpos],
		    st->footpos + 1))
			return 0;

	assert(st->footdepth > 0);
	st->footdepth--;
	return 1;
}

static int
rndr_footnote_ref(struct nroff *st, struct bnodeq *obq,
	struct bnodeq *bq)
{
	struct bnode	*bn;
	void		*pp;
	size_t		 num = st->footsz + 1;

	/* 
	 * Use groff_ms(7)-style automatic footnoting, else just put a
	 * reference number in small superscripts.
	 */

	if (st->type != LOWDOWN_MS) {
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			return 0;
		if (asprintf(&bn->nbuf, "\\u\\s-3%zu\\s+3\\d",
		    num) == -1)
			bn->nbuf = NULL;
		if (bn->nbuf == NULL)
			return 0;
	} else {
		bn = bqueue_node(obq, BSCOPE_SEMI, ".pdfhref L");
		if (bn == NULL)
			return 0;
		if (asprintf(&bn->nargs, "-D footnote-%zu -- \\**",
		    num) == -1)
			bn->nargs = NULL;
		if (bn->nargs == NULL)
			return 0;
	}

	/*
	 * Queue the footnote for printing at the end of the document.
	 * The FS/FE could be printed now, but it's easier for the block
	 * printing algorithm to determine that the link shouldn't have
	 * trailing space without it.
	 */

	pp = recallocarray(st->foots, st->footsz,
		st->footsz + 1, sizeof(struct bnodeq *));
	if (pp == NULL)
		return 0;
	st->foots = pp;
	st->foots[st->footsz] = malloc(sizeof(struct bnodeq));
	if (st->foots[st->footsz] == NULL)
		return 0;
	TAILQ_INIT(st->foots[st->footsz]);
	TAILQ_CONCAT(st->foots[st->footsz], bq, entries);
	st->footsz++;
	return 1;
}

static int
rndr_entity(const struct nroff *st,
	struct bnodeq *obq, const struct rndr_entity *param)
{
	char		 buf[32];
	const char	*ent;
	struct bnode	*bn;
	int32_t		 iso;
	size_t		 sz;

	/*
	 * Handle named entities if "ent" is non-NULL, use unicode
	 * escapes for values above 126, and just the regular character
	 * if within the ASCII set.
	 */

	if ((ent = entity_find_nroff(&param->text, &iso)) != NULL) {
		sz = strlen(ent);
		if (sz == 1)
			snprintf(buf, sizeof(buf), "\\%s", ent);
		else if (sz == 2)
			snprintf(buf, sizeof(buf), "\\(%s", ent);
		else
			snprintf(buf, sizeof(buf), "\\[%s]", ent);
		return bqueue_span(obq, buf) != NULL;
	} else if (iso > 0 && iso > 126) {
		if (st->flags & LOWDOWN_ROFF_GROFF)
			snprintf(buf, sizeof(buf), "\\[u%.4llX]", 
				(unsigned long long)iso);
		else
			snprintf(buf, sizeof(buf), "\\U\'%.4llX\'", 
				(unsigned long long)iso);
		return bqueue_span(obq, buf) != NULL;
	} else if (iso > 0) {
		snprintf(buf, sizeof(buf), "%c", iso);
		return bqueue_span(obq, buf) != NULL;
	}

	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;
	bn->buf = strndup(param->text.data, param->text.size);
	return bn->buf != NULL;
}

/*
 * Render a font prior to parsing child content.  This is because the
 * font stack must be set for decorating child content.  Returns FALSE
 * on failure (memory), TRUE on success.
 */
static int
rndr_font_pre(struct nroff *st, const struct lowdown_node *n,
    struct bnodeq *obq)
{
	/*
	 * mdoc(7) and man(7) passes font commands into the manpage
	 * parser to determine the content type and response.  FIXME: if
	 * the parser in roff_manpage_inline() fails to convert, then
	 * this strips away any formatting unilaterally.
	 */

	if ((st->type == LOWDOWN_MDOC || st->type == LOWDOWN_MAN) &&
	    (st->flags & LOWDOWN_ROFF_MANPAGE))
		switch (n->type) {
		case LOWDOWN_EMPHASIS:
		case LOWDOWN_HIGHLIGHT:
		case LOWDOWN_DOUBLE_EMPHASIS:
		case LOWDOWN_TRIPLE_EMPHASIS:
			return 1;
		default:
			break;
		}

	switch (n->type) {
	case LOWDOWN_CODESPAN:
		return bqueue_font_mod(st, obq, 0, NFONT_FIXED);
	case LOWDOWN_EMPHASIS:
		return bqueue_font_mod(st, obq, 0, NFONT_ITALIC);
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
		return bqueue_font_mod(st, obq, 0, NFONT_BOLD);
	case LOWDOWN_TRIPLE_EMPHASIS:
		st->fonts[NFONT_ITALIC]++;
		st->fonts[NFONT_BOLD]++;
		return bqueue_font(st, obq, 0);
	default:
		break;
	}
	abort();
}

/*
 * Reset the font stack to what it was entering the child content and
 * close out the font.  Returns FALSE on failure (memory), TRUE on
 * success.
 */
static int
rndr_font_post(struct nroff *st, const struct lowdown_node *n,
    const enum nfont *fonts, struct bnodeq *obq)
{
	/*
	 * mdoc(7) and man(7) passes font commands into the manpage
	 * parser to determine the content type and response.
	 */

	if ((st->type == LOWDOWN_MDOC || st->type == LOWDOWN_MAN) &&
	    (st->flags & LOWDOWN_ROFF_MANPAGE))
		switch (n->type) {
		case LOWDOWN_EMPHASIS:
		case LOWDOWN_HIGHLIGHT:
		case LOWDOWN_DOUBLE_EMPHASIS:
		case LOWDOWN_TRIPLE_EMPHASIS:
			return 1;
		default:
			break;
		}

	switch (n->type) {
	case LOWDOWN_CODESPAN:
	case LOWDOWN_EMPHASIS:
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
	case LOWDOWN_TRIPLE_EMPHASIS:
		memcpy(st->fonts, fonts, sizeof(st->fonts));
		return bqueue_font(st, obq, 1);
	default:
		break;
	}
	abort();
}

/*
 * Split "b" at sequential white-space, outputting the results per-line,
 * after outputting the initial block macro.
 * The content in "b" has not been escaped.
 */
static int
rndr_meta_multi(struct nroff *st, struct bnodeq *obq, const char *b,
    const char *env)
{
	const char	*start;
	size_t		 sz, i, bsz;
	struct bnode	*bn;

	if (b == NULL)
		return 1;

	bsz = strlen(b);

	if (bqueue_block(obq, env) == NULL)
		return 0;

	for (i = 0; i < bsz; i++) {
		while (i < bsz &&
		       isspace((unsigned char)b[i]))
			i++;
		if (i == bsz)
			continue;
		start = &b[i];

		for (; i < bsz; i++)
			if (i < bsz - 1 &&
			    isspace((unsigned char)b[i]) &&
			    isspace((unsigned char)b[i + 1]))
				break;
		if ((sz = &b[i] - start) == 0)
			continue;

		if ((bn = bqueue_block(obq, NULL)) == NULL)
			return 0;
		if ((bn->buf = strndup(start, sz)) == NULL)
			return 0;
	}

	return 1;
}

/*
 * Fill "mq" by serialising child nodes into strings.  The serialised
 * strings are escaped.
 */
static int
rndr_meta(struct nroff *st, const struct lowdown_node *n,
    struct lowdown_metaq *mq)
{
	struct lowdown_meta	*m;
	ssize_t			 val;
	const char		*ep;

	if ((m = lowdown_get_meta(n, mq)) == NULL)
		return 0;

	if (strcasecmp(m->key, "shiftheadinglevelby") == 0) {
		val = (ssize_t)strtonum(m->value, -100, 100, &ep);
		if (ep == NULL)
			st->headers_offs = val + 1;
	} else if (strcasecmp(m->key, "baseheaderlevel") == 0) {
		val = (ssize_t)strtonum(m->value, 1, 100, &ep);
		if (ep == NULL)
			st->headers_offs = val;
	} else if (strcasecmp(m->key, "section") == 0) {
		st->headers_sec = m->value;
	}

	return 1;
}

static int
rndr_root(struct nroff *st, struct bnodeq *obq,
    const struct lowdown_node *n, struct bnodeq *bq,
    const struct lowdown_metaq *mq)
{
	struct lowdown_buf		*ob = NULL;
	const struct lowdown_node	*nn;
	struct bnode			*bn;
	const struct lowdown_meta	*m;
	int				 rc = 0;
	size_t				 sz;
	char				*abuf = NULL, *cp;
	const char			*author = NULL, *title = NULL,
					*affil = NULL, *date = NULL,
					*copy = NULL, *sec = NULL,
					*rcsauthor = NULL, *rcsdate = NULL,
					*source = NULL, *volume = NULL,
					*msheader = NULL,
					*manheader = NULL;

	if (!(st->flags & LOWDOWN_STANDALONE)) {
		TAILQ_CONCAT(obq, bq, entries);
		return 1;
	} else if (st->templ != NULL) {
		TAILQ_CONCAT(obq, bq, entries);
		return 1;
	}

	TAILQ_FOREACH(m, mq, entries)
		if (strcasecmp(m->key, "author") == 0)
			author = m->value;
		else if (strcasecmp(m->key, "copyright") == 0)
			copy = m->value;
		else if (strcasecmp(m->key, "affiliation") == 0)
			affil = m->value;
		else if (strcasecmp(m->key, "date") == 0)
			date = m->value;
		else if (strcasecmp(m->key, "rcsauthor") == 0)
			rcsauthor = rcsauthor2str(m->value);
		else if (strcasecmp(m->key, "rcsdate") == 0)
			rcsdate = rcsdate2str(m->value);
		else if (strcasecmp(m->key, "title") == 0)
			title = m->value;
		else if (strcasecmp(m->key, "section") == 0)
			sec = m->value;
		else if (strcasecmp(m->key, "source") == 0)
			source = m->value;
		else if (strcasecmp(m->key, "volume") == 0)
			volume = m->value;
		else if (strcasecmp(m->key, "msheader") == 0)
			msheader = m->value;
		else if (strcasecmp(m->key, "manheader") == 0)
			manheader = m->value;

	/* Overrides. */

	if (sec == NULL)
		sec = "7";
	if (rcsdate != NULL)
		date = rcsdate;
	if (rcsauthor != NULL)
		author = rcsauthor;

	bn = bqueue_block(obq, 
		".\\\" -*- mode: troff; coding: utf-8 -*-");
	if (bn == NULL)
		goto out;

	if (st->type == LOWDOWN_MS) {
		if (copy != NULL) {
			bn = bqueue_block(obq,
				".ds LF Copyright \\(co");
			if (bn == NULL)
				goto out;
			if ((bn->args = strdup(copy)) == NULL)
				goto out;
		}
		if (date != NULL) {
			if (copy != NULL)
				bn = bqueue_block(obq, ".ds RF");
			else
				bn = bqueue_block(obq, ".DA");
			if (bn == NULL)
				goto out;
			if ((bn->args = strdup(date)) == NULL)
				goto out;
		}

		if (msheader != NULL &&
		    bqueue_block(obq, msheader) == NULL)
			goto out;

		/*
		 * The title is required if having an author or
		 * affiliation; however, it doesn't make a difference if
		 * there are none of these elements, so specify it
		 * anyway.
		 */

		if (bqueue_block(obq, ".TL") == NULL)
			goto out;
		if (title != NULL) {
			if ((bn = bqueue_span(obq, NULL)) == NULL)
				goto out;
			if ((bn->buf = strdup(title)) == NULL)
				goto out;
		}
		
		/*
		 * XXX: in groff_ms(7), it's stipulated that multiple
		 * authors get multiple AU invocations.  However, these
		 * are grouped with the subsequent AI.  In lowdown, we
		 * accept all authors and institutions at once (without
		 * grouping), so simply put these all together.
		 */

		if (!rndr_meta_multi(st, obq, author, ".AU"))
			goto out;
		if (!rndr_meta_multi(st, obq, affil, ".AI"))
			goto out;
	} else if (st->type == LOWDOWN_MDOC) {
		/*
		 * If a title has been specified in the metadata,
		 * uppercase it.  Otherwise, try to infer it from the
		 * NAME section, if provided, by parsing out the first
		 * text node of the first paragraph.  If found,
		 * uppercase that as well.  If there's no title at all,
		 * use UNKNOWN as a stand-in.
		 */

		if (title == NULL &&
		    (n = TAILQ_FIRST(&n->children)) != NULL &&
		    n->type == LOWDOWN_DOC_HEADER &&
		    (n = TAILQ_NEXT(n, entries)) != NULL &&
		    n->type == LOWDOWN_HEADER &&
		    (nn = TAILQ_FIRST(&n->children)) != NULL &&
		    nn->type == LOWDOWN_NORMAL_TEXT &&
		    hbuf_streq(&nn->rndr_normal_text.text, "NAME") &&
		    (n = TAILQ_NEXT(n, entries)) != NULL &&
		    n->type == LOWDOWN_PARAGRAPH &&
		    (nn = TAILQ_FIRST(&n->children)) != NULL &&
		    nn->type == LOWDOWN_NORMAL_TEXT) {
			abuf = hbuf_string_trim(&nn->rndr_normal_text.text);
			if (abuf == NULL)
				goto out;
			if ((sz = strcspn(abuf, "\\-, ")) > 0)
				abuf[sz] = '\0';
			for (cp = abuf; *cp != '\0'; cp++)
				*cp = toupper((unsigned char)*cp);
			title = abuf;
		} else if (title != NULL) {
			if ((abuf = strdup(title)) == NULL)
				goto out;
			for (cp = abuf; *cp != '\0'; cp++)
				*cp = toupper((unsigned char)*cp);
			title = abuf;
		} else
			title = "UNKNOWN";

		if (date == NULL)
			date = "$Mdocdate$";
		if ((bn = bqueue_block(obq, ".Dd")) == NULL ||
		    (bn->args = strdup(date)) == NULL)
			goto out;
		if ((bn = bqueue_block(obq, ".Dt")) == NULL)
			goto out;
		if (asprintf(&bn->args, "%s %s", title, sec) == -1) {
			bn->args = NULL;
			goto out;
		}
		if ((bn = bqueue_block(obq, ".Os")) == NULL)
			goto out;
	} else {
		if (manheader != NULL &&
		    bqueue_block(obq, manheader) == NULL)
			goto out;

		if ((ob = hbuf_new(32)) == NULL)
			goto out;

		/*
		 * The syntax of this macro, according to man(7), is 
		 * TH name section date [source [volume]].
		 */

		if ((bn = bqueue_block(obq, ".TH")) == NULL)
			goto out;
		if (!HBUF_PUTSL(ob, "\""))
			goto out;
		if (title != NULL &&
		    !lowdown_roff_esc(ob, title, strlen(title), 1, 0))
			goto out;
		if (!HBUF_PUTSL(ob, "\" \""))
			goto out;
		if (!lowdown_roff_esc(ob, sec, strlen(sec), 1, 0))
			goto out;
		if (!HBUF_PUTSL(ob, "\" \""))
			goto out;

		/*
		 * We may not have a date (or it may be empty), in which
		 * case man(7) says the current date is used.
		 */

		if (date != NULL &&
		    !lowdown_roff_esc(ob, date, strlen(date), 1, 0))
			goto out;
		if (!HBUF_PUTSL(ob, "\""))
			goto out;

		/*
		 * Don't print these unless necessary, as the latter
		 * overrides the default system printing for the
		 * section.
		 */

		if (source != NULL || volume != NULL) {
			if (!HBUF_PUTSL(ob, " \""))
				goto out;
			if (source != NULL && !lowdown_roff_esc
			    (ob, source, strlen(source), 1, 0))
				goto out;
			if (!HBUF_PUTSL(ob, "\""))
				goto out;
			if (!HBUF_PUTSL(ob, " \""))
				goto out;
			if (volume != NULL && !lowdown_roff_esc
			    (ob, volume, strlen(volume), 1, 0))
				goto out;
			if (!HBUF_PUTSL(ob, "\""))
				goto out;
		}
		if ((bn->nargs = hbuf_string(ob)) == NULL)
			goto out;
	}

	rc = 1;
out:
	TAILQ_CONCAT(obq, bq, entries);
	hbuf_free(ob);
	free(abuf);
	return rc;
}

/*
 * Actually render the node "n" and all of its children into the output
 * buffer "ob", chopping "chop" from the current node if specified.
 * Return what (if anything) we should chop from the next node or <0 on
 * failure.
 */
static int
rndr(struct lowdown_metaq *mq, struct nroff *st,
    const struct lowdown_node *n, struct bnodeq *obq)
{
	const struct lowdown_node	*child;
	int				 rc = 1, use_lp = st->use_lp;
	enum nfont			 fonts[NFONT__MAX];
	struct bnodeq			 tmpbq;
	struct bnode			*bn;

	TAILQ_INIT(&tmpbq);

	if ((n->chng == LOWDOWN_CHNG_INSERT ||
	     n->chng == LOWDOWN_CHNG_DELETE) &&
	    !bqueue_colour(obq, n->chng, 0))
		goto out;

	/*
	 * Font management.
	 * roff doesn't handle its own font stack, so we can't set fonts
	 * and step out of them in a nested way.
	 */

	memcpy(fonts, st->fonts, sizeof(fonts));

	switch (n->type) {
	case LOWDOWN_CODESPAN:
	case LOWDOWN_EMPHASIS:
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
	case LOWDOWN_TRIPLE_EMPHASIS:
		if (!rndr_font_pre(st, n, obq))
			goto out;
		break;
	default:
		break;
	}

	/* Guard against flushing footnotes. */

	if (n->type == LOWDOWN_FOOTNOTE)
		st->footdepth++;

	/* Parse child content into a temporary queue. */

	TAILQ_FOREACH(child, &n->children, entries)
		if (!rndr(mq, st, child, &tmpbq))
			goto out;

	/* Process each node, optionally passing child content. */

	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
		rc = rndr_blockcode(st, obq, n);
		break;
	case LOWDOWN_BLOCKQUOTE:
		rc = rndr_blockquote(st, obq, &tmpbq);
		break;
	case LOWDOWN_DEFINITION:
		rc = rndr_list(st, obq, n, &tmpbq);
		break;
	case LOWDOWN_DEFINITION_DATA:
		rc = rndr_definition_data(st, obq, &tmpbq);
		break;
	case LOWDOWN_DEFINITION_TITLE:
		rc = rndr_definition_title(st, obq, &tmpbq);
		break;
	case LOWDOWN_DOC_HEADER:
		break;
	case LOWDOWN_META:
		if (n->chng != LOWDOWN_CHNG_DELETE)
			rc = rndr_meta(st, n, mq);
		break;
	case LOWDOWN_HEADER:
		rc = rndr_header(st, obq, &tmpbq, n);
		break;
	case LOWDOWN_HRULE:
		rc = rndr_hrule(st, obq);
		break;
	case LOWDOWN_LIST:
		rc = rndr_list(st, obq, n, &tmpbq);
		break;
	case LOWDOWN_LISTITEM:
		rc = rndr_listitem(st,
			obq, n, &tmpbq, &n->rndr_listitem);
		break;
	case LOWDOWN_PARAGRAPH:
		rc = rndr_paragraph(st, n, obq, &tmpbq);
		break;
	case LOWDOWN_TABLE_BLOCK:
		rc = rndr_table(st, obq, &tmpbq);
		break;
	case LOWDOWN_TABLE_HEADER:
		rc = rndr_table_header(st, 
			obq, &tmpbq, &n->rndr_table_header);
		break;
	case LOWDOWN_TABLE_ROW:
		rc = rndr_table_row(st, obq, &tmpbq);
		break;
	case LOWDOWN_TABLE_CELL:
		rc = rndr_table_cell(obq, &tmpbq, &n->rndr_table_cell);
		break;
	case LOWDOWN_ROOT:
		assert(st->footdepth == 0);
		rc = rndr_footnotes(st, &tmpbq, 1) &&
			rndr_root(st, obq, n, &tmpbq, mq);
		break;
	case LOWDOWN_BLOCKHTML:
		rc = rndr_raw_block(st, obq, &n->rndr_blockhtml);
		break;
	case LOWDOWN_LINK_AUTO:
		rc = rndr_autolink(st, obq, n);
		break;
	case LOWDOWN_EMPHASIS:
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
	case LOWDOWN_TRIPLE_EMPHASIS:
		rc = rndr_font(st, obq, n, &tmpbq);
		break;
	case LOWDOWN_CODESPAN:
		rc = rndr_codespan(st, obq, &n->rndr_codespan);
		break;
	case LOWDOWN_IMAGE:
		rc = rndr_image(st, obq, &n->rndr_image);
		break;
	case LOWDOWN_LINEBREAK:
		rc = rndr_linebreak(st, obq);
		break;
	case LOWDOWN_LINK:
		rc = rndr_link(st, obq, &tmpbq, n);
		break;
	case LOWDOWN_SUBSCRIPT:
		rc = rndr_superscript(obq, &tmpbq, n->type);
		break;
	case LOWDOWN_SUPERSCRIPT:
		rc = rndr_superscript(obq, &tmpbq, n->type);
		break;
	case LOWDOWN_FOOTNOTE:
		rc = rndr_footnote_ref(st, obq, &tmpbq);
		assert(st->footdepth > 0);
		st->footdepth--;
		/*
		 * Restore what subsequent paragraphs should do.  This
		 * macro will create output that's delayed in being
		 * shown.  It might set use_lp, which we don't want
		 * to propagate to the actual output that will follow.
		 */
		st->use_lp = use_lp;
		break;
	case LOWDOWN_RAW_HTML:
		rc = rndr_raw_html(st, obq, &n->rndr_raw_html);
		break;
	case LOWDOWN_NORMAL_TEXT:
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			goto out;
		bn->buf = hbuf_string(&n->rndr_normal_text.text);
		if (bn->buf == NULL)
			goto out;
		break;
	case LOWDOWN_ENTITY:
		rc = rndr_entity(st, obq, &n->rndr_entity);
		break;
	default:
		TAILQ_CONCAT(obq, &tmpbq, entries);
		break;
	}

	if (rc == 0)
		goto out;

	/* Restore the font stack. */

	switch (n->type) {
	case LOWDOWN_CODESPAN:
	case LOWDOWN_EMPHASIS:
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
	case LOWDOWN_TRIPLE_EMPHASIS:
		if ((rc = rndr_font_post(st, n, fonts, obq)) == 0)
			goto out;
		break;
	default:
		break;
	}

	/* Process insert/delete content. */

	if ((n->chng == LOWDOWN_CHNG_INSERT ||
	     n->chng == LOWDOWN_CHNG_DELETE) &&
	    !bqueue_colour(obq, n->chng, 1)) {
		rc = 0;
		goto out;
	}

	/*
	 * Flush out all existing footnotes, if applicable.  This is
	 * because in -tms with footnotes, footnotes will go on the same
	 * page as the footnote definition, but only if the FS/FE is
	 * declared on the same page as well.  This is delayed until a
	 * block because, if it were produced alongside the .pdfhref
	 * macro, it would confuse spacing in the event, of, e.g.,
	 * foo[^ref]bar.
	 */
	
	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
	case LOWDOWN_BLOCKQUOTE:
	case LOWDOWN_PARAGRAPH:
	case LOWDOWN_DEFINITION:
	case LOWDOWN_LIST:
	case LOWDOWN_HEADER:
	case LOWDOWN_TABLE_BLOCK:
		if (!rndr_footnotes(st, obq, 0)) {
			rc = 0;
			goto out;
		}
		break;
	default:
		break;
	}
out:
	bqueue_free(&tmpbq);
	return rc;
}

int
lowdown_roff_rndr(struct lowdown_buf *ob, void *arg,
    const struct lowdown_node *n)
{
	struct nroff		*st = arg;
	struct lowdown_buf	*tmp = NULL;
	struct lowdown_metaq	 metaq;
	int			 rc = 0;
	struct bnodeq		 bq;
	size_t			 i;

	TAILQ_INIT(&metaq);
	TAILQ_INIT(&bq);
	TAILQ_INIT(&st->headers_used);

	memset(st->fonts, 0, sizeof(st->fonts));
	st->headers_offs = 1;
	st->headers_sec = NULL;
	st->use_lp = 0;

	if (rndr(&metaq, st, n, &bq)) {
		if ((tmp = hbuf_new(64)) == NULL)
			goto out;
		if (!bqueue_flush(st, tmp, &bq, 0))
			goto out;
		if (tmp->size && tmp->data[tmp->size - 1] != '\n' &&
		    !hbuf_putc(tmp, '\n'))
			goto out;
		rc = st->templ == NULL ? hbuf_putb(ob, tmp) :
			lowdown_template(st->templ, tmp, ob, &metaq, 0);
	}

out:
	hbuf_free(tmp);

	for (i = 0; i < st->namesz; i++)
		free(st->names[i]);
	free(st->names);
	st->names = NULL;
	st->namesz = 0;

	for (i = 0; i < st->footsz; i++) {
		bqueue_free(st->foots[i]);
		free(st->foots[i]);
	}
	free(st->foots);
	st->foots = NULL;
	st->footsz = st->footpos = 0;

	lowdown_metaq_free(&metaq);
	bqueue_free(&bq);
	hentryq_clear(&st->headers_used);
	return rc;
}

void *
lowdown_roff_new(const struct lowdown_opts *opts)
{
	struct nroff 	*p;

	if ((p = calloc(1, sizeof(struct nroff))) == NULL)
		return NULL;

	p->flags = opts != NULL ? opts->oflags : 0;
	p->type = opts == NULL ? LOWDOWN_MS : opts->type;
	p->cr = opts != NULL ? opts->nroff.cr : NULL;
	p->cb = opts != NULL ? opts->nroff.cb : NULL;
	p->ci = opts != NULL ? opts->nroff.ci : NULL;
	p->cbi = opts != NULL ? opts->nroff.cbi : NULL;
	p->templ = opts != NULL ? opts->templ : NULL;

	/*
	 * Set the default "constant width" fonts.  This is complicated
	 * because the "C" fixed-with font is not universally available
	 * on all output media.  For example, -Tascii does not carry a
	 * "C" font.  However, this is a good enough default.
	 */

	if (p->cr == NULL)
		p->cr = "CR";
	if (p->cb == NULL)
		p->cb = "CB";
	if (p->ci == NULL)
		p->ci = "CI";
	if (p->cbi == NULL)
		p->cbi = "CBI";

	/*
	 * Set a default indentation.  For -man, we use 3 because we
	 * want to be efficient with page width.  For -ms, use 5 because
	 * that'll usually be used for PDF with a variable-width font.
	 */

	p->indent = p->type == LOWDOWN_MAN ? 3 : 5;
	return p;
}

void
lowdown_roff_free(void *arg)
{

	/* No need to check NULL: pass directly to free(). */

	free(arg);
}

void *
lowdown_nroff_new(const struct lowdown_opts *opts)
{
	return lowdown_roff_new(opts);
}

void
lowdown_nroff_free(void *arg)
{
	lowdown_roff_free(arg);
}

int
lowdown_nroff_rndr(struct lowdown_buf *ob, void *arg,
    const struct lowdown_node *n)
{
	return lowdown_roff_rndr(ob, arg, n);
}

