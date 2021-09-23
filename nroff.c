/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016--2021 Kristaps Dzonsons <kristaps@bsd.lv>
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

enum	nfont {
	NFONT_ITALIC = 0, /* italic */
	NFONT_BOLD, /* bold */
	NFONT_FIXED, /* fixed-width */
	NFONT__MAX
};

struct 	nroff {
	int 		 man; /* whether man(7) */
	int		 post_para; /* for choosing PP/LP */
	unsigned int 	 flags; /* output flags */
	ssize_t		 headers_offs; /* header offset */
	enum nfont	 fonts[NFONT__MAX]; /* see bqueue_font() */
};

enum	bscope {
	BSCOPE_BLOCK = 0,
	BSCOPE_SPAN = 1,
	BSCOPE_LITERAL,
	BSCOPE_FONT,
	BSCOPE_COLOUR
};

/*
 * Instead of writing directly into the output buffer, we write
 * temporarily into bnodes, which are converted into output.  These
 * nodes are aware of whether they need surrounding newlines.  This way,
 * we have much more control over where to put newlines, which before
 * was haphazard at best.
 */
struct	bnode {
	char				*nbuf; /* (safe) 1st data */
	char				*buf; /* (unsafe) 2nd data */
	size_t				 bufchop; /* strip from buf */
	char				*nargs; /* (safe) 1st args */
	char				*args; /* (unsafe) 2nd args */
	int				 close; /* BNODE_COLOUR/FONT */
	int				 tblhack; /* BSCOPE_SPAN */
	enum bscope			 scope; /* scope */
	unsigned int			 font; /* if BNODE_FONT */
#define	BFONT_ITALIC			 0x01
#define	BFONT_BOLD			 0x02
#define	BFONT_FIXED			 0x04
	unsigned int			 colour; /* if BNODE_COLOUR */
#define	BFONT_BLUE			 0x01
#define	BFONT_RED			 0x02
	TAILQ_ENTRY(bnode)		 entries;
};

TAILQ_HEAD(bnodeq, bnode);

/*
 * Escape unsafe text into roff output such that no roff fetaures are
 * invoked by the text (macros, escapes, etc.).
 * If "oneline" is non-zero, newlines are replaced with spaces.
 * If "literal", doesn't strip leading space.
 * Return zero on failure, non-zero on success.
 */
static int
hesc_nroff(struct lowdown_buf *ob, const char *data, 
	size_t size, int oneline, int literal, int esc)
{
	size_t	 i = 0;

	if (size == 0)
		return 1;
	if (!esc)
		return hbuf_put(ob, data, size);

	/* Strip leading whitespace. */

	if (!literal && ob->size > 0 && ob->data[ob->size - 1] == '\n')
		while (i < size && (data[i] == ' ' || data[i] == '\n'))
			i++;

	/*
	 * According to mandoc_char(7), we need to escape the backtick,
	 * single apostrophe, and tilde or else they'll be considered as
	 * special Unicode output.
	 * Slashes need to be escaped too.
	 * We also escape double-quotes because this text might be used
	 * within quoted macro arguments.
	 */

	for ( ; i < size; i++)
		switch (data[i]) {
		case '^':
			if (!HBUF_PUTSL(ob, "\\(ha"))
				return 0;
			break;
		case '~':
			if (!HBUF_PUTSL(ob, "\\(ti"))
				return 0;
			break;
		case '`':
			if (!HBUF_PUTSL(ob, "\\(ga"))
				return 0;
			break;
		case '"':
			if (!HBUF_PUTSL(ob, "\\(dq"))
				return 0;
			break;
		case '\n':
			if (!hbuf_putc(ob, oneline ? ' ' : '\n'))
				return 0;
			if (literal)
				break;

			/* Prevent leading spaces on the line. */

			while (i + 1 < size && 
			       (data[i + 1] == ' ' || 
				data[i + 1] == '\n'))
				i++;
			break;
		case '\\':
			if (!HBUF_PUTSL(ob, "\\e"))
				return 0;
			break;
		case '\'':
		case '.':
			if (!oneline &&
			    ob->size > 0 && 
			    ob->data[ob->size - 1] == '\n' &&
			    !HBUF_PUTSL(ob, "\\&"))
				return 0;
			/* FALLTHROUGH */
		default:
			if (!hbuf_putc(ob, data[i]))
				return 0;
			break;
		}

	return 1;
}

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
		strlcat(fonts, "black", sizeof(fonts));
	return fonts;
}

/*
 * For compatibility with traditional troff, return non-block font code
 * using the correct sequence of \fX, \f(xx, and \f[xxx].
 */
static const char *
nstate_font_buf(unsigned int ft, int blk)
{
	static char 	 fonts[10];
	char		*cp = fonts;
	size_t		 len = 0;

	if (ft & BFONT_FIXED)
		len++;
	if (ft & BFONT_BOLD)
		len++;
	if (ft & BFONT_ITALIC)
		len++;
	if (ft == 0)
		len++;

	if (!blk && len == 3)
		(*cp++) = '[';
	else if (!blk && len == 2)
		(*cp++) = '(';

	if (ft & BFONT_FIXED)
		(*cp++) = 'C';
	if (ft & BFONT_BOLD)
		(*cp++) = 'B';
	if (ft & BFONT_ITALIC)
		(*cp++) = 'I';
	if (ft == 0)
		(*cp++) = 'R';

	if (!blk && len == 3)
		(*cp++) = ']';

	(*cp++) = '\0';
	return fonts;
}

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

static int
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

static struct bnode *
bqueue_span(struct bnodeq *bq, const char *text)
{
	struct bnode	*bn;

	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return NULL;
	bn->scope = BSCOPE_SPAN;
	if (text != NULL && (bn->nbuf = strdup(text)) == NULL) {
		free(bn);
		return NULL;
	}
	TAILQ_INSERT_TAIL(bq, bn, entries);
	return bn;
}

