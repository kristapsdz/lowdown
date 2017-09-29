/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016--2017 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
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
	do if ((_sz) > 0 && '\n' != (_buf)[(_sz) - 1]) \
		hbuf_putc((_ob), '\n'); \
	while (/* CONSTCOND */ 0)
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

struct 	nstate {
	int 		 mdoc; /* whether mdoc(7) */
	unsigned int 	 flags; /* output flags */
	enum nfont	 fonts[NFONT__MAX]; /* see nstate_fonts() */
};

static const enum nscope nscopes[LOWDOWN__MAX] = {
	NSCOPE_BLOCK, /* LOWDOWN_ROOT */
	NSCOPE_BLOCK, /* LOWDOWN_BLOCKCODE */
	NSCOPE_BLOCK, /* LOWDOWN_BLOCKQUOTE */
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
	NSCOPE_BLOCK /* LOWDOWN_DOC_FOOTER */
};

static void
escape_span(hbuf *ob, const char *source, size_t length)
{

	hesc_nroff(ob, source, length, 1, 0);
}

static void
escape_block(hbuf *ob, const char *source, size_t length)
{

	hesc_nroff(ob, source, length, 0, 0);
}

static void
escape_oneline_span(hbuf *ob, const char *source, size_t length)
{

	hesc_nroff(ob, source, length, 1, 1);
}

/*
 * Return the font string for the current set of fonts.
 * FIXME: I don't think this works for combinations of fixed-width,
 * bold, and italic.
 */
static const char *
nstate_fonts(const struct nstate *st)
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
	    (0 == st->fonts[NFONT_BOLD] &&
	     0 == st->fonts[NFONT_ITALIC]))
		(*cp++) = 'R';

	/* Reset. */
	if (0 == st->fonts[NFONT_BOLD] &&
	    0 == st->fonts[NFONT_FIXED] &&
	    0 == st->fonts[NFONT_ITALIC])
		(*cp++) = 'R';
	(*cp++) = ']';
	(*cp++) = '\0';
	return(fonts);
}

/*
 * Manage hypertext linking with the groff "pdfhref" macro or simply
 * using italics.
 * We use italics because the UR/UE macro doesn't support leading
 * un-spaced content, so "[foo](https://foo.com)" wouldn't work.
 * Until a solution is found, let's just italicise the link text (or
 * link, if no text is found).
 */
static int
putlink(hbuf *ob, struct nstate *st, const hbuf *link, 
	const hbuf *text, struct lowdown_node *next, 
	struct lowdown_node *prev, enum halink_type type)
{
	const hbuf	*buf;
	size_t		 i, pos;
	int		 ret = 1, usepdf;

	usepdf = ! st->mdoc && LOWDOWN_NROFF_GROFF & st->flags;

	if (usepdf)
		HBUF_PUTSL(ob, ".pdfhref W ");

	/*
	 * If we're preceded by normal text that doesn't end with space,
	 * then put that text into the "-P" (prefix) argument.
	 * If we're not in groff mode, emit the data, but without -P.
	 */

	if (NULL != prev &&
	    LOWDOWN_NORMAL_TEXT == prev->type) {
		buf = &prev->rndr_normal_text.text;
		i = buf->size;
		while (i && ! isspace((unsigned char)buf->data[i - 1]))
			i--;
		if (i != buf->size && usepdf)
			HBUF_PUTSL(ob, "-P \"");
		for (pos = i; pos < buf->size; pos++) {
			/* Be sure to escape... */
			if ('"' == buf->data[pos]) {
				HBUF_PUTSL(ob, "\\(dq");
				continue;
			} else if ('\\' == buf->data[pos]) {
				HBUF_PUTSL(ob, "\\e");
				continue;
			}
			hbuf_putc(ob, buf->data[pos]);
		}
		if (i != buf->size && usepdf)
			HBUF_PUTSL(ob, "\" ");
	}

	if ( ! usepdf) {
		st->fonts[NFONT_ITALIC]++;
		hbuf_puts(ob, nstate_fonts(st));
		if (NULL == text) {
			if (0 == hbuf_prefix(link, "mailto:"))
				hbuf_put(ob, link->data + 7, link->size - 7);
			else
				hbuf_put(ob, link->data, link->size);
		} else
			hbuf_put(ob, text->data, text->size);
		st->fonts[NFONT_ITALIC]--;
		hbuf_puts(ob, nstate_fonts(st));
	}

