/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016--2017, 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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
 * Emit a newline if and only if the given buffer doesn't end in one.
 */
#define	BUFFER_NEWLINE(_buf, _sz, _ob) \
	((_sz) == 0 || \
	 (_buf)[(_sz) - 1] == '\n' || \
	 hbuf_putc((_ob), '\n'))

/*
 * See BUFFER_NEWLINE().
 */
#define	HBUF_NEWLINE(_buf, _ob) \
	BUFFER_NEWLINE((_buf)->data, (_buf)->size, (_ob))

enum	nfont {
	NFONT_ITALIC = 0, /* italic */
	NFONT_BOLD, /* bold */
	NFONT_FIXED, /* fixed-width */
	NFONT__MAX
};

/*
 * This relates to the roff output, not the node type.
 * If NSCOPE_BLOCK, the output is newline-terminated.
 * Otherwise, it is not.
 */
enum	nscope {
	NSCOPE_BLOCK,
	NSCOPE_SPAN
};

struct 	nroff {
	int 		 man; /* whether man(7) */
	int		 post_para; /* for choosing PP/LP */
	unsigned int 	 flags; /* output flags */
	size_t		 base_header_level; /* header offset */
	enum nfont	 fonts[NFONT__MAX]; /* see nstate_fonts() */
};

static const enum nscope nscopes[LOWDOWN__MAX] = {
	NSCOPE_BLOCK, /* LOWDOWN_ROOT */
	NSCOPE_BLOCK, /* LOWDOWN_BLOCKCODE */
	NSCOPE_BLOCK, /* LOWDOWN_BLOCKQUOTE */
	NSCOPE_BLOCK, /* LOWDOWN_DEFINITION */
	NSCOPE_BLOCK, /* LOWDOWN_DEFINITION_TITLE */
	NSCOPE_BLOCK, /* LOWDOWN_DEFINITION_DATA */
	NSCOPE_BLOCK, /* LOWDOWN_HEADER */
	NSCOPE_BLOCK, /* LOWDOWN_HRULE */
	NSCOPE_BLOCK, /* LOWDOWN_LIST */
	NSCOPE_BLOCK, /* LOWDOWN_LISTITEM */
	NSCOPE_BLOCK, /* LOWDOWN_PARAGRAPH */
	NSCOPE_BLOCK, /* LOWDOWN_TABLE_BLOCK */
	NSCOPE_BLOCK, /* LOWDOWN_TABLE_HEADER */
	NSCOPE_BLOCK, /* LOWDOWN_TABLE_BODY */
	NSCOPE_BLOCK, /* LOWDOWN_TABLE_ROW */
	NSCOPE_BLOCK, /* LOWDOWN_TABLE_CELL */
	NSCOPE_BLOCK, /* LOWDOWN_FOOTNOTES_BLOCK */
	NSCOPE_BLOCK, /* LOWDOWN_FOOTNOTE_DEF */
	NSCOPE_BLOCK, /* LOWDOWN_BLOCKHTML */
	NSCOPE_BLOCK, /* LOWDOWN_LINK_AUTO */
	NSCOPE_SPAN, /* LOWDOWN_CODESPAN */
	NSCOPE_SPAN, /* LOWDOWN_DOUBLE_EMPHASIS */
	NSCOPE_SPAN, /* LOWDOWN_EMPHASIS */
	NSCOPE_SPAN, /* LOWDOWN_HIGHLIGHT */
	NSCOPE_BLOCK, /* LOWDOWN_IMAGE */
	NSCOPE_BLOCK, /* LOWDOWN_LINEBREAK */
	NSCOPE_BLOCK, /* LOWDOWN_LINK */
	NSCOPE_SPAN, /* LOWDOWN_TRIPLE_EMPHASIS */
	NSCOPE_SPAN, /* LOWDOWN_STRIKETHROUGH */
	NSCOPE_SPAN, /* LOWDOWN_SUPERSCRIPT */
	NSCOPE_SPAN, /* LOWDOWN_FOOTNOTE_REF */
	NSCOPE_BLOCK, /* LOWDOWN_MATH_BLOCK */
	NSCOPE_SPAN, /* LOWDOWN_RAW_HTML */
	NSCOPE_SPAN, /* LOWDOWN_ENTITY */
	NSCOPE_SPAN, /* LOWDOWN_NORMAL_TEXT */
	NSCOPE_BLOCK, /* LOWDOWN_DOC_HEADER */
	NSCOPE_BLOCK, /* LOWDOWN_META */
	NSCOPE_BLOCK /* LOWDOWN_DOC_FOOTER */
};

/*
 * If "span" is non-zero, don't test for leading periods.
 * Otherwise, a leading period will be escaped.
 * If "oneline" is non-zero, newlines are replaced with spaces.
 * Return zero on failure, non-zero on success.
 */