static struct bnode *
bqueue_block(struct bnodeq *bq, const char *macro)
{
	struct bnode	*bn;

	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return NULL;
	bn->scope = BSCOPE_BLOCK;
	if (macro != NULL && (bn->nbuf = strdup(macro)) == NULL) {
		free(bn);
		return NULL;
	}
	TAILQ_INSERT_TAIL(bq, bn, entries);
	return bn;
}

static void
bnode_free(struct bnode *bn)
{

	free(bn->args);
	free(bn->nargs);
	free(bn->nbuf);
	free(bn->buf);
	free(bn);
}

static void
bqueue_free(struct bnodeq *bq)
{
	struct bnode	*bn;

	while ((bn = TAILQ_FIRST(bq)) != NULL) {
		TAILQ_REMOVE(bq, bn, entries);
		bnode_free(bn);
	}
}

static void
bqueue_strip_paras(struct bnodeq *bq)
{
	struct bnode	*bn;

	while ((bn = TAILQ_FIRST(bq)) != NULL) {
		if (bn->scope != BSCOPE_BLOCK || bn->nbuf == NULL)
			break;
		if (strcmp(bn->nbuf, ".PP") &&
		    strcmp(bn->nbuf, ".IP") &&
		    strcmp(bn->nbuf, ".LP"))
			break;
		TAILQ_REMOVE(bq, bn, entries);
		bnode_free(bn);
	}
}