	/*
	 * If we're followed by normal text that doesn't begin with a
	 * space, use the "-A" (affix) option to prevent a space before
	 * what follows.
	 * But first, initialise the offset.
	 */

	if (NULL != next && 
	    LOWDOWN_NORMAL_TEXT == next->type)
		next->rndr_normal_text.offs = 0;

	if (NULL != next && 
	    LOWDOWN_NORMAL_TEXT == next->type &&
	    next->rndr_normal_text.text.size > 0 &&
	    ' ' != next->rndr_normal_text.text.data[0]) {
		buf = &next->rndr_normal_text.text;
		if (usepdf)
			HBUF_PUTSL(ob, "-A \"");
		for (pos = 0; pos < buf->size; pos++) {
			if (isspace((unsigned char)buf->data[pos]))
				break;
			/* Be sure to escape... */
			if ('"' == buf->data[pos]) {
				HBUF_PUTSL(ob, "\\(dq");
				continue;
			} else if ('\\' == buf->data[pos]) {
				HBUF_PUTSL(ob, "\\e");
				continue;
			}
			hbuf_putc(ob, buf->data[pos]);
		}
		ret = pos < buf->size;
		next->rndr_normal_text.offs = pos;
		if (usepdf)
			HBUF_PUTSL(ob, "\" ");
	}

	/* Encode the URL. */

	if (usepdf) {
		HBUF_PUTSL(ob, "-D ");
		if (HALINK_EMAIL == type)
			HBUF_PUTSL(ob, "mailto:");
		for (i = 0; i < link->size; i++) {
			if ( ! isprint((unsigned char)link->data[i]) ||
			    NULL != strchr("<>\\^`{|}\"", link->data[i]))
				hbuf_printf(ob, "%%%.2X", link->data[i]);
			else
				hbuf_putc(ob, link->data[i]);
		}
		HBUF_PUTSL(ob, " ");
		if (NULL == text) {
			if (0 == hbuf_prefix(link, "mailto:"))
				hbuf_put(ob, link->data + 7, link->size - 7);
			else
				hbuf_put(ob, link->data, link->size);
		} else
			hesc_nroff(ob, text->data, text->size, 0, 1);
	}

	HBUF_PUTSL(ob, "\n");
	return ret;
}

static int
rndr_autolink(hbuf *ob, const hbuf *link, enum halink_type type, 
	struct lowdown_node *prev, struct lowdown_node *next,
	struct nstate *st, int nln)
{

	if (NULL == link || 0 == link->size)
		return 1;

	if ( ! nln)
		if (NULL == prev ||
		    (ob->size && '\n' != ob->data[ob->size - 1]))
			HBUF_PUTSL(ob, "\n");

	return putlink(ob, st, link, NULL, next, prev, type);
}

static void
rndr_blockcode(hbuf *ob, const hbuf *content, 
	const hbuf *lang, const struct nstate *st)
{

	if (NULL == content || 0 == content->size)
		return;

	if (st->mdoc) {
		HBUF_PUTSL(ob, ".sp 1\n");
		HBUF_PUTSL(ob, ".nf\n");
	} else
		HBUF_PUTSL(ob, ".LD\n");

	HBUF_PUTSL(ob, ".ft CR\n");
	escape_block(ob, content->data, content->size);
	HBUF_NEWLINE(content, ob);
	HBUF_PUTSL(ob, ".ft\n");

	if (st->mdoc)
		HBUF_PUTSL(ob, ".fi\n");
	else
		HBUF_PUTSL(ob, ".DE\n");
}