static int
hesc_nroff(struct lowdown_buf *ob, const char *data, 
	size_t size, int span, int oneline, int keep)
{
	size_t	 i;

	if (size == 0)
		return 1;
	if (!span && data[0] == '.' && !HBUF_PUTSL(ob, "\\&"))
		return 0;

	/*
	 * According to mandoc_char(7), we need to escape the backtick,
	 * single apostrophe, and tilde or else they'll be considered as
	 * special Unicode output.
	 * Slashes need to be escaped too, and newlines if appropriate
	 */

	for (i = 0; i < size; i++)
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
		case '\n':
			if (!hbuf_putc(ob, oneline ? ' ' : '\n'))
				return 0;
			if (keep)
				break;
			/*
			 * Prevent leading spaces on the line.
			 */
			while (i + 1 < size && data[i + 1] == ' ')
				i++;
			break;
		case '\\':
			if (!HBUF_PUTSL(ob, "\\e"))
				return 0;
			break;
		case '.':
			if (!oneline && i && data[i - 1] == '\n' &&
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

/*
 * Output "source" of size "length" on as many lines as required,
 * starting on a line with existing content.
 * Escapes text so as not to be roff.
 * Return zero on failure, non-zero on success.
 */
static int
rndr_span(struct lowdown_buf *ob, const char *source, size_t length)
{

	return hesc_nroff(ob, source, length, 1, 0, 0);
}

/*
 * Output "source" of size "length" on as many lines as required,
 * starting on its own line.
 * Escapes text so as not to be roff.
 * Return zero on failure, non-zero on success.
 */
static int
rndr_block(struct lowdown_buf *ob, const char *source, size_t length)
{

	return hesc_nroff(ob, source, length, 0, 0, 0);
}

/*
 * Like rndr_block(), but not collapsing spaces at the start of lines.
 * Return zero on failure, non-zero on success.
 */
static int
rndr_block_keep(struct lowdown_buf *ob, const struct lowdown_buf *in)
{

	return hesc_nroff(ob, in->data, in->size, 0, 0, 1);
}

/*
 * Output "source" of size "length" on a single line.
 * Does not escape the given text, which should already have been
 * escaped, unless "ownline" is given, in which case make sure we don't
 * start with roff.
 * Return zero on failure, non-zero on success.
 */
static int
rndr_one_line_noescape(struct lowdown_buf *ob,
	const char *source, size_t length, int ownline)
{
	size_t	 i;
	char	 c;

	if (ownline && length && 
	    source[0] == '.' && !HBUF_PUTSL(ob, "\\&"))
		return 0;

	for (i = 0; i < length; i++) {
		c = isspace((unsigned char)source[i]) ?
			' ' : source[i];
		if (!hbuf_putc(ob, c))
			return 0;
	}

	return 1;
}

/*
 * See rndr_one_line_noescape().
 * Return zero on failure, non-zero on success.
 */
static int
rndr_one_lineb_noescape(struct lowdown_buf *ob,
	const struct lowdown_buf *b, int ownline)
{

	return rndr_one_line_noescape(ob, b->data, b->size, ownline);
}

/*
 * Return the font string for the current set of fonts.
 * FIXME: I don't think this works for combinations of fixed-width,
 * bold, and italic.
 * Always returns a valid pointer.
 */
static const char *
nstate_fonts(const struct nroff *st)
{
	static char 	 fonts[10];
	char		*cp = fonts;

	(*cp++) = '\\';
	(*cp++) = 'f';
	(*cp++) = '[';

	if (st->fonts[NFONT_FIXED])
		(*cp++) = 'C';
	if (st->fonts[NFONT_BOLD])
		(*cp++) = 'B';
	if (st->fonts[NFONT_ITALIC])
		(*cp++) = 'I';
	if (st->fonts[NFONT_FIXED] &&
	    (st->fonts[NFONT_BOLD] == 0 &&
	     st->fonts[NFONT_ITALIC] == 0))
		(*cp++) = 'R';

	/* Reset. */

	if (st->fonts[NFONT_BOLD] == 0 &&
	    st->fonts[NFONT_FIXED] == 0 &&
	    st->fonts[NFONT_ITALIC] == 0)
		(*cp++) = 'R';
	(*cp++) = ']';
	(*cp++) = '\0';
	return fonts;
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
static int
putlink(struct lowdown_buf *ob, struct nroff *st,
	const struct lowdown_buf *link,
	const struct lowdown_buf *text, 
	struct lowdown_node *next, 
	struct lowdown_node *prev, 
	enum halink_type type)
{
	const struct lowdown_buf	*buf;
	size_t		 		 i, pos;
	int		 		 ret = 1, usepdf;

	usepdf = !st->man && (st->flags & LOWDOWN_NROFF_GROFF);

	if (usepdf && !HBUF_PUTSL(ob, ".pdfhref W "))
		return -1;

	/*
	 * If we're preceded by normal text that doesn't end with space,
	 * then put that text into the "-P" (prefix) argument.
	 * If we're not in groff mode, emit the data, but without -P.
	 */

	if (prev != NULL &&
	    prev->type == LOWDOWN_NORMAL_TEXT) {
		buf = &prev->rndr_normal_text.text;
		i = buf->size;
		while (i && !isspace((unsigned char)buf->data[i - 1]))
			i--;
		if (i != buf->size && 
		    usepdf && !HBUF_PUTSL(ob, "-P \""))
			return -1;
		for (pos = i; pos < buf->size; pos++) {
			/* Be sure to escape... */
			if (buf->data[pos] == '"') {
				if (!HBUF_PUTSL(ob, "\\(dq"))
					return -1;
				continue;
			} else if (buf->data[pos] == '\\') {
				if (!HBUF_PUTSL(ob, "\\e"))
					return -1;
				continue;
			}
			if (!hbuf_putc(ob, buf->data[pos]))
				return -1;
		}
		if (i != buf->size && 
		    usepdf && !HBUF_PUTSL(ob, "\" "))
			return -1;
	}

	if (!usepdf) {
		st->fonts[NFONT_ITALIC]++;
		if (!hbuf_puts(ob, nstate_fonts(st)))
			return -1;
		if (text == NULL) {
			if (hbuf_strprefix(link, "mailto:")) {
				if (!hbuf_put(ob, 
				    link->data + 7, link->size - 7))
					return -1;
			} else if (!hbuf_putb(ob, link))
				return -1;
		} else if (!hbuf_putb(ob, text))
			return -1;
		st->fonts[NFONT_ITALIC]--;
		if (!hbuf_puts(ob, nstate_fonts(st)))
			return -1;
	}

	/*
	 * If we're followed by normal text that doesn't begin with a
	 * space, use the "-A" (affix) option to prevent a space before
	 * what follows.
	 * But first, initialise the offset.
	 */

	if (next != NULL && 
	    next->type == LOWDOWN_NORMAL_TEXT)
		next->rndr_normal_text.offs = 0;

	if (next != NULL && 
	    next->type == LOWDOWN_NORMAL_TEXT &&
	    next->rndr_normal_text.text.size > 0 &&
	    next->rndr_normal_text.text.data[0] != ' ') {
		buf = &next->rndr_normal_text.text;
		if (usepdf && !HBUF_PUTSL(ob, "-A \""))
			return -1;
		for (pos = 0; pos < buf->size; pos++) {
			if (isspace((unsigned char)buf->data[pos]))
				break;
			/* Be sure to escape... */
			if (buf->data[pos] == '"') {
				if (!HBUF_PUTSL(ob, "\\(dq"))
					return -1;
				continue;
			} else if (buf->data[pos] == '\\') {
				if (!HBUF_PUTSL(ob, "\\e"))
					return -1;
				continue;
			}
			if (!hbuf_putc(ob, buf->data[pos]))
				return -1;
		}
		ret = pos < buf->size;
		next->rndr_normal_text.offs = pos;
		if (usepdf && !HBUF_PUTSL(ob, "\" "))
			return -1;
	}

	/* Encode the URL. */

	if (usepdf) {
		if (!HBUF_PUTSL(ob, "-D "))
			return -1;
		if (type == HALINK_EMAIL && 
		    !HBUF_PUTSL(ob, "mailto:"))
			return -1;
		for (i = 0; i < link->size; i++) {
			if (!isprint((unsigned char)link->data[i]) ||
			    strchr("<>\\^`{|}\"", 
			    link->data[i]) != NULL) {
				if (!hbuf_printf
				    (ob, "%%%.2X", link->data[i]))
					return -1;
			} else if (!hbuf_putc(ob, link->data[i]))
				return -1;
		}
		if (!HBUF_PUTSL(ob, " "))
			return -1;
		if (text == NULL) {
			if (hbuf_strprefix(link, "mailto:")) {
				if (!hbuf_put(ob, 
				    link->data + 7, link->size - 7))
					return -1;
			} else if (!hbuf_putb(ob, link))
				return -1;
		} else {
			if (!hesc_nroff(ob, 
			    text->data, text->size, 0, 1, 0))
				return -1;
		}
	}

	return HBUF_PUTSL(ob, "\n") ? ret : -1;
}

/*
 * Return <0 on failure, 0 to remove next, >0 otherwise.
 */
static int
rndr_autolink(struct lowdown_buf *ob,
	const struct rndr_autolink *param,
	struct lowdown_node *prev, 
	struct lowdown_node *next,
	struct nroff *st, int nln)
{

	if (param->link.size == 0)
		return 1;

	if (!nln)
		if (prev == NULL || 
		    (ob->size && ob->data[ob->size - 1] != '\n'))
			if (!HBUF_PUTSL(ob, "\n"))
				return -1;

	return putlink(ob, st, &param->link, 
		NULL, next, prev, param->type);
}

static int
rndr_blockcode(struct lowdown_buf *ob,
	const struct rndr_blockcode *param,
	struct nroff *st)
{

	if (param->text.size == 0)
		return 1;

	/*
	 * XXX: intentionally don't use LD/DE because it introduces
	 * vertical space.  This means that subsequent blocks
	 * (paragraphs, etc.) will have a double-newline.
	 */

	HBUF_PUTSL(ob, ".sp\n");
	if (st->man && (st->flags & LOWDOWN_NROFF_GROFF)) {
		if (!HBUF_PUTSL(ob, ".EX\n"))
			return 0;
	} else {
		if (!HBUF_PUTSL(ob, ".nf\n.ft CR\n"))
			return 0;
	}

	if (!rndr_block_keep(ob, &param->text))
		return 0;
	if (!HBUF_NEWLINE(&param->text, ob))
		return 0;

	if (st->man && (st->flags & LOWDOWN_NROFF_GROFF))
		return HBUF_PUTSL(ob, ".EE\n");
	else
		return HBUF_PUTSL(ob, ".ft\n.fi\n");
}

static int
rndr_definition_title(struct lowdown_buf *ob,
	const struct lowdown_node *np,
	const struct lowdown_buf *content)
{

	if (content->size == 0)
		return 1;
	if (!HBUF_PUTSL(ob, ".IP \""))
		return 0;
	if (!rndr_one_lineb_noescape(ob, content, 0))
		return 0;
	return HBUF_PUTSL(ob, "\"\n");
}

static int
rndr_definition_data(struct lowdown_buf *ob,
	const struct lowdown_node *root,
	const struct lowdown_buf *content)
{
	const char	*cdata;
	size_t		 csize;

	/* 
	 * Strip out leading paragraphs.
	 * XXX: shouldn't these all be handled by the child list item?
	 */

	cdata = content->data;
	csize = content->size;

	while (csize > 3 && 
	       (memcmp(cdata, ".PP\n", 4) == 0 ||
	        memcmp(cdata, ".IP\n", 4) == 0 ||
	        memcmp(cdata, ".LP\n", 4) == 0)) {
		cdata += 4;
		csize -= 4;
	}

	if (!hbuf_put(ob, cdata, csize))
		return 0;
	return BUFFER_NEWLINE(cdata, csize, ob);
}

/*
 * Used by both definition and regular lists.
 * The only handling this does is to increment the left margin if nested
 * within another list item.
 */
static int
rndr_list(struct lowdown_buf *ob,
	const struct lowdown_node *np,
	const struct lowdown_buf *content,
	struct nroff *st)
{

	if (content->size == 0)
		return 1;

	/* 
	 * If we have a nested list, we need to use RS/RE to indent the
	 * nested component.  Otherwise the `IP` used for the titles and
	 * contained paragraphs won't indent properly.
	 */

	for (np = np->parent; np != NULL; np = np->parent)
		if (np->type == LOWDOWN_LISTITEM)
			break;

	if (np != NULL && !HBUF_PUTSL(ob, ".RS\n"))
		return 0;

	if (!hbuf_putb(ob, content))
		return 0;
	if (!HBUF_NEWLINE(content, ob))
		return 0;

	if (np != NULL && !HBUF_PUTSL(ob, ".RE\n"))
		return 0;

	st->post_para = 1;
	return 1;
}

static int
rndr_blockquote(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	struct nroff *st)
{

	if (content->size == 0)
		return 1;
	if (!HBUF_PUTSL(ob, ".RS\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	if (!HBUF_NEWLINE(content, ob))
		return 0;
	if (!HBUF_PUTSL(ob, ".RE\n"))
		return 0;
	st->post_para = 1;
	return 1;
}

static int
rndr_codespan(struct lowdown_buf *ob,
	const struct rndr_codespan *param)
{

	return rndr_span(ob, param->text.data, param->text.size);
}

/*
 * FIXME: not supported.
 */
static int
rndr_strikethrough(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	return hbuf_putb(ob, content);
}

static int
rndr_linebreak(struct lowdown_buf *ob)
{

	/* FIXME: should this always have a newline? */

	return HBUF_PUTSL(ob, "\n.br\n");
}

static int
rndr_header(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_header *param,
	struct nroff *st)
{
	size_t	level = param->level + st->base_header_level;

	if (content->size == 0)
		return 1;

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
		if (level == 1 && !HBUF_PUTSL(ob, ".SH "))
			return 0;
		else if (level != 1 && !HBUF_PUTSL(ob, ".SS "))
			return 0;
		if (!rndr_one_lineb_noescape(ob, content, 0))
			return 0;
		return HBUF_PUTSL(ob, "\n");
	} 

	if (st->flags & LOWDOWN_NROFF_NUMBERED) {
		if (!hbuf_printf(ob, ".NH %zu\n", level))
			return 0;
	} else if (st->flags & LOWDOWN_NROFF_GROFF) {
		if (!hbuf_printf(ob, ".SH %zu\n", level))
			return 0;
	} else {
		if (!HBUF_PUTSL(ob, ".SH\n"))
			return 0;
	}

	/* Used in -mspdf output for creating a TOC. */

	if (st->flags & LOWDOWN_NROFF_GROFF) {
		if (!HBUF_PUTSL(ob, ".XN "))
			return 0;
		if (!rndr_one_lineb_noescape(ob, content, 0))
			return 0;
		if (!HBUF_PUTSL(ob, "\n"))
			return 0;
	} else {
		if (!rndr_one_lineb_noescape(ob, content, 1))
			return 0;
		if (!HBUF_NEWLINE(content, ob))
			return 0;
	}

	st->post_para = 1;
	return 1;
}

/*
 * Return <0 on failure, 0 to remove next, >0 otherwise.
 */
static int
rndr_link(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_link *param,
	struct nroff *st, 
	struct lowdown_node *prev, 
	struct lowdown_node *next, int nln)
{

	if (content->size == 0 && param->link.size == 0)
		return 1;

	if (!nln)
		if (prev == NULL ||
		    (ob->size && ob->data[ob->size - 1] != '\n'))
			if (!HBUF_PUTSL(ob, "\n"))
				return -1;

	return putlink(ob, st, &param->link,
		content, next, prev, HALINK_NORMAL);
}

/*
 * The list item is part of both definition and regular lists.
 * In the former case, it's within the "data" part of the definition
 * list, so the title `IP` has already been emitted.
 */
static int
rndr_listitem(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	const struct lowdown_node *np,
	const struct rndr_listitem *param)
{
	const char	*cdata;
	size_t	 	 csize;

	if (content->size == 0)
		return 1;

	if (param->flags & HLIST_FL_ORDERED) {
		if (!hbuf_printf(ob, ".IP \"%zu.  \"\n", param->num))
			return 0;
	} else if (param->flags & HLIST_FL_UNORDERED) {
		if (!HBUF_PUTSL(ob,  ".IP \"\\(bu\" 2\n"))
			return 0;
	}

	/* Strip out all leading redundant paragraphs. */

	cdata = content->data;
	csize = content->size;

	while (csize > 3 &&
	       (memcmp(cdata, ".LP\n", 4) == 0 ||
	        memcmp(cdata, ".IP\n", 4) == 0 ||
	        memcmp(cdata, ".PP\n", 4) == 0)) {
		cdata += 4;
		csize -= 4;
	}

	/* Make sure we have a newline before links. */

	if (csize > 8 && memcmp(cdata, ".pdfhref ", 9) == 0 &&
	    !HBUF_PUTSL(ob, "\n"))
		return 0;

	if (!hbuf_put(ob, cdata, csize))
		return 0;
	if (!BUFFER_NEWLINE(cdata, csize, ob))
		return 0;

	/* 
	 * Suppress trailing space if we're not in a block and there's a
	 * list item that comes after us (i.e., anything after us).
	 */

	if (np->rndr_listitem.flags & HLIST_FL_BLOCK)
		return 1;
	if (np->rndr_listitem.flags & HLIST_FL_DEF)
		np = np->parent;
	if (TAILQ_NEXT(np, entries) != NULL)
		return HBUF_PUTSL(ob, 
			".if n \\\n.sp -1\n"
			".if t \\\n.sp -0.25v\n");
	return 1;
}

static int
rndr_paragraph(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	struct nroff *st, 
	const struct lowdown_node *np)
{
	size_t	 			 i = 0, org;
	const struct lowdown_node	*prev;
	int				 no_macro = 0;

	if (content->size == 0)
		return 1;

	/* Strip away initial white-space. */

	while (i < content->size && 
	       isspace((unsigned char)content->data[i]))
		i++;

	if (i == content->size)
		return 1;

	prev = TAILQ_PREV(np, lowdown_nodeq, entries);

	/* 
	 * We don't just blindly print a paragraph macro: it depends
	 * upon what came before.  If we're following a HEADER, don't do
	 * anything at all.  If we're in a list item, make sure that we
	 * don't reset our text indent by using an `IP`.
	 */

	if (st->man && prev != NULL && prev->type == LOWDOWN_HEADER)
		no_macro = 1;

	if (!no_macro) {
		for ( ; np != NULL; np = np->parent)
			if (np->type == LOWDOWN_LISTITEM)
				break;
		if (np != NULL) {
			if (!HBUF_PUTSL(ob, ".IP\n"))
				return 0;
		} else if (st->post_para) {
			if (!HBUF_PUTSL(ob, ".LP\n"))
				return 0;
		} else {
			if (!HBUF_PUTSL(ob, ".PP\n"))
				return 0;
		}
	}

	st->post_para = 0;

	if (!(st->flags & LOWDOWN_NROFF_HARD_WRAP)) {
		if (!hbuf_put(ob, content->data + i, 
		    content->size - i))
			return 0;
		return BUFFER_NEWLINE(content->data + i, 
			content->size - i, ob);
	}

	while (i < content->size) {
		org = i;
		while (i < content->size && content->data[i] != '\n')
			i++;
		if (i > org && !hbuf_put
	   	    (ob, content->data + org, i - org))
			return 0;
		if (i >= content->size - 1) {
			if (!HBUF_PUTSL(ob, "\n"))
				return 0;
			break;
		}
		if (!rndr_linebreak(ob))
			return 0;
		i++;
	}

	return 1;
}

static int
rndr_raw_block(struct lowdown_buf *ob,
	const struct rndr_blockhtml *param,
	const struct nroff *st)
{
	size_t	 org = 0, sz = param->text.size;

	if (param->text.size == 0 ||
	    (st->flags & LOWDOWN_NROFF_SKIP_HTML))
		return 1;

	while (sz > 0 && param->text.data[sz - 1] == '\n')
		sz--;
	while (org < sz && param->text.data[org] == '\n')
		org++;

	if (org >= sz)
		return 1;

	if (!HBUF_NEWLINE(ob, ob))
		return 0;
	if (!hbuf_put(ob, param->text.data + org, sz - org))
		return 0;
	return HBUF_PUTSL(ob, "\n");
}

static int
rndr_hrule(struct lowdown_buf *ob, const struct nroff *st)
{

	/*
	 * I'm not sure how else to do horizontal lines.
	 * The LP is to reset the margins.
	 */

	if (!HBUF_PUTSL(ob, ".LP\n"))
		return 0;
	if (!st->man && 
	    !HBUF_PUTSL(ob, "\\l\'\\n(.lu-\\n(\\n[.in]u\'\n"))
		return 0;
	return 1;
}

static int
rndr_image(struct lowdown_buf *ob,
	const struct rndr_image *param,
	const struct nroff *st, int nln, 
	const struct lowdown_node *prev)
{
	const char	*cp;
	size_t		 sz;

	if (st->man)
		return 1;

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

	if (!nln)
		if (prev == NULL ||
		    (ob->size && ob->data[ob->size - 1] != '\n'))
			if (!HBUF_PUTSL(ob, "\n"))
				return 0;

	return hbuf_printf(ob, ".PSPIC %.*s\n", 
		(int)param->link.size, param->link.data);
}

static int
rndr_raw_html(struct lowdown_buf *ob,
	const struct rndr_raw_html *param,
	const struct nroff *st)
{

	if (st->flags & LOWDOWN_NROFF_SKIP_HTML)
		return 1;
	return rndr_block_keep(ob, &param->text);
}

static int
rndr_table(struct lowdown_buf *ob, 
	const struct lowdown_buf *content,
	struct nroff *st)
{

	if (!HBUF_PUTSL(ob, ".TS\n"))
		return 0;
	if (!HBUF_PUTSL(ob, "tab(|) expand allbox;\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	if (!HBUF_NEWLINE(content, ob))
		return 0;
	if (!HBUF_PUTSL(ob, ".TE\n"))
		return 0;

	st->post_para = 1;
	return 1;
}

static int
rndr_table_header(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_table_header *param)
{
	size_t	 i;

	/* 
	 * This specifies the header layout.
	 * We make the header bold, but this is arbitrary.
	 */

	for (i = 0; i < param->columns; i++) {
		if (i > 0 && !HBUF_PUTSL(ob, " "))
			return 0;
		switch (param->flags[i] & HTBL_FL_ALIGNMASK) {
		case HTBL_FL_ALIGN_CENTER:
			if (!HBUF_PUTSL(ob, "cb"))
				return 0;
			break;
		case HTBL_FL_ALIGN_RIGHT:
			if (!HBUF_PUTSL(ob, "rb"))
				return 0;
			break;
		default:
			if (!HBUF_PUTSL(ob, "lb"))
				return 0;
			break;
		}
	}
	if (!HBUF_PUTSL(ob, "\n"))
		return 0;

	/* Now the body layout. */

	for (i = 0; i < param->columns; i++) {
		if (i > 0 && !HBUF_PUTSL(ob, " "))
			return 0;
		switch (param->flags[i] & HTBL_FL_ALIGNMASK) {
		case HTBL_FL_ALIGN_CENTER:
			if (!HBUF_PUTSL(ob, "c"))
				return 0;
			break;
		case HTBL_FL_ALIGN_RIGHT:
			if (!HBUF_PUTSL(ob, "r"))
				return 0;
			break;
		default:
			if (!HBUF_PUTSL(ob, "l"))
				return 0;
			break;
		}
	}
	if (!HBUF_PUTSL(ob, ".\n"))
		return 0;

	/* Now the table data. */

	return hbuf_putb(ob, content);
}

static int
rndr_table_row(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "\n");
}

static int
rndr_table_cell(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	const struct rndr_table_cell *param)
{

	if (param->col > 0 && !HBUF_PUTSL(ob, "|"))
		return 0;

	if (content->size) {
		if (!HBUF_PUTSL(ob, "T{\n"))
			return 0;
		if (!hbuf_putb(ob, content))
			return 0;
		if (!HBUF_PUTSL(ob, "\nT}"))
			return 0;
	}

	return 1;
}

static int
rndr_superscript(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (content->size == 0)
		return 1;

	/*
	 * If we have a macro contents, it might be the usual macro
	 * (solo in its buffer) or starting with a newline.
	 */

	if (content->data[0] == '.' ||
	    (content->size &&
	     content->data[0] == '\n' &&
	     content->data[1] == '.')) {
		if (!HBUF_PUTSL(ob, "\\u\\s-3"))
			return 0;
		if (content->data[0] != '\n' && !HBUF_PUTSL(ob, "\n"))
			return 0;
		if (!hbuf_putb(ob, content))
			return 0;
		if (!HBUF_NEWLINE(content, ob))
			return 0;
		if (!HBUF_PUTSL(ob, "\\s+3\\d\n"))
			return 0;
	} else {
		if (!HBUF_PUTSL(ob, "\\u\\s-3"))
			return 0;
		if (!hbuf_putb(ob, content))
			return 0;
		if (!HBUF_PUTSL(ob, "\\s+3\\d"))
			return 0;
	}

	return 1;
}

static int
rndr_normal_text(struct lowdown_buf *ob,
	const struct rndr_normal_text *param,
	const struct lowdown_node *prev, 
	const struct lowdown_node *next, 
	const struct nroff *st, int nl)
{
	size_t	 	 i, size;
	const char 	*data;

	if (param->text.size == 0)
		return 1;

	data = param->text.data + param->offs;
	size = param->text.size - param->offs;

	/* 
	 * If we don't have trailing space, omit the final word because
	 * we'll put that in the link's pdfhref.
	 */

	if (next != NULL &&
	    (next->type == LOWDOWN_LINK_AUTO ||
	     next->type == LOWDOWN_LINK))
		while (size && 
		       !isspace((unsigned char)data[size - 1]))
			size--;

	if (nl) {
		for (i = 0; i < size; i++)
			if (!isspace((unsigned char)data[i]))
				break;
		return rndr_block(ob, data + i, size - i);
	} 

	return rndr_span(ob, data, size);
}

static int
rndr_footnotes(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	const struct nroff *st)
{

	/* Put a horizontal line in the case of man(7). */

	if (content->size && st->man) {
		if (!HBUF_PUTSL(ob, ".LP\n"))
			return 0;
		if (!HBUF_PUTSL(ob, ".sp 3\n"))
			return 0;
		if (!HBUF_PUTSL(ob, "\\l\'2i'\n"))
			return 0;
	}

	return hbuf_putb(ob, content);
}

static int
rndr_footnote_def(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_footnote_def *param,
	const struct nroff *st)
{

	/* 
	 * Use groff_ms(7)-style footnotes.
	 * We know that the definitions are delivered in the same order
	 * as the footnotes are made, so we can use the automatic
	 * ordering facilities.
	 */

	if (!st->man) {
		if (!HBUF_PUTSL(ob, ".FS\n"))
			return 0;

		/* Ignore leading paragraph marker. */

		if (content->size > 3 &&
		    (memcmp(content->data, ".LP\n", 4) == 0 ||
		     memcmp(content->data, ".PP\n", 4) == 0)) {
			if (!hbuf_put(ob,
			    content->data + 4, content->size - 4))
				return 0;
		} else if (!hbuf_putb(ob, content))
			return 0;
		if (!HBUF_NEWLINE(content, ob))
			return 0;
		return HBUF_PUTSL(ob, ".FE\n");
	}

	/*
	 * For man(7), just print as normal, with a leading footnote
	 * number in italics and superscripted.
	 */

	if (!HBUF_PUTSL(ob, ".LP\n"))
		return 0;

	if (!hbuf_printf(ob, 
	    "\\0\\fI\\u\\s-3%zu\\s+3\\d\\fP\\0", param->num))
		return 0;

	if (content->size > 3 &&
	    (memcmp(content->data, ".PP\n", 4) == 0 ||
	     memcmp(content->data, ".LP\n", 4) == 0)) {
		if (!hbuf_put(ob, 
		    content->data + 4, content->size - 4))
			return 0;
	} else if (!hbuf_putb(ob, content))
		return 0;

	return HBUF_NEWLINE(content, ob);
}

static int
rndr_footnote_ref(struct lowdown_buf *ob,
	const struct rndr_footnote_ref *param,
	const struct nroff *st)
{

	/* 
	 * Use groff_ms(7)-style automatic footnoting, else just put a
	 * reference number in small superscripts.
	 */

	return !st->man ?
		HBUF_PUTSL(ob, "\\**") :
		hbuf_printf(ob, "\\u\\s-3%zu\\s+3\\d", param->num);
}

/*
 * Split "b" at sequential white-space, outputting the results in the
 * line-based "env" macro.
 * The content in "b" has already been escaped, so there's no need to do
 * anything but manage white-space.
 */
static int
rndr_meta_multi(struct lowdown_buf *ob, const char *b, const char *env)
{
	const char	*start;
	size_t		 sz, i, bsz;

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
		if (!hbuf_printf(ob, ".%s\n", env))
			return 0;
		if (!rndr_one_line_noescape(ob, start, sz, 1))
			return 0;
		if (!HBUF_PUTSL(ob, "\n"))
			return 0;
	}
	return 1;
}

static int
rndr_meta(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	struct lowdown_metaq *mq,
	const struct lowdown_node *n, struct nroff *st)
{
	struct lowdown_meta	*m;

	if ((m = calloc(1, sizeof(struct lowdown_meta))) == NULL)
		return 0;
	TAILQ_INSERT_TAIL(mq, m, entries);

	m->key = strndup(n->rndr_meta.key.data,
		n->rndr_meta.key.size);
	if (m->key == NULL)
		return 0;
	m->value = strndup(content->data, content->size);
	if (m->value == NULL)
		return 0;

	if (strcasecmp(m->key, "baseheaderlevel") == 0) {
		st->base_header_level = strtonum
			(m->value, 1, 1000, NULL);
		if (st->base_header_level == 0)
			st->base_header_level = 1;
	}
	return 1;
}

static int
rndr_doc_header(struct lowdown_buf *ob, 
	const struct lowdown_metaq *mq, const struct nroff *st)
{
	const struct lowdown_meta	*m;
	const char			*author = NULL, *title = NULL,
					*affil = NULL, *date = NULL,
					*copy = NULL, *section = NULL,
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
			section = m->value;
		else if (strcasecmp(m->key, "source") == 0)
			source = m->value;
		else if (strcasecmp(m->key, "volume") == 0)
			volume = m->value;

	/* Overrides. */

	if (title == NULL)
		title = "Untitled article";
	if (section == NULL)
		section = "7";
	if (rcsdate != NULL)
		date = rcsdate;
	if (rcsauthor != NULL)
		author = rcsauthor;

	if (!st->man) {
		if (copy != NULL) {
			if (!HBUF_PUTSL(ob, 
			     ".ds LF \\s-2Copyright \\(co "))
				return 0;
			if (!rndr_one_line_noescape
			    (ob, copy, strlen(copy), 0))
				return 0;
			if (!HBUF_PUTSL(ob, "\\s+2\n"))
				return 0;
		}
		if (date != NULL) {
			if (copy != NULL) {
				if (!HBUF_PUTSL(ob, ".ds RF \\s-2"))
					return 0;
				if (!rndr_one_line_noescape
				    (ob, date, strlen(date), 0))
					return 0;
				if (!HBUF_PUTSL(ob, "\\s+2\n"))
					return 0;
			} else {
				if (!HBUF_PUTSL(ob, ".DA \\s-2"))
					return 0;
				if (!rndr_one_line_noescape
				    (ob, date, strlen(date), 0))
					return 0;
				if (!HBUF_PUTSL(ob, "\\s+2\n"))
					return 0;
			}
		}
		if (!HBUF_PUTSL(ob, ".TL\n"))
			return 0;
		if (!rndr_one_line_noescape
		    (ob, title, strlen(title), 0))
			return 0;
		if (!HBUF_PUTSL(ob, "\n"))
			return 0;

		if (author != NULL && 
		    !rndr_meta_multi(ob, author, "AU"))
			return 0;
		if (affil != NULL &&
		    !rndr_meta_multi(ob, affil, "AI"))
			return 0;
	} else {
		/*
		 * The syntax of this macro, according to man(7), is 
		 * TH name section date [source [volume]].
		 */

		if (!HBUF_PUTSL(ob, ".TH \""))
			return 0;
		if (!rndr_one_line_noescape
		    (ob, title, strlen(title), 0))
			return 0;
		if (!HBUF_PUTSL(ob, "\" \""))
			return 0;
		if (!rndr_one_line_noescape
		    (ob, section, strlen(section), 0))
			return 0;
		if (!HBUF_PUTSL(ob, "\""))
			return 0;

		/*
		 * We may not have a date (or it may be empty), in which
		 * case man(7) says the current date is used.
		 */

		if (date != NULL) {
			if (!HBUF_PUTSL(ob, " \""))
				return 0;
			if (!rndr_one_line_noescape
			    (ob, date, strlen(date), 0))
				return 0;
			if (!HBUF_PUTSL(ob, "\""))
				return 0;
		} else {
			if (!HBUF_PUTSL(ob, " \"\""))
				return 0;
		}

		/*
		 * Don't print these unless necessary, as the latter
		 * overrides the default system printing for the
		 * section.
		 */

		if (source != NULL || volume != NULL) {
			if (source != NULL) {
				if (!HBUF_PUTSL(ob, " \""))
					return 0;
				if (!rndr_one_line_noescape
				    (ob, source, strlen(source), 0))
					return 0;
				if (!HBUF_PUTSL(ob, "\""))
					return 0;
			} else {
				if (!HBUF_PUTSL(ob, " \"\""))
					return 0;
			}

			if (volume != NULL) {
				if (!HBUF_PUTSL(ob, " \""))
					return 0;
				if (!rndr_one_line_noescape
				    (ob, volume, strlen(volume), 0))
					return 0;
				if (!HBUF_PUTSL(ob, "\""))
					return 0;
			} else if (source != NULL) {
				if (!HBUF_PUTSL(ob, " \"\""))
					return 0;
			}
		}

		if (!HBUF_PUTSL(ob, "\n"))
			return 0;
	}

	return 1;
}

/*
 * Actually render the node "n" and all of its children into the output
 * buffer "ob".
 * Return whether we should remove nodes relative to "n".
 */
static int
rndr(struct lowdown_buf *ob, struct lowdown_metaq *mq, 
	struct nroff *st, struct lowdown_node *n)
{
	struct lowdown_node	*child, *next, *prev;
	struct lowdown_buf	*tmp;
	int			 pnln, keepnext = 1, ret = 0, rc;
	int32_t			 ent;
	enum nfont		 fonts[NFONT__MAX];

	assert(n != NULL);

	if ((tmp = hbuf_new(64)) == NULL)
		return 0;

	memcpy(fonts, st->fonts, sizeof(fonts));

	/*
	 * Font management.
	 * roff doesn't handle its own font stack, so we can't set fonts
	 * and step out of them in a nested way.
	 */

	switch (n->type) {
	case LOWDOWN_CODESPAN:
		st->fonts[NFONT_FIXED]++;
		if (!hbuf_puts(ob, nstate_fonts(st)))
			goto out;
		break;
	case LOWDOWN_EMPHASIS:
		st->fonts[NFONT_ITALIC]++;
		if (!hbuf_puts(ob, nstate_fonts(st)))
			goto out;
		break;
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
		st->fonts[NFONT_BOLD]++;
		if (!hbuf_puts(ob, nstate_fonts(st)))
			goto out;
		break;
	case LOWDOWN_TRIPLE_EMPHASIS:
		st->fonts[NFONT_ITALIC]++;
		st->fonts[NFONT_BOLD]++;
		if (!hbuf_puts(ob, nstate_fonts(st)))
			goto out;
		break;
	default:
		break;
	}

	TAILQ_FOREACH(child, &n->children, entries)
		if (!rndr(tmp, mq, st, child))
			goto out;

	/* 
	 * Compute whether the previous output does have a newline:
	 * if we don't have a previous node, scan up to the parent and
	 * see if it's a block node.
	 * If it's a block node, we're absolutely starting on a newline;
	 * otherwise, we don't know.
	 */

	if (n->parent == NULL ||
	    (prev = TAILQ_PREV(n, lowdown_nodeq, entries)) == NULL)
		prev = n->parent;
	pnln = prev == NULL || nscopes[prev->type] == NSCOPE_BLOCK;

	/* Get the last and next emitted node. */

	prev = n->parent == NULL ? NULL : 
		TAILQ_PREV(n, lowdown_nodeq, entries);
	next = TAILQ_NEXT(n, entries);

	if (nscopes[n->type] == NSCOPE_BLOCK) {
		if (n->chng == LOWDOWN_CHNG_INSERT &&
		    !HBUF_PUTSL(ob, ".gcolor blue\n"))
			goto out;
		if (n->chng == LOWDOWN_CHNG_DELETE &&
		    !HBUF_PUTSL(ob, ".gcolor red\n"))
			goto out;
	} else {
		/*
		 * FIXME: this is going to disrupt our newline
		 * computation.
		 */
		if (n->chng == LOWDOWN_CHNG_INSERT &&
		    !HBUF_PUTSL(ob, "\\m[blue]"))
			goto out;
		if (n->chng == LOWDOWN_CHNG_DELETE &&
		    !HBUF_PUTSL(ob, "\\m[red]"))
			goto out;
	}

	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
		rc = rndr_blockcode(ob, &n->rndr_blockcode, st);
		break;
	case LOWDOWN_BLOCKQUOTE:
		rc = rndr_blockquote(ob, tmp, st);
		break;
	case LOWDOWN_DEFINITION:
		rc = rndr_list(ob, n, tmp, st);
		break;
	case LOWDOWN_DEFINITION_DATA:
		rc = rndr_definition_data(ob, n, tmp);
		break;
	case LOWDOWN_DEFINITION_TITLE:
		rc = rndr_definition_title(ob, n, tmp);
		break;
	case LOWDOWN_DOC_HEADER:
		rc = rndr_doc_header(ob, mq, st);
		break;
	case LOWDOWN_META:
		rc = rndr_meta(ob, tmp, mq, n, st);
		break;
	case LOWDOWN_HEADER:
		rc = rndr_header(ob, tmp, &n->rndr_header, st);
		break;
	case LOWDOWN_HRULE:
		rc = rndr_hrule(ob, st);
		break;
	case LOWDOWN_LIST:
		rc = rndr_list(ob, n, tmp, st);
		break;
	case LOWDOWN_LISTITEM:
		rc = rndr_listitem(ob, tmp, n, &n->rndr_listitem);
		break;
	case LOWDOWN_PARAGRAPH:
		rc = rndr_paragraph(ob, tmp, st, n);
		break;
	case LOWDOWN_TABLE_BLOCK:
		rc = rndr_table(ob, tmp, st);
		break;
	case LOWDOWN_TABLE_HEADER:
		rc = rndr_table_header(ob, tmp, &n->rndr_table_header);
		break;
	case LOWDOWN_TABLE_ROW:
		rc = rndr_table_row(ob, tmp);
		break;
	case LOWDOWN_TABLE_CELL:
		rc = rndr_table_cell(ob, tmp, &n->rndr_table_cell);
		break;
	case LOWDOWN_FOOTNOTES_BLOCK:
		rc = rndr_footnotes(ob, tmp, st);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rc = rndr_footnote_def(ob, 
			tmp, &n->rndr_footnote_def, st);
		break;
	case LOWDOWN_BLOCKHTML:
		rc = rndr_raw_block(ob, &n->rndr_blockhtml, st);
		break;
	case LOWDOWN_LINK_AUTO:
		keepnext = rndr_autolink(ob, &n->rndr_autolink,
			prev, next, st, pnln);
		break;
	case LOWDOWN_CODESPAN:
		rc = rndr_codespan(ob, &n->rndr_codespan);
		break;
	case LOWDOWN_IMAGE:
		rc = rndr_image(ob, &n->rndr_image, st, pnln, prev);
		break;
	case LOWDOWN_LINEBREAK:
		rc = rndr_linebreak(ob);
		break;
	case LOWDOWN_LINK:
		keepnext = rndr_link(ob, tmp, &n->rndr_link,
			st, prev, next, pnln);
		break;
	case LOWDOWN_STRIKETHROUGH:
		rc = rndr_strikethrough(ob, tmp);
		break;
	case LOWDOWN_SUPERSCRIPT:
		rc = rndr_superscript(ob, tmp);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		rc = rndr_footnote_ref(ob, &n->rndr_footnote_ref, st);
		break;
	case LOWDOWN_RAW_HTML:
		rc = rndr_raw_html(ob, &n->rndr_raw_html, st);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rc = rndr_normal_text(ob, &n->rndr_normal_text, 
			prev, next, st, pnln);
		break;
	case LOWDOWN_ENTITY:
		ent = entity_find_iso(&n->rndr_entity.text);
		rc = ent > 0 ?
			hbuf_printf(ob, "\\[u%.4llX]", 
				(unsigned long long)ent) :
			hbuf_putb(ob, &n->rndr_entity.text);
		break;
	default:
		rc = hbuf_putb(ob, tmp);
		break;
	}

	if (keepnext < 0 || !rc)
		goto out;

	if (n->chng == LOWDOWN_CHNG_INSERT ||
	    n->chng == LOWDOWN_CHNG_DELETE) {
		if (nscopes[n->type] == NSCOPE_BLOCK)
			rc = HBUF_PUTSL(ob, ".gcolor\n");
		else
			rc = HBUF_PUTSL(ob, "\\m[]");
	}
	if (!rc)
		goto out;

	/* Restore the font stack. */

	switch (n->type) {
	case LOWDOWN_CODESPAN:
	case LOWDOWN_EMPHASIS:
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
	case LOWDOWN_TRIPLE_EMPHASIS:
		memcpy(st->fonts, fonts, sizeof(fonts));
		if (!hbuf_puts(ob, nstate_fonts(st)))
			goto out;
		break;
	default:
		break;
	}

	/* 
	 * Our processors might want to remove the current or next node,
	 * so return that knowledge to the parent step.
	 */

	if (!keepnext) {
		assert(next->parent != NULL);
		TAILQ_REMOVE(&next->parent->children, next, entries);
		lowdown_node_free(next);
	}

	ret = 1;
out:
	hbuf_free(tmp);
	return ret;
}

int
lowdown_nroff_rndr(struct lowdown_buf *ob,
	struct lowdown_metaq *mq, void *arg, 
	struct lowdown_node *n)
{
	struct nroff		*st = arg;
	struct lowdown_metaq	 metaq;
	int			 rc;

	/* Temporary metaq if not provided. */

	if (mq == NULL) {
		TAILQ_INIT(&metaq);
		mq = &metaq;
	}

	memset(st->fonts, 0, sizeof(st->fonts));
	st->base_header_level = 1;
	st->post_para = 0;
	rc = rndr(ob, mq, st, n);

	/* Release temporary metaq. */

	if (mq == &metaq)
		lowdown_metaq_free(mq);

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