static int
bqueue_flush(struct lowdown_buf *ob, const struct bnodeq *bq, int esc)
{
	const struct bnode	*bn, *chk;
	const char		*cp;
	int		 	 nextblk;

	TAILQ_FOREACH(bn, bq, entries) {
		nextblk = 0;

		/* 
		 * Block scopes start with a newline.
		 * Also have colours use their own block, as otherwise
		 * (bugs in groff?) inline colour selection after a
		 * hyperlink macro causes line breaks.
		 * Besides, having spaces around changing colour, which
		 * indicates differences, improves readability.
		 */

		if (bn->scope == BSCOPE_BLOCK ||
		    bn->scope == BSCOPE_COLOUR) {
			if (ob->size > 0 && 
			    ob->data[ob->size - 1] != '\n' &&
			    !hbuf_putc(ob, '\n'))
				return 0;
			nextblk = 1;
		}

		/* 
		 * Fonts can either be macros or inline depending upon
		 * where they set relative to a macro block.
		 */

		if (bn->scope == BSCOPE_FONT) {
			chk = bn->close ?
				TAILQ_PREV(bn, bnodeq, entries) :
				TAILQ_NEXT(bn, entries);
			if (chk != NULL && 
			    chk->scope == BSCOPE_BLOCK) {
				if (ob->size > 0 && 
				    ob->data[ob->size - 1] != '\n' &&
				    !hbuf_putc(ob, '\n'))
					return 0;
				nextblk = 1;
			}
		}

		/* Print font and colour escapes. */

		if (bn->scope == BSCOPE_FONT && nextblk) {
			if (!hbuf_printf(ob, ".ft %s",
			    nstate_font_buf(bn->font, nextblk)))
				return 0;
		} else if (bn->scope == BSCOPE_FONT) {
			if (!hbuf_printf(ob, "\\f%s", 
			    nstate_font_buf(bn->font, nextblk)))
				return 0;
		} else if (bn->scope == BSCOPE_COLOUR) {
			assert(nextblk);
			if (!hbuf_printf(ob, ".gcolor %s", 
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

		/* Safe data need not be escaped. */

		if (bn->nbuf != NULL && !hbuf_puts(ob, bn->nbuf))
			return 0;

		/* Unsafe data must be escaped. */

		if (bn->scope == BSCOPE_LITERAL) {
			assert(bn->buf != NULL);
			if (!hesc_nroff(ob, bn->buf,
			    strlen(bn->buf), 0, 1, esc))
				return 0;
		} else if (bn->buf != NULL)
			if (!hesc_nroff(ob, bn->buf + bn->bufchop, 
			    strlen(bn->buf) - bn->bufchop, 0, 0, esc))
				return 0;

		/* 
		 * Macro arguments follow after space.
		 * These must all be printed on the same line.
		 */

		if (bn->nargs != NULL) {
			assert(nextblk);
			assert(bn->scope == BSCOPE_BLOCK);
			if (!hbuf_putc(ob, ' '))
				return 0;
			for (cp = bn->nargs; *cp != '\0'; cp++)
				if (!hbuf_putc(ob, 
				    *cp == '\n' ? ' ' : *cp))
					return 0;
		}

		if (bn->args != NULL) {
			assert(nextblk);
			assert(bn->scope == BSCOPE_BLOCK);
			if (!hbuf_putc(ob, ' '))
				return 0;
			if (!hesc_nroff(ob, bn->args,
			    strlen(bn->args), 1, 0, esc))
				return 0;
		}

		/* Finally, trailing newline. */

		if (nextblk && ob->size > 0 && 
		    ob->data[ob->size - 1] != '\n' &&
		    !hbuf_putc(ob, '\n'))
			return 0;
	}

	return 1;
}

static int
bqueue_to_nargs(struct bnode *bn, const struct bnodeq *bq, int quoted)
{
	struct lowdown_buf	*ob;
	int			 rc = 0;

	if ((ob = hbuf_new(32)) == NULL)
		goto out;
	if (quoted && !hbuf_putc(ob, '"'))
		goto out;
	if (!bqueue_flush(ob, bq, 1))
		goto out;
	if (quoted && !hbuf_putc(ob, '"'))
		goto out;
	assert(bn->nargs == NULL);
	if ((bn->nargs = strndup(ob->data, ob->size)) == NULL)
		goto out;
	rc = 1;
out:
	hbuf_free(ob);
	return rc;
}

/*
 * Convert a link into a short-link and place the escaped output into a
 * returned string.
 * Returns NULL on memory allocation failure.
 */
static char *
hbuf2shortlink(const struct lowdown_buf *link)
{
	struct lowdown_buf	*tmp = NULL, *slink = NULL;
	char			*ret = NULL;

	if ((tmp = hbuf_new(32)) == NULL)
		goto out;
	if ((slink = hbuf_new(32)) == NULL)
		goto out;
	if (!hbuf_shortlink(tmp, link))
		goto out;
	if (!hesc_nroff(slink, tmp->data, tmp->size, 1, 0, 1))
		goto out;
	ret = strndup(slink->data, slink->size);
out:
	hbuf_free(tmp);
	hbuf_free(slink);
	return ret;
}

/*
 * Manage hypertext linking with the groff "pdfhref" macro or simply
 * using italics.
 * We use italics because the UR/UE macro doesn't support leading
 * un-spaced content, so "[foo](https://foo.com)" wouldn't work.
 * Until a solution is found, let's just italicise the link text (or
 * link, if no text is found).
 * Return <0 on error (memory), 0 if we've snipped the next node, >0
 * otherwise.
 */
static ssize_t
putlink(struct bnodeq *obq, struct nroff *st, 
	const struct lowdown_buf *link, struct bnodeq *bq,
	enum halink_type type, const struct lowdown_node *next)
{
	struct lowdown_buf		*ob = NULL, *tmp = NULL;
	const struct lowdown_buf	*nbuf;
	struct bnode			*bn, *prev;
	size_t				 sz, i;
	int				 rc = 0;
	ssize_t				 ret = 0;

	/*
	 * For -Tman or without .pdfhref, format the link as-is, with
	 * text then link, or use the various shorteners.
	 */

	if (st->man || !(st->flags & LOWDOWN_NROFF_GROFF)) {
		if (bq == NULL) {
			st->fonts[NFONT_ITALIC]++;
			if (!bqueue_font(st, obq, 0))
				goto out;
			if ((bn = bqueue_span(obq, NULL)) == NULL)
				goto out;
			if (st->flags & LOWDOWN_NROFF_SHORTLINK) {
				bn->nbuf = hbuf2shortlink(link);
				if (bn->nbuf == NULL)
					goto out;
			} else {
				bn->buf = strndup(link->data, link->size);
				if (bn->buf == NULL)
					goto out;
			}
			st->fonts[NFONT_ITALIC]--;
			if (!bqueue_font(st, obq, 1))
				goto out;
			rc = 1;
			goto out;
		}
		st->fonts[NFONT_BOLD]++;
		if (!bqueue_font(st, obq, 0))
			goto out;
		TAILQ_CONCAT(obq, bq, entries);
		st->fonts[NFONT_BOLD]--;
		if (!bqueue_font(st, obq, 1))
			goto out;
		if (st->flags & LOWDOWN_NROFF_NOLINK) {
			rc = 1;
			goto out;
		}
		if (bqueue_span(obq, " (") == NULL)
			goto out;
		st->fonts[NFONT_ITALIC]++;
		if (!bqueue_font(st, obq, 0))
			goto out;
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			goto out;
		if (st->flags & LOWDOWN_NROFF_SHORTLINK) {
			bn->nbuf = hbuf2shortlink(link);
			if (bn->nbuf == NULL)
				goto out;
		} else {
 			bn->buf = strndup(link->data, link->size);
			if (bn->buf == NULL)
				goto out;
		}
		st->fonts[NFONT_ITALIC]--;
		if (!bqueue_font(st, obq, 1))
			goto out;
		if (bqueue_span(obq, ")") == NULL)
			goto out;
		rc = 1;
		goto out;
	}

	/* Otherwise, use .pdfhref. */

	if ((ob = hbuf_new(32)) == NULL)
		goto out;

	/*
	 * If we're preceded by normal text that doesn't end with space,
	 * then put that text into the "-P" (prefix) argument.
	 */

	prev = TAILQ_LAST(obq, bnodeq);
	if (prev != NULL &&
	    prev->scope == BSCOPE_SPAN &&
	    prev->buf != NULL &&
	    strlen(prev->buf) > 0 && !isspace
	    ((unsigned char)prev->buf[strlen(prev->buf) - 1])) {
		sz = strlen(prev->buf);
		while (sz && !isspace
		       ((unsigned char)prev->buf[sz - 1]))
			sz--;
		assert(sz != strlen(prev->buf));

		if (!HBUF_PUTSL(ob, "-P \""))
			goto out;
		if (!hesc_nroff(ob, &prev->buf[sz],
		    strlen(prev->buf) - sz, 1, 0, 1))
			goto out;
		if (!HBUF_PUTSL(ob, "\" "))
			goto out;

		if ((tmp = hbuf_new(32)) == NULL)
			goto out;
		if (!hesc_nroff(tmp, prev->buf, sz, 1, 0, 1))
			goto out;
		assert(prev->nbuf == NULL);
		prev->nbuf = strndup(tmp->data, tmp->size);
		if (prev->nbuf == NULL)
			goto out;
		free(prev->buf);
		prev->buf = NULL;
	}

	/* 
	 * If we're followed by text flush against the node, "chop" the
	 * node until the next white-space.  This will be handed to the
	 * next rndr() invocation.
	 */

	if (next != NULL && 
	    next->type == LOWDOWN_NORMAL_TEXT) {
		nbuf = &next->rndr_normal_text.text;
		for (sz = 0; sz < nbuf->size; sz++)
			if (isspace((unsigned char)nbuf->data[sz]))
				break;
		if (sz > 0) {
			if (!HBUF_PUTSL(ob, "-A \""))
				goto out;
			if (!hesc_nroff(ob, nbuf->data, sz, 1, 0, 1))
				goto out;
			if (!HBUF_PUTSL(ob, "\" "))
				goto out;
			ret = sz;
		}
	}

	/* Encode the URL. */

	if (!HBUF_PUTSL(ob, "-D "))
		goto out;
	if (type == HALINK_EMAIL && !HBUF_PUTSL(ob, "mailto:"))
		goto out;
	for (i = 0; i < link->size; i++) {
		if (!isprint((unsigned char)link->data[i]) ||
		    strchr("<>\\^`{|}\"", link->data[i]) != NULL) {
			if (!hbuf_printf(ob, "%%%.2X", link->data[i]))
				goto out;
		} else if (!hbuf_putc(ob, link->data[i]))
			goto out;
	}

	if (!HBUF_PUTSL(ob, " "))
		goto out;
	if (bq == NULL && !hbuf_putb(ob, link))
		goto out;
	else if (bq != NULL && !bqueue_flush(ob, bq, 1))
		goto out;
	if ((bn = bqueue_block(obq, ".pdfhref W")) == NULL)
		goto out;
	if ((bn->nargs = strndup(ob->data, ob->size)) == NULL)
		goto out;

	rc = 1;
out:
	hbuf_free(tmp);
	hbuf_free(ob);
	return rc ? ret : -1;
}

/*
 * Return <0 on failure, 0 to remove next, >0 otherwise.
 */
static ssize_t
rndr_autolink(struct nroff *st, struct bnodeq *obq,
	const struct rndr_autolink *param, 
	const struct lowdown_node *next)
{

	return putlink(obq, st, &param->link, NULL, param->type, next);
}

static int
rndr_blockcode(const struct nroff *st, struct bnodeq *obq,
	const struct rndr_blockcode *param)
{
	struct bnode	*bn;

	/*
	 * XXX: intentionally don't use LD/DE because it introduces
	 * vertical space.  This means that subsequent blocks
	 * (paragraphs, etc.) will have a double-newline.
	 */

	if (bqueue_block(obq, ".LP") == NULL)
		return 0;

	if (st->man && (st->flags & LOWDOWN_NROFF_GROFF)) {
		if (bqueue_block(obq, ".EX") == NULL)
			return 0;
	} else {
		if (bqueue_block(obq, ".nf") == NULL)
			return 0;
		if (bqueue_block(obq, ".ft CR") == NULL)
			return 0;
	}

	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(obq, bn, entries);
	bn->scope = BSCOPE_LITERAL;
	bn->buf = strndup(param->text.data, param->text.size);
	if (bn->buf == NULL)
		return 0;

	if (st->man && (st->flags & LOWDOWN_NROFF_GROFF))
		return bqueue_block(obq, ".EE") != NULL;

	if (bqueue_block(obq, ".ft") == NULL)
		return 0;
	return bqueue_block(obq, ".fi") != NULL;
}

static int
rndr_definition_title(struct bnodeq *obq, struct bnodeq *bq)
{
	struct bnode	*bn;

	if ((bn = bqueue_block(obq, ".IP")) == NULL)
		return 0;
	return bqueue_to_nargs(bn, bq, 1);
}

static int
rndr_definition_data(struct bnodeq *obq, struct bnodeq *bq)
{
	/* 
	 * Strip out leading paragraphs.
	 * XXX: shouldn't these all be handled by the child list item?
	 */

	bqueue_strip_paras(bq);
	TAILQ_CONCAT(obq, bq, entries);
	return 1;
}

static int
rndr_list(struct nroff *st, struct bnodeq *obq, 
	const struct lowdown_node *n, struct bnodeq *bq)
{
	/* 
	 * If we have a nested list, we need to use RS/RE to indent the
	 * nested component.  Otherwise the `IP` used for the titles and
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

	st->post_para = 1;
	return 1;
}

static int
rndr_blockquote(struct nroff *st, 
	struct bnodeq *obq, struct bnodeq *bq)
{

	if (bqueue_block(obq, ".RS") == NULL)
		return 0;
	TAILQ_CONCAT(obq, bq, entries);
	st->post_para = 1;
	return bqueue_block(obq, ".RE") != NULL;
}

static int
rndr_codespan(struct bnodeq *obq, const struct rndr_codespan *param)
{
	struct bnode	*bn;

	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;
	bn->buf = strndup(param->text.data, param->text.size);
	return bn->buf != NULL;
}

static int
rndr_linebreak(struct bnodeq *obq)
{

	return bqueue_block(obq, ".br") != NULL;
}

static int
rndr_header(struct nroff *st, struct bnodeq *obq,
	struct bnodeq *bq, const struct rndr_header *param)
{
	ssize_t			 level;
	struct bnode		*bn;

	level = (ssize_t)param->level + st->headers_offs;
	if (level < 1)
		level = 1;

	/*
	 * For man(7), we use SH for the first-level section, SS for
	 * other sections.
	 * FIXME: use PP then italics or something for third-level etc.
	 * For ms(7), just use SH.
	 * If we're using ms(7) w/groff extensions and w/o numbering,
	 * used the numbered version of the SH macro.
	 * If we're numbered ms(7), use NH.
	 * With groff extensions, use XN (-mspdf).
	 */

	if (st->man) {
		bn = level == 1 ?
			bqueue_block(obq, ".SH") :
			bqueue_block(obq, ".SS");
		if (bn == NULL)
			return 0;
		return bqueue_to_nargs(bn, bq, 0);
	} 

	if (st->flags & LOWDOWN_NROFF_NUMBERED)
		bn = bqueue_block(obq, ".NH");
	else
		bn = bqueue_block(obq, ".SH");
	if (bn == NULL)
		return 0;

	if ((st->flags & LOWDOWN_NROFF_NUMBERED) ||
	    (st->flags & LOWDOWN_NROFF_GROFF)) 
		if (asprintf(&bn->nargs, "%zd", level) == -1) {
			bn->nargs = NULL;
			return 0;
		}

	/* Used in -mspdf output for creating a TOC. */

	if (st->flags & LOWDOWN_NROFF_GROFF) {
		if ((bn = bqueue_block(obq, ".XN")) == NULL)
			return 0;
		if (!bqueue_to_nargs(bn, bq, 0))
			return 0;
	} else
		TAILQ_CONCAT(obq, bq, entries);

	st->post_para = 1;
	return 1;
}

/*
 * Return <0 on failure, 0 to remove next, >0 otherwise.
 */
static ssize_t
rndr_link(struct nroff *st, struct bnodeq *obq, struct bnodeq *bq,
	const struct rndr_link *param, 
	const struct lowdown_node *next)
{

	return putlink(obq, st, &param->link, bq, HALINK_NORMAL, next);
}

static int
rndr_listitem(struct bnodeq *obq, const struct lowdown_node *n,
	struct bnodeq *bq, const struct rndr_listitem *param)
{
	struct bnode	*bn;
	const char	*box;

	if (param->flags & HLIST_FL_ORDERED) {
		if ((bn = bqueue_block(obq, ".IP")) == NULL)
			return 0;
		if (asprintf(&bn->nargs, 
		    "\"%zu.  \"", param->num) == -1)
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
		if (asprintf(&bn->nargs, "\"\\%s\" 2", box) == -1)
			return 0;
	}

	/* Strip out all leading redundant paragraphs. */

	bqueue_strip_paras(bq);
	TAILQ_CONCAT(obq, bq, entries);

	/* 
	 * Suppress trailing space if we're not in a block and there's a
	 * list item that comes after us (i.e., anything after us).
	 */

	if (n->rndr_listitem.flags & HLIST_FL_BLOCK)
		return 1;
	if (n->rndr_listitem.flags & HLIST_FL_DEF)
		n = n->parent;
	if (TAILQ_NEXT(n, entries) != NULL) {
		if (bqueue_block(obq, ".if n \\\n.sp -1") == NULL)
			return 0;
		if (bqueue_block(obq, ".if t \\\n.sp -0.25v\n") == NULL)
			return 0;
	}

	return 1;
}

static int
rndr_paragraph(struct nroff *st, const struct lowdown_node *n,
	struct bnodeq *obq, struct bnodeq *nbq)
{
	const struct lowdown_node	*prev;
	struct bnode			*bn;

	/* 
	 * We don't just blindly print a paragraph macro: it depends
	 * upon what came before.  If we're following a HEADER, don't do
	 * anything at all.  If we're in a list item, make sure that we
	 * don't reset our text indent by using an `IP`.
	 */

	prev = TAILQ_PREV(n, lowdown_nodeq, entries);
	if (!st->man || prev == NULL || prev->type != LOWDOWN_HEADER) {
		for ( ; n != NULL; n = n->parent)
			if (n->type == LOWDOWN_LISTITEM)
				break;
		if (n != NULL)
			bn = bqueue_block(obq, ".IP");
		else if (st->post_para)
			bn = bqueue_block(obq, ".LP");
		else
			bn = bqueue_block(obq, ".PP");
		if (bn == NULL)
			return 0;
	}

	TAILQ_CONCAT(obq, nbq, entries);
	st->post_para = 0;
	return 1;
}

static int
rndr_raw_block(const struct nroff *st,
	struct bnodeq *obq, const struct rndr_blockhtml *param)
{
	struct bnode	*bn;

	if (st->flags & LOWDOWN_NROFF_SKIP_HTML)
		return 1;
	if ((bn = calloc(1, sizeof(struct bnode))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(obq, bn, entries);
	bn->scope = BSCOPE_LITERAL;
	bn->buf = strndup(param->text.data, param->text.size);
	return bn->buf != NULL;
}

static int
rndr_hrule(const struct nroff *st, struct bnodeq *obq)
{
	/*
	 * I'm not sure how else to do horizontal lines.
	 * The LP is to reset the margins.
	 */

	if (bqueue_block(obq, ".LP") == NULL)
		return 0;
	if (!st->man && 
	    bqueue_block(obq, "\\l\'\\n(.lu-\\n(\\n[.in]u\'") == NULL)
		return 0;
	return 1;
}

static int
rndr_image(struct nroff *st, struct bnodeq *obq, 
	const struct rndr_image *param)
{
	const char	*cp;
	size_t		 sz;
	struct bnode	*bn;

	/* In -Tman, we have no images: treat as a link. */

	if (st->man) {
		st->fonts[NFONT_BOLD]++;
		if (!bqueue_font(st, obq, 0))
			return 0;
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			return 0;
		bn->buf = strndup(param->alt.data, param->alt.size);
		if (bn->buf == NULL)
			return 0;
		st->fonts[NFONT_BOLD]--;
		if (!bqueue_font(st, obq, 1))
			return 0;
		if (st->flags & LOWDOWN_NROFF_NOLINK)
			return bqueue_span(obq, " (Image)") != NULL;
		if (bqueue_span(obq, " (Image: ") == NULL)
			return 0;
		st->fonts[NFONT_ITALIC]++;
		if (!bqueue_font(st, obq, 0))
			return 0;
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			return 0;
		if (st->flags & LOWDOWN_NROFF_SHORTLINK) {
			bn->nbuf = hbuf2shortlink(&param->link);
			if (bn->nbuf == NULL)
				return 0;
		} else {
			bn->buf = strndup(param->link.data, param->link.size);
			if (bn->buf == NULL)
				return 0;
		}
		st->fonts[NFONT_ITALIC]--;
		if (!bqueue_font(st, obq, 1))
			return 0;
		return bqueue_span(obq, ")") != NULL;
	}

	/* Are we suffixed with ps or eps? */

	cp = memrchr(param->link.data, '.', param->link.size);
	if (cp == NULL)
		return 1;
	cp++;
	sz = param->link.size - (cp - param->link.data);
	if (sz == 0)
		return 1;
	if (!(sz == 2 && memcmp(cp, "ps", 2) == 0) &&
	    !(sz == 3 && memcmp(cp, "eps", 3) == 0))
		return 1;

	/* If so, use a PSPIC. */

	if ((bn = bqueue_block(obq, ".PSPIC")) == NULL)
		return 0;
	bn->args = strndup(param->link.data, param->link.size);
	return bn->args != NULL;
}

static int
rndr_raw_html(const struct nroff *st,
	struct bnodeq *obq, const struct rndr_raw_html *param)
{
	struct bnode	*bn;

	if (st->flags & LOWDOWN_NROFF_SKIP_HTML)
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

	macro = st->man || !(st->flags & LOWDOWN_NROFF_GROFF) ?
		".TS" : ".TS H";
	if (bqueue_block(obq, macro) == NULL)
		return 0;
	if (bqueue_block(obq, "tab(|) expand allbox;") == NULL)
		return 0;
	TAILQ_CONCAT(obq, bq, entries);
	st->post_para = 1;
	return bqueue_block(obq, ".TE") != NULL;
}

static int
rndr_table_header(const struct nroff *st, struct bnodeq *obq,
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

	if (!st->man && (st->flags & LOWDOWN_NROFF_GROFF) &&
	    bqueue_block(obq, ".TH") == NULL)
		goto out;

	rc = 1;
out:
	hbuf_free(ob);
	return rc;
}

static int
rndr_table_row(struct bnodeq *obq, struct bnodeq *bq)
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
rndr_superscript(struct bnodeq *obq, struct bnodeq *bq)
{

	if (bqueue_span(obq, "\\u\\s-3") == NULL)
		return 0;
	TAILQ_CONCAT(obq, bq, entries);
	return bqueue_span(obq, "\\s+3\\d") != NULL;
}

static int
rndr_footnotes(const struct nroff *st,
	struct bnodeq *obq, struct bnodeq *bq)
{

	/* Put a horizontal line in the case of man(7). */

	if (st->man) {
		if (bqueue_block(obq, ".LP") == NULL)
			return 0;
		if (bqueue_block(obq, ".sp 3") == NULL)
			return 0;
		if (bqueue_block(obq, "\\l\'2i'") == NULL)
			return 0;
	}

	TAILQ_CONCAT(obq, bq, entries);
	return 1;
}

static int
rndr_footnote_def(const struct nroff *st, struct bnodeq *obq,
	struct bnodeq *bq, const struct lowdown_node *n,
	const struct rndr_footnote_def *param)
{
	struct bnode	*bn;

	/* 
	 * Use groff_ms(7)-style footnotes.
	 * We know that the definitions are delivered in the same order
	 * as the footnotes are made, so we can use the automatic
	 * ordering facilities.
	 */

	if (!st->man) {
		if (bqueue_block(obq, ".FS") == NULL)
			return 0;
		if ((n->chng == LOWDOWN_CHNG_INSERT ||
		     n->chng == LOWDOWN_CHNG_DELETE) &&
		    !bqueue_colour(obq, n->chng, 0))
			return 0;
		bqueue_strip_paras(bq);
		TAILQ_CONCAT(obq, bq, entries);
		if ((n->chng == LOWDOWN_CHNG_INSERT ||
		     n->chng == LOWDOWN_CHNG_DELETE) &&
		    !bqueue_colour(obq, n->chng, 1))
			return 0;
		return bqueue_block(obq, ".FE") != NULL;
	}

	/*
	 * For man(7), just print as normal, with a leading footnote
	 * number in italics and superscripted.
	 */

	if (bqueue_block(obq, ".LP") == NULL)
		return 0;
	if ((n->chng == LOWDOWN_CHNG_INSERT ||
	     n->chng == LOWDOWN_CHNG_DELETE) &&
	    !bqueue_colour(obq, n->chng, 0))
		return 0;

	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;
	if (asprintf(&bn->nbuf, 
	    "\\0\\fI\\u\\s-3%zu\\s+3\\d\\fP\\0", param->num) == -1) {
		bn->nbuf = NULL;
		return 0;
	}

	bqueue_strip_paras(bq);
	TAILQ_CONCAT(obq, bq, entries);
	if ((n->chng == LOWDOWN_CHNG_INSERT ||
	     n->chng == LOWDOWN_CHNG_DELETE) &&
	    !bqueue_colour(obq, n->chng, 1))
		return 0;
	return 1;
}

static int
rndr_footnote_ref(const struct nroff *st,
	struct bnodeq *obq, const struct rndr_footnote_ref *param)
{
	struct bnode	*bn;

	/* 
	 * Use groff_ms(7)-style automatic footnoting, else just put a
	 * reference number in small superscripts.
	 */

	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;

	if (st->man) {
		if (asprintf(&bn->nbuf, 
		    "\\u\\s-3%zu\\s+3\\d", param->num) == -1)
			bn->nbuf = NULL;
	} else
		bn->nbuf = strdup("\\**");

	return bn->nbuf != NULL;
}

static int
rndr_entity(const struct nroff *st,
	struct bnodeq *obq, const struct rndr_entity *param)
{
	int32_t		 ent;
	struct bnode	*bn;
	char		 buf[32];

	if ((ent = entity_find_iso(&param->text)) > 0) {
		if (st->flags & LOWDOWN_NROFF_GROFF)
			snprintf(buf, sizeof(buf), "\\[u%.4llX]", 
				(unsigned long long)ent);
		else
			snprintf(buf, sizeof(buf), "\\U\'%.4llX\'", 
				(unsigned long long)ent);
		return bqueue_span(obq, buf) != NULL;
	} 
	if ((bn = bqueue_span(obq, NULL)) == NULL)
		return 0;
	bn->buf = strndup(param->text.data, param->text.size);
	return bn->buf != NULL;
}

/*
 * Split "b" at sequential white-space, outputting the results in the
 * line-based "env" macro.
 * The content in "b" has already been escaped, so there's no need to do
 * anything but manage white-space.
 */
static int
rndr_meta_multi(struct bnodeq *obq, const char *b, const char *env)
{
	const char	*start;
	size_t		 sz, i, bsz;
	struct bnode	*bn;
	char		 macro[32];

	if (b == NULL)
		return 1;

	assert(strlen(env) < sizeof(macro) - 1);
	snprintf(macro, sizeof(macro), ".%s", env);
	bsz = strlen(b);

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

		if (bqueue_block(obq, macro) == NULL)
			return 0;
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			return 0;
		if ((bn->buf = strndup(start, sz)) == NULL)
			return 0;
	}

	return 1;
}

static int
rndr_meta(struct nroff *st, const struct bnodeq *bq, 
	struct lowdown_metaq *mq, const struct rndr_meta *params)
{
	struct lowdown_meta	*m;
	struct lowdown_buf	*ob;
	ssize_t			 val;
	const char		*ep;

	if ((m = calloc(1, sizeof(struct lowdown_meta))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(mq, m, entries);

	m->key = strndup(params->key.data, params->key.size);
	if (m->key == NULL)
		return 0;

	if ((ob = hbuf_new(32)) == NULL)
		return 0;
	if (!bqueue_flush(ob, bq, 0)) {
		hbuf_free(ob);
		return 0;
	}
	m->value = strndup(ob->data, ob->size);
	hbuf_free(ob);
	if (m->value == NULL)
		return 0;

	if (strcmp(m->key, "shiftheadinglevelby") == 0) {
		val = (ssize_t)strtonum
			(m->value, -100, 100, &ep);
		if (ep == NULL)
			st->headers_offs = val + 1;
	} else if (strcmp(m->key, "baseheaderlevel") == 0) {
		val = (ssize_t)strtonum
			(m->value, 1, 100, &ep);
		if (ep == NULL)
			st->headers_offs = val;
	}

	return 1;
}

static int
rndr_doc_header(const struct nroff *st,
	struct bnodeq *obq, const struct lowdown_metaq *mq)
{
	struct lowdown_buf		*ob = NULL;
	struct bnode			*bn;
	const struct lowdown_meta	*m;
	int				 rc = 0;
	const char			*author = NULL, *title = NULL,
					*affil = NULL, *date = NULL,
					*copy = NULL, *sec = NULL,
					*rcsauthor = NULL, *rcsdate = NULL,
					*source = NULL, *volume = NULL;

	if (!(st->flags & LOWDOWN_STANDALONE))
		return 1;

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

	/* Overrides. */

	if (title == NULL)
		title = "Untitled article";
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

	if (!st->man) {
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
		if (bqueue_block(obq, ".TL") == NULL)
			goto out;
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			goto out;
		if ((bn->buf = strdup(title)) == NULL)
			goto out;
		if (!rndr_meta_multi(obq, author, "AU"))
			goto out;
		if (!rndr_meta_multi(obq, affil, "AI"))
			goto out;
	} else {
		if ((ob = hbuf_new(32)) == NULL)
			goto out;

		/*
		 * The syntax of this macro, according to man(7), is 
		 * TH name section date [source [volume]].
		 */

		if ((bn = bqueue_block(obq, ".TH")) == NULL)
			goto out;
		if (!hbuf_putc(ob, '"') ||
		    !hesc_nroff(ob, title, strlen(title), 1, 0, 1) ||
		    !HBUF_PUTSL(ob, "\" \"") ||
		    !hesc_nroff(ob, sec, strlen(sec), 1, 0, 1) ||
		    !hbuf_putc(ob, '"'))
			goto out;

		/*
		 * We may not have a date (or it may be empty), in which
		 * case man(7) says the current date is used.
		 */

		if (!HBUF_PUTSL(ob, " \""))
			goto out;
		if (date != NULL &&
		    !hesc_nroff(ob, date, strlen(date), 1, 0, 1))
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
			if (source != NULL && !hesc_nroff
			    (ob, source, strlen(source), 1, 0, 1))
				goto out;
			if (!HBUF_PUTSL(ob, "\""))
				goto out;
			if (!HBUF_PUTSL(ob, " \""))
				goto out;
			if (volume != NULL && !hesc_nroff
			    (ob, volume, strlen(volume), 1, 0, 1))
				goto out;
			if (!HBUF_PUTSL(ob, "\""))
				goto out;
		}
		if ((bn->nargs = strndup(ob->data, ob->size)) == NULL)
			goto out;
	}

	rc = 1;
out:
	hbuf_free(ob);
	return rc;
}

/*
 * Actually render the node "n" and all of its children into the output
 * buffer "ob", chopping "chop" from the current node if specified.
 * Return what (if anything) we should chop from the next node or <0 on
 * failure.
 */
static ssize_t
rndr(struct lowdown_metaq *mq, struct nroff *st,
	const struct lowdown_node *n, struct bnodeq *obq, size_t chop)
{
	const struct lowdown_node	*child;
	int				 rc = 1;
	ssize_t				 keepnext, ret = -1;
	enum nfont			 fonts[NFONT__MAX];
	struct bnodeq			 tmpbq;
	struct bnode			*bn;

	TAILQ_INIT(&tmpbq);

	if ((n->chng == LOWDOWN_CHNG_INSERT ||
	     n->chng == LOWDOWN_CHNG_DELETE) &&
	    n->type != LOWDOWN_FOOTNOTE_DEF &&
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
		st->fonts[NFONT_FIXED]++;
		if (!bqueue_font(st, obq, 0))
			goto out;
		break;
	case LOWDOWN_EMPHASIS:
		st->fonts[NFONT_ITALIC]++;
		if (!bqueue_font(st, obq, 0))
			goto out;
		break;
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
		st->fonts[NFONT_BOLD]++;
		if (!bqueue_font(st, obq, 0))
			goto out;
		break;
	case LOWDOWN_TRIPLE_EMPHASIS:
		st->fonts[NFONT_ITALIC]++;
		st->fonts[NFONT_BOLD]++;
		if (!bqueue_font(st, obq, 0))
			goto out;
		break;
	default:
		break;
	}

	keepnext = 0;
	TAILQ_FOREACH(child, &n->children, entries) {
		keepnext = rndr(mq, st, child, &tmpbq, keepnext);
		if (keepnext < 0)
			goto out;
	}

	ret = 0;
	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
		rc = rndr_blockcode(st, obq, &n->rndr_blockcode);
		break;
	case LOWDOWN_BLOCKQUOTE:
		rc = rndr_blockquote(st, obq, &tmpbq);
		break;
	case LOWDOWN_DEFINITION:
		rc = rndr_list(st, obq, n, &tmpbq);
		break;
	case LOWDOWN_DEFINITION_DATA:
		rc = rndr_definition_data(obq, &tmpbq);
		break;
	case LOWDOWN_DEFINITION_TITLE:
		rc = rndr_definition_title(obq, &tmpbq);
		break;
	case LOWDOWN_DOC_HEADER:
		rc = rndr_doc_header(st, obq, mq);
		break;
	case LOWDOWN_META:
		if (n->chng != LOWDOWN_CHNG_DELETE)
			rc = rndr_meta(st, &tmpbq, mq, &n->rndr_meta);
		break;
	case LOWDOWN_HEADER:
		rc = rndr_header(st, obq, &tmpbq, &n->rndr_header);
		break;
	case LOWDOWN_HRULE:
		rc = rndr_hrule(st, obq);
		break;
	case LOWDOWN_LIST:
		rc = rndr_list(st, obq, n, &tmpbq);
		break;
	case LOWDOWN_LISTITEM:
		rc = rndr_listitem(obq, n, &tmpbq, &n->rndr_listitem);
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
		rc = rndr_table_row(obq, &tmpbq);
		break;
	case LOWDOWN_TABLE_CELL:
		rc = rndr_table_cell(obq, &tmpbq, &n->rndr_table_cell);
		break;
	case LOWDOWN_FOOTNOTES_BLOCK:
		rc = rndr_footnotes(st, obq, &tmpbq);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rc = rndr_footnote_def(st, obq, 
			&tmpbq, n, &n->rndr_footnote_def);
		break;
	case LOWDOWN_BLOCKHTML:
		rc = rndr_raw_block(st, obq, &n->rndr_blockhtml);
		break;
	case LOWDOWN_LINK_AUTO:
		ret = rndr_autolink(st, obq, 
			&n->rndr_autolink, TAILQ_NEXT(n, entries));
		break;
	case LOWDOWN_CODESPAN:
		rc = rndr_codespan(obq, &n->rndr_codespan);
		break;
	case LOWDOWN_IMAGE:
		rc = rndr_image(st, obq, &n->rndr_image);
		break;
	case LOWDOWN_LINEBREAK:
		rc = rndr_linebreak(obq);
		break;
	case LOWDOWN_LINK:
		ret = rndr_link(st, obq, &tmpbq, 
			&n->rndr_link, TAILQ_NEXT(n, entries));
		break;
	case LOWDOWN_SUPERSCRIPT:
		rc = rndr_superscript(obq, &tmpbq);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		rc = rndr_footnote_ref(st, obq, &n->rndr_footnote_ref);
		break;
	case LOWDOWN_RAW_HTML:
		rc = rndr_raw_html(st, obq, &n->rndr_raw_html);
		break;
	case LOWDOWN_NORMAL_TEXT:
		if (chop == n->rndr_normal_text.text.size)
			break;
		if ((bn = bqueue_span(obq, NULL)) == NULL)
			goto out;
		bn->buf = strndup
			(n->rndr_normal_text.text.data,
			 n->rndr_normal_text.text.size);
		if (bn->buf == NULL)
			goto out;
		bn->bufchop = chop;
		break;
	case LOWDOWN_ENTITY:
		rc = rndr_entity(st, obq, &n->rndr_entity);
		break;
	default:
		TAILQ_CONCAT(obq, &tmpbq, entries);
		break;
	}

	if (!rc)
		ret = -1;
	if (ret < 0)
		goto out;

	/* Restore the font stack. */

	switch (n->type) {
	case LOWDOWN_CODESPAN:
	case LOWDOWN_EMPHASIS:
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
	case LOWDOWN_TRIPLE_EMPHASIS:
		memcpy(st->fonts, fonts, sizeof(fonts));
		if (!bqueue_font(st, obq, 1)) {
			ret = -1;
			goto out;
		}
		break;
	default:
		break;
	}

	if ((n->chng == LOWDOWN_CHNG_INSERT ||
	     n->chng == LOWDOWN_CHNG_DELETE) &&
	    n->type != LOWDOWN_FOOTNOTE_DEF &&
	    !bqueue_colour(obq, n->chng, 1)) {
		ret = -1;
		goto out;
	}

out:
	bqueue_free(&tmpbq);
	return ret;
}

int
lowdown_nroff_rndr(struct lowdown_buf *ob,
	void *arg, const struct lowdown_node *n)
{
	struct nroff		*st = arg;
	struct lowdown_metaq	 metaq;
	int			 rc = 0;
	struct bnodeq		 bq;

	TAILQ_INIT(&metaq);
	TAILQ_INIT(&bq);

	memset(st->fonts, 0, sizeof(st->fonts));
	st->headers_offs = 1;
	st->post_para = 0;

	if (rndr(&metaq, st, n, &bq, 0) >= 0) {
		if (!bqueue_flush(ob, &bq, 1))
			goto out;
		if (ob->size && ob->data[ob->size - 1] != '\n' &&
		    !hbuf_putc(ob, '\n'))
			goto out;
		rc = 1;
	}
out:
	lowdown_metaq_free(&metaq);
	bqueue_free(&bq);
	return rc;
}

void *
lowdown_nroff_new(const struct lowdown_opts *opts)
{
	struct nroff 	*p;

	if ((p = calloc(1, sizeof(struct nroff))) == NULL)
		return NULL;

	p->flags = opts != NULL ? opts->oflags : 0;
	p->man = opts != NULL && opts->type == LOWDOWN_MAN;
	return p;
}

void
lowdown_nroff_free(void *arg)
{

	/* No need to check NULL: pass directly to free(). */

	free(arg);
}