static void
rndr_blockquote(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return;

	HBUF_PUTSL(ob, ".RS\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_NEWLINE(content, ob);
	HBUF_PUTSL(ob, ".RE\n");
}

static void
rndr_codespan(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return;

	escape_span(ob, content->data, content->size);
}

/*
 * FIXME: not supported.
 */
static int
rndr_strikethrough(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return(0);

	hbuf_put(ob, content->data, content->size);
	return 1;
}

static int
rndr_linebreak(hbuf *ob)
{

	/* FIXME: should this always have a newline? */

	HBUF_PUTSL(ob, "\n.br\n");
	return 1;
}

/*
 * For man(7), we use SH for the first-level section, SS for other
 * sections.
 * FIXME: use PP then italics or something for third-level etc.
 * For ms(7), just use SH.
 * (Again, see above FIXME.)
 * If we're using ms(7) w/groff extensions, used the numbered version of
 * the SH macro.
 * If we're numbered ms(7), use NH.
 * If we're numbered ms(7) w/extensions, use NH and XN (-mspdf).
 */
static void
rndr_header(hbuf *ob, const hbuf *content, int level,
	const struct nstate *st)
{

	if (NULL == content || 0 == content->size)
		return;

	if (st->mdoc) {
		if (1 == level)
			HBUF_PUTSL(ob, ".SH ");
		else 
			HBUF_PUTSL(ob, ".SS ");
		escape_oneline_span(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\n");
		return;
	} 

	if (st->flags & LOWDOWN_NROFF_NUMBERED)
		hbuf_printf(ob, ".NH %d\n", level);
	else if (st->flags & LOWDOWN_NROFF_GROFF) 
		hbuf_printf(ob, ".SH %d\n", level);
	else
		hbuf_printf(ob, ".SH\n");

	if ((st->flags & LOWDOWN_NROFF_NUMBERED) &&
	    (st->flags & LOWDOWN_NROFF_GROFF)) {
		HBUF_PUTSL(ob, ".XN ");
		escape_oneline_span(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\n");
	} else {
		hbuf_put(ob, content->data, content->size);
		HBUF_NEWLINE(content, ob);
	}
}

static int
rndr_link(hbuf *ob, const hbuf *content, const hbuf *link, 
	struct nstate *st, struct lowdown_node *prev, 
	struct lowdown_node *next, int nln)
{

	if ((NULL == content || 0 == content->size) &&
	    (NULL == link || 0 == link->size))
		return 1;

	if ( ! nln)
		if (NULL == prev ||
		    (ob->size && '\n' != ob->data[ob->size - 1]))
			HBUF_PUTSL(ob, "\n");

	return putlink(ob, st, link, 
		content, next, prev, HALINK_NORMAL);
}

static void
rndr_listitem(hbuf *ob, const hbuf *content, 
	enum hlist_fl flags, size_t num)
{

	if (NULL == content || 0 == content->size)
		return;

	/* 
	 * Start out with a vertical spacing, then put us into an
	 * indented paragraph.
	 */

	HBUF_PUTSL(ob, ".sp 0.5\n");
	HBUF_PUTSL(ob, ".RS\n");

	/* 
	 * Now back out by the size of our list glyph(s) and print the
	 * glyph(s) (padding with two spaces).
	 */

	if (HLIST_FL_ORDERED & flags)
		hbuf_printf(ob, ".ti -\\w'%zu.  \'u\n%zu.  ", 
			num, num);
	else
		HBUF_PUTSL(ob, ".ti -\\w'\\(bu  \'u\n\\(bu  ");

	/* Strip out any leading paragraph marker. */

	if (content->size > 3 &&
	    0 == memcmp(content->data, ".LP\n", 4))
		hbuf_put(ob, content->data + 4, content->size - 4);
	else
		hbuf_put(ob, content->data, content->size);

	HBUF_NEWLINE(content, ob);
	HBUF_PUTSL(ob, ".RE\n");
}

static void
rndr_paragraph(hbuf *ob, const hbuf *content, 
	const struct nstate *st, const struct lowdown_node *np)
{
	size_t	 	 i = 0, org;

	if (NULL == content || 0 == content->size)
		return;

	/* Strip away initial white-space. */

	while (i < content->size && 
	       isspace((unsigned char)content->data[i]))
		i++;
	if (i == content->size)
		return;

	HBUF_PUTSL(ob, ".LP\n");

	if (st->flags & LOWDOWN_NROFF_HARD_WRAP) {
		while (i < content->size) {
			org = i;
			while (i < content->size && content->data[i] != '\n')
				i++;

			if (i > org)
				hbuf_put(ob, content->data + org, i - org);

			/*
			 * do not insert a line break if this newline
			 * is the last character on the paragraph
			 */
			if (i >= content->size - 1)
				break;

			rndr_linebreak(ob);
			i++;
		}
	} else
		hbuf_put(ob, content->data + i, content->size - i);

	BUFFER_NEWLINE(content->data + i, content->size - i, ob);
}

/*
 * FIXME: verify behaviour.
 */
static void
rndr_raw_block(hbuf *ob, const hbuf *content, const struct nstate *st)
{
	size_t org, sz;

	if (NULL == content)
		return;

	if (st->flags & LOWDOWN_NROFF_SKIP_HTML) {
		escape_block(ob, content->data, content->size);
		return;
	}

	/*
	 * FIXME: Do we *really* need to trim the HTML? How does that
	 * make a difference?
	 */

	sz = content->size;
	while (sz > 0 && content->data[sz - 1] == '\n')
		sz--;

	org = 0;
	while (org < sz && content->data[org] == '\n')
		org++;

	if (org >= sz)
		return;

	if (ob->size)
		hbuf_putc(ob, '\n');

	hbuf_put(ob, content->data + org, sz - org);
	hbuf_putc(ob, '\n');
}

static void
rndr_hrule(hbuf *ob, const struct nstate *st)
{

	/*
	 * I'm not sure how else to do horizontal lines.
	 * The LP is to reset the margins.
	 */

	HBUF_PUTSL(ob, ".LP\n");
	if ( ! st->mdoc)
		HBUF_PUTSL(ob, "\\l\'\\n(.lu-\\n(\\n[.in]u\'\n");
}

static void
rndr_image(hbuf *ob, const hbuf *link, const struct nstate *st, 
	int nln, const struct lowdown_node *prev)
{
	const char	*cp;
	size_t		 sz;

	if (st->mdoc) {
		warnx("warning: images not supported");
		return;
	}

	cp = memrchr(link->data, '.', link->size);
	if (NULL == cp) {
		warnx("warning: no image suffix (ignoring)");
		return;
	}

	cp++;
	sz = link->size - (cp - link->data);

	if (0 == sz) {
		warnx("warning: empty image suffix (ignoring)");
		return;
	}

	if ( ! (2 == sz && 0 == memcmp(cp, "ps", 2)) &&
	     ! (3 == sz && 0 == memcmp(cp, "eps", 3))) {
		warnx("warning: unknown image suffix (ignoring)");
		return;
	}

	if ( ! nln)
		if (NULL == prev ||
		    (ob->size && '\n' != ob->data[ob->size - 1]))
			HBUF_PUTSL(ob, "\n");

	hbuf_printf(ob, ".PSPIC %.*s\n", 
		(int)link->size, link->data);
}

static int
rndr_raw_html(hbuf *ob, const hbuf *text, const struct nstate *st)
{

	if ((st->flags & LOWDOWN_NROFF_SKIP_HTML) != 0)
		return 1;

	escape_block(ob, text->data, text->size);
	return 1;
}

static void
rndr_table(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, ".TS\n");
	HBUF_PUTSL(ob, "tab(|) allbox;\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_NEWLINE(content, ob);
	HBUF_PUTSL(ob, ".TE\n");
}

static void
rndr_table_header(hbuf *ob, const hbuf *content,
	const enum htbl_flags *fl, size_t columns)
{
	size_t	 i;

	for (i = 0; i < columns; i++) {
		if (i > 0)
			HBUF_PUTSL(ob, " ");
		switch (fl[i] & HTBL_FL_ALIGNMASK) {
		case (HTBL_FL_ALIGN_CENTER):
			HBUF_PUTSL(ob, "c");
			break;
		case (HTBL_FL_ALIGN_RIGHT):
			HBUF_PUTSL(ob, "r");
			break;
		default:
			HBUF_PUTSL(ob, "l");
			break;
		}
	}
	HBUF_PUTSL(ob, ".\n");
	hbuf_put(ob, content->data, content->size);
}

static void
rndr_table_body(hbuf *ob, const hbuf *content)
{

	hbuf_put(ob, content->data, content->size);
}

static void
rndr_tablerow(hbuf *ob, const hbuf *content)
{

	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\n");
}

static void
rndr_tablecell(hbuf *ob, const hbuf *content,
	enum htbl_flags flags, size_t col, size_t columns)
{

	if (col > 0)
		HBUF_PUTSL(ob, "|");
	if (NULL != content && content->size) {
		HBUF_PUTSL(ob, "T{\n");
		hbuf_put(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\nT}");
	}
}

static int
rndr_superscript(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return 0;

	/*
	 * If we have a macro contents, it might be the usual macro
	 * (solo in its buffer) or starting with a newline.
	 */

	if ('.' == content->data[0] ||
	    (content->size &&
	     '\n' == content->data[0] &&
	     '.' == content->data[1])) {
		HBUF_PUTSL(ob, "\\u\\s-3");
		if ('\n' != content->data[0])
			HBUF_PUTSL(ob, "\n");
		hbuf_put(ob, content->data, content->size);
		if (content->size && 
		    '\n' != content->data[content->size - 1])
			HBUF_PUTSL(ob, "\n");
		HBUF_PUTSL(ob, "\\s+3\\d\n");
	} else {
		HBUF_PUTSL(ob, "\\u\\s-3");
		hbuf_put(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\\s+3\\d");
	}
	return 1;
}

static void
rndr_normal_text(hbuf *ob, const hbuf *content, size_t offs, 
	const struct lowdown_node *prev, 
	const struct lowdown_node *next, 
	const struct nstate *st, int nl)
{
	size_t	 	 i, size;
	const char 	*data;

	if (NULL == content || 0 == content->size)
		return;

	data = content->data + offs;
	size = content->size - offs;

	/* 
	 * If we don't have trailing space, omit the final word because
	 * we'll put that in the link's pdfhref.
	 */

	if (NULL != next &&
	    (LOWDOWN_LINK_AUTO == next->type ||
	     LOWDOWN_LINK == next->type))
		while (size && 
		       ! isspace((unsigned char)data[size - 1]))
			size--;

	if (nl) {
		for (i = 0; i < size; i++)
			if ( ! isspace((unsigned char)data[i]))
				break;
		escape_block(ob, data + i, size - i);
	} else
		escape_span(ob, data, size);
}

static void
rndr_footnotes(hbuf *ob, const hbuf *content, const struct nstate *st)
{

	if (NULL == content || 0 == content->size)
		return;

	/* Put a horizontal line in the case of man(7). */

	if (st->mdoc) {
		HBUF_PUTSL(ob, ".LP\n");
		HBUF_PUTSL(ob, ".sp 3\n");
		HBUF_PUTSL(ob, "\\l\'2i'\n");
	}
	hbuf_put(ob, content->data, content->size);
}

static void
rndr_footnote_def(hbuf *ob, const hbuf *content, unsigned int num,
	const struct nstate *st)
{

	/* 
	 * Use groff_ms(7)-style footnotes.
	 * We know that the definitions are delivered in the same order
	 * as the footnotes are made, so we can use the automatic
	 * ordering facilities.
	 */

	if ( ! st->mdoc) {
		HBUF_PUTSL(ob, ".FS\n");
		/* Ignore leading paragraph marker. */
		if (content->size > 3 &&
		    0 == memcmp(content->data, ".LP\n", 4))
			hbuf_put(ob, content->data + 4, content->size - 4);
		else
			hbuf_put(ob, content->data, content->size);
		HBUF_NEWLINE(content, ob);
		HBUF_PUTSL(ob, ".FE\n");
		return;
	}

	/*
	 * For man(7), just print as normal, with a leading footnote
	 * number in italics and superscripted.
	 */

	HBUF_PUTSL(ob, ".LP\n");
	hbuf_printf(ob, "\\0\\fI\\u\\s-3%u\\s+3\\d\\fP\\0", num);
	if (content->size > 3 &&
	    0 == memcmp(content->data, ".LP\n", 4))
		hbuf_put(ob, content->data + 4, content->size - 4);
	else
		hbuf_put(ob, content->data, content->size);
	HBUF_NEWLINE(content, ob);
}

static void
rndr_footnote_ref(hbuf *ob, unsigned int num, const struct nstate *st)
{

	/* 
	 * Use groff_ms(7)-style automatic footnoting, else just put a
	 * reference number in small superscripts.
	 */

	if ( ! st->mdoc)
		HBUF_PUTSL(ob, "\\**");
	else
		hbuf_printf(ob, "\\u\\s-3%u\\s+3\\d", num);
}

static int
rndr_math(void)
{

	/* FIXME: use lowdown_opts warnings. */
	warnx("warning: math not supported");
	return 1;
}

/*
 * Convert an ISO date (y/m/d or y-m-d) to a canonical form.
 * Returns NULL if the string is malformed at all or the date otherwise.
 */
static char *
date2str(const char *v)
{
	unsigned int	y, m, d;
	int		rc;
	static char	buf[32];

	if (NULL == v)
		return(NULL);

	rc = sscanf(v, "%u/%u/%u", &y, &m, &d);
	if (3 != rc) {
		rc = sscanf(v, "%u-%u-%u", &y, &m, &d);
		if (3 != rc)
			return(NULL);
	}

	snprintf(buf, sizeof(buf), "%u-%.2u-%.2u", y, m, d);
	return(buf);
}

/*
 * Convert the "$Author$" string to just the author in a static
 * buffer of a fixed length.
 * Returns NULL if the string is malformed (too long, too short, etc.)
 * at all or the author name otherwise.
 */
static char *
rcsauthor2str(const char *v)
{
	static char	buf[1024];
	size_t		sz;

	if (NULL == v ||
	    strlen(v) < 12 ||
	    strncmp(v, "$Author: ", 9))
		return(NULL);

	if ((sz = strlcpy(buf, v + 9, sizeof(buf))) >= sizeof(buf))
		return(NULL);

	if ('$' == buf[sz - 1])
		buf[sz - 1] = '\0';
	if (' ' == buf[sz - 2])
		buf[sz - 2] = '\0';

	return(buf);
}

/*
 * Convert the "$Date$" string to a simple ISO date in a
 * static buffer.
 * Returns NULL if the string is malformed at all or the date otherwise.
 */
static char *
rcsdate2str(const char *v)
{
	unsigned int	y, m, d, h, min, s;
	int		rc;
	static char	buf[32];

	if (NULL == v ||
	    strlen(v) < 10 ||
	    strncmp(v, "$Date: ", 7))
		return(NULL);

	rc = sscanf(v + 7, "%u/%u/%u %u:%u:%u", 
		&y, &m, &d, &h, &min, &s);

	if (6 != rc)
		return(NULL);

	snprintf(buf, sizeof(buf), "%u-%.2u-%.2u", y, m, d);
	return(buf);
}

/*
 * Itereate through multiple multi-white-space separated values in
 * "val", filling them in to "env".
 */
static void
rndr_doc_header_multi(hbuf *ob, const char *val, const char *env)
{
	const char	*cp, *start;
	size_t		 sz;

	for (cp = val; '\0' != *cp; ) {
		while (isspace((unsigned char)*cp))
			cp++;
		if ('\0' == *cp)
			continue;
		start = cp;
		sz = 0;
		while ('\0' != *cp) {
			if ( ! isspace((unsigned char)cp[0]) ||
			     ! isspace((unsigned char)cp[1])) {
				sz++;
				cp++;
				continue;
			}
			cp += 2;
			break;
		}
		if (0 == sz)
			continue;
		hbuf_printf(ob, ".%s\n", env);
		hesc_nroff(ob, start, sz, 0, 1);
		hbuf_putc(ob, '\n');
	}
}

static void
rndr_doc_header(hbuf *ob, 
	const struct lowdown_meta *m, size_t msz, 
	const struct nstate *st)
{
	const char	*date = NULL, *author = NULL,
	      		*title = "Untitled article", *affil = NULL;
	time_t		 t;
	char		 buf[32];
	struct tm	*tm;
	size_t		 i;

	if ( ! (LOWDOWN_STANDALONE & st->flags))
		return;

	/* Acquire metadata that we'll fill in. */

	for (i = 0; i < msz; i++) 
		if (0 == strcmp(m[i].key, "title"))
			title = m[i].value;
		else if (0 == strcmp(m[i].key, "affiliation"))
			affil = m[i].value;
		else if (0 == strcmp(m[i].key, "author"))
			author = m[i].value;
		else if (0 == strcmp(m[i].key, "rcsauthor"))
			author = rcsauthor2str(m[i].value);
		else if (0 == strcmp(m[i].key, "rcsdate"))
			date = rcsdate2str(m[i].value);
		else if (0 == strcmp(m[i].key, "date"))
			date = date2str(m[i].value);

	/* FIXME: convert to buf without strftime. */

	if (NULL == date) {
		t = time(NULL);
		tm = localtime(&t);
		strftime(buf, sizeof(buf), "%F", tm);
		date = buf;
	}

	/* Strip leading newlines (empty ok but weird) */

	while (isspace((unsigned char)*title))
		title++;

	/* Emit our authors and title. */

	if ( ! st->mdoc) {
		HBUF_PUTSL(ob, ".nr PS 10\n");
		HBUF_PUTSL(ob, ".nr GROWPS 3\n");
		hbuf_printf(ob, ".DA %s\n.TL\n", date);
		escape_block(ob, title, strlen(title));
		HBUF_PUTSL(ob, "\n");
		if (NULL != author)
			rndr_doc_header_multi(ob, author, "AU");
		if (NULL != affil)
			rndr_doc_header_multi(ob, affil, "AI");
	} else {
		HBUF_PUTSL(ob, ".TH \"");
		escape_oneline_span(ob, title, strlen(title));
		hbuf_printf(ob, "\" 7 %s\n", date);
	}
}

/*
 * Actually render the node "root" and all of its children into the
 * output buffer "ob".
 * Return whether we should remove nodes relative to "root".
 */
static void
rndr(hbuf *ob, struct nstate *ref, struct lowdown_node *root)
{
	struct lowdown_node *n, *next, *prev;
	hbuf		*tmp;
	int		 pnln, keepnext;
	enum nfont	 fonts[NFONT__MAX];

	assert(NULL != root);

	tmp = hbuf_new(64);

	memcpy(fonts, ref->fonts, sizeof(fonts));

	/*
	 * Font management.
	 * roff doesn't handle its own font stack, so we can't set fonts
	 * and step out of them in a nested way.
	 */

	switch (root->type) {
	case (LOWDOWN_CODESPAN):
		ref->fonts[NFONT_FIXED]++;
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	case (LOWDOWN_EMPHASIS):
		ref->fonts[NFONT_ITALIC]++;
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	case (LOWDOWN_HIGHLIGHT):
	case (LOWDOWN_DOUBLE_EMPHASIS):
		ref->fonts[NFONT_BOLD]++;
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	case (LOWDOWN_TRIPLE_EMPHASIS):
		ref->fonts[NFONT_ITALIC]++;
		ref->fonts[NFONT_BOLD]++;
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	default:
		break;
	}

	TAILQ_FOREACH(n, &root->children, entries)
		rndr(tmp, ref, n);

	/* Compute whether the previous output has a newline. */

	if (NULL == root->parent ||
	    NULL == (n = TAILQ_PREV(root, lowdown_nodeq, entries)))
		n = root->parent;

	pnln = NULL == n || NSCOPE_BLOCK == nscopes[n->type];

	prev = NULL == root->parent ? NULL : 
		TAILQ_PREV(root, lowdown_nodeq, entries);
	next = TAILQ_NEXT(root, entries);
	keepnext = 1;

	switch (root->type) {
	case (LOWDOWN_BLOCKCODE):
		rndr_blockcode(ob, 
			&root->rndr_blockcode.text, 
			&root->rndr_blockcode.lang, ref);
		break;
	case (LOWDOWN_BLOCKQUOTE):
		rndr_blockquote(ob, tmp);
		break;
	case (LOWDOWN_DOC_HEADER):
		rndr_doc_header(ob, 
			root->rndr_doc_header.m, 
			root->rndr_doc_header.msz, ref);
		break;
	case (LOWDOWN_HEADER):
		rndr_header(ob, tmp, 
			root->rndr_header.level, ref);
		break;
	case (LOWDOWN_HRULE):
		rndr_hrule(ob, ref);
		break;
	case (LOWDOWN_LISTITEM):
		rndr_listitem(ob, tmp, 
			root->rndr_listitem.flags,
			root->rndr_listitem.num);
		break;
	case (LOWDOWN_PARAGRAPH):
		rndr_paragraph(ob, tmp, ref, root->parent);
		break;
	case (LOWDOWN_TABLE_BLOCK):
		rndr_table(ob, tmp);
		break;
	case (LOWDOWN_TABLE_HEADER):
		rndr_table_header(ob, tmp, 
			root->rndr_table_header.flags,
			root->rndr_table_header.columns);
		break;
	case (LOWDOWN_TABLE_BODY):
		rndr_table_body(ob, tmp);
		break;
	case (LOWDOWN_TABLE_ROW):
		rndr_tablerow(ob, tmp);
		break;
	case (LOWDOWN_TABLE_CELL):
		rndr_tablecell(ob, tmp, 
			root->rndr_table_cell.flags, 
			root->rndr_table_cell.col,
			root->rndr_table_cell.columns);
		break;
	case (LOWDOWN_FOOTNOTES_BLOCK):
		rndr_footnotes(ob, tmp, ref);
		break;
	case (LOWDOWN_FOOTNOTE_DEF):
		rndr_footnote_def(ob, tmp, 
			root->rndr_footnote_def.num, ref);
		break;
	case (LOWDOWN_BLOCKHTML):
		rndr_raw_block(ob, tmp, ref);
		break;
	case (LOWDOWN_LINK_AUTO):
		keepnext = rndr_autolink(ob, 
			&root->rndr_autolink.link,
			root->rndr_autolink.type,
			prev, next, ref, pnln);
		break;
	case (LOWDOWN_CODESPAN):
		rndr_codespan(ob, &root->rndr_codespan.text);
		break;
	case (LOWDOWN_IMAGE):
		rndr_image(ob, &root->rndr_image.link,
			ref, pnln, prev);
		break;
	case (LOWDOWN_LINEBREAK):
		rndr_linebreak(ob);
		break;
	case (LOWDOWN_LINK):
		keepnext = rndr_link(ob, tmp,
			&root->rndr_link.link,
			ref, prev, next, pnln);
		break;
	case (LOWDOWN_STRIKETHROUGH):
		rndr_strikethrough(ob, tmp);
		break;
	case (LOWDOWN_SUPERSCRIPT):
		rndr_superscript(ob, tmp);
		break;
	case (LOWDOWN_FOOTNOTE_REF):
		rndr_footnote_ref(ob, 
			root->rndr_footnote_ref.num, ref);
		break;
	case (LOWDOWN_MATH_BLOCK):
		rndr_math();
		break;
	case (LOWDOWN_RAW_HTML):
		rndr_raw_html(ob, &root->rndr_raw_html.text, ref);
		break;
	case (LOWDOWN_NORMAL_TEXT):
		rndr_normal_text(ob, 
			&root->rndr_normal_text.text, 
			root->rndr_normal_text.offs,
			prev, next, ref, pnln);
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

	/* Restore the font stack. */

	switch (root->type) {
	case (LOWDOWN_CODESPAN):
	case (LOWDOWN_EMPHASIS):
	case (LOWDOWN_HIGHLIGHT):
	case (LOWDOWN_DOUBLE_EMPHASIS):
	case (LOWDOWN_TRIPLE_EMPHASIS):
		memcpy(ref->fonts, fonts, sizeof(fonts));
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	default:
		break;
	}

	hbuf_free(tmp);

	/* 
	 * Our processors might want to remove the current or next node,
	 * so return that knowledge to the parent step.
	 */

	if ( ! keepnext) {
		assert(NULL != next->parent);
		TAILQ_REMOVE(&next->parent->children, next, entries);
		lowdown_node_free(next);
	}
}

void
lowdown_nroff_rndr(hbuf *ob, void *ref, struct lowdown_node *root)
{
	struct nstate	*st = ref;

	memset(st->fonts, 0, sizeof(st->fonts));
	rndr(ob, ref, root);
}

void *
lowdown_nroff_new(const struct lowdown_opts *opts)
{
	struct nstate 	*state;

	/* Prepare the state pointer. */

	state = xcalloc(1, sizeof(struct nstate));

	state->flags = NULL != opts ? opts->oflags : 0;
	state->mdoc = NULL != opts && LOWDOWN_MAN == opts->type;

	return state;
}

void
lowdown_nroff_free(void *data)
{

	free(data);
}
