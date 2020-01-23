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

enum rndr_meta_key {
	RNDR_META_AFFIL,
	RNDR_META_AUTHOR,
	RNDR_META_COPY,
	RNDR_META_DATE,
	RNDR_META_RCSAUTHOR,
	RNDR_META_RCSDATE,
	RNDR_META_TITLE,
	RNDR_META__MAX
};

static const char *rndr_meta_keys[RNDR_META__MAX] = {
	"affiliation", /* RNDR_META_AFFIL */
	"author", /* RNDR_META_AUTHOR */
	"copyright", /* RNDR_META_COPY */
	"date", /* RNDR_META_DATE */
	"rcsauthor", /* RNDR_META_RCSAUTHOR */
	"rcsdate", /* RNDR_META_RCSDATE */
	"title", /* RNDR_META_TITLE */
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
	NSCOPE_BLOCK, /* LOWDOWN_META */
	NSCOPE_BLOCK /* LOWDOWN_DOC_FOOTER */
};

/*
 * If "span" is non-zero, don't test for leading periods.
 * Otherwise, a leading period will be escaped.
 * If "oneline" is non-zero, newlines are replaced with spaces.
 */
static void
hesc_nroff(hbuf *ob, const char *data, 
	size_t size, int span, int oneline)
{
	size_t	 i;

	if (0 == size)
		return;

	if (!span && data[0] == '.')
		HBUF_PUTSL(ob, "\\&");

	/*
	 * According to mandoc_char(7), we need to escape the backtick,
	 * single apostrophe, and tilde or else they'll be considered as
	 * special Unicode output.
	 * Slashes need to be escaped too, and newlines if appropriate
	 */

	for (i = 0; i < size; i++)
		switch (data[i]) {
		case '^':
			HBUF_PUTSL(ob, "\\(ha");
			break;
		case '~':
			HBUF_PUTSL(ob, "\\(ti");
			break;
		case '`':
			HBUF_PUTSL(ob, "\\(ga");
			break;
		case '\n':
			hbuf_putc(ob, oneline ? ' ' : '\n');
			break;
		case '\\':
			HBUF_PUTSL(ob, "\\e");
			break;
		case '.':
			if (!oneline && i && data[i - 1] == '\n')
				HBUF_PUTSL(ob, "\\&");
			/* FALLTHROUGH */
		default:
			hbuf_putc(ob, data[i]);
			break;
		}
}

/*
 * Output "source" of size "length" on as many lines as required,
 * starting on a line with existing content.
 * Escapes text so as not to be roff.
 */
static void
rndr_span(hbuf *ob, const char *source, size_t length)
{

	hesc_nroff(ob, source, length, 1, 0);
}

/*
 * Output "source" of size "length" on as many lines as required,
 * starting on its own line.
 * Escapes text so as not to be roff.
 */
static void
rndr_block(hbuf *ob, const char *source, size_t length)
{

	hesc_nroff(ob, source, length, 0, 0);
}

/*
 * Output "source" of size "length" on one line, starting on a line with
 * existing content.
 * Escapes text so as not to be roff.
 */
static void
rndr_one_line_span(hbuf *ob, const char *source, size_t length)
{

	hesc_nroff(ob, source, length, 1, 1);
}

/*
 * Output "source" of size "length" on a single line.
 * Does not escape the given text, which should already have been
 * escaped, unless "ownline" is given, in which case make sure we don't
 * start with roff.
 */
static void
rndr_one_line_noescape(hbuf *ob,
	const char *source, size_t length, int ownline)
{
	size_t	 i;

	if (ownline && length && source[0] == '.')
		HBUF_PUTSL(ob, "\\&");
	for (i = 0; i < length; i++)
		if (isspace((unsigned char)source[i]))
			HBUF_PUTSL(ob, " ");
		else
			hbuf_putc(ob, source[i]);
}

/*
 * See rndr_one_line_noescape().
 */
static void
rndr_one_lineb_noescape(hbuf *ob, const hbuf *b, int ownline)
{

	return rndr_one_line_noescape(ob, b->data, b->size, ownline);
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
 */
static int
putlink(hbuf *ob, struct nstate *st, const hbuf *link, 
	const hbuf *text, struct lowdown_node *next, 
	struct lowdown_node *prev, enum halink_type type)
{
	const hbuf	*buf;
	size_t		 i, pos;
	int		 ret = 1, usepdf;

	usepdf = !st->mdoc && (st->flags & LOWDOWN_NROFF_GROFF);

	if (usepdf)
		HBUF_PUTSL(ob, ".pdfhref W ");

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
		if (i != buf->size && usepdf)
			HBUF_PUTSL(ob, "-P \"");
		for (pos = i; pos < buf->size; pos++) {
			/* Be sure to escape... */
			if (buf->data[pos] == '"') {
				HBUF_PUTSL(ob, "\\(dq");
				continue;
			} else if (buf->data[pos] == '\\') {
				HBUF_PUTSL(ob, "\\e");
				continue;
			}
			hbuf_putc(ob, buf->data[pos]);
		}
		if (i != buf->size && usepdf)
			HBUF_PUTSL(ob, "\" ");
	}

	if (!usepdf) {
		st->fonts[NFONT_ITALIC]++;
		hbuf_puts(ob, nstate_fonts(st));
		if (text == NULL) {
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

	if (next != NULL && 
	    next->type == LOWDOWN_NORMAL_TEXT)
		next->rndr_normal_text.offs = 0;

	if (next != NULL && 
	    next->type == LOWDOWN_NORMAL_TEXT &&
	    next->rndr_normal_text.text.size > 0 &&
	    next->rndr_normal_text.text.data[0] != ' ') {
		buf = &next->rndr_normal_text.text;
		if (usepdf)
			HBUF_PUTSL(ob, "-A \"");
		for (pos = 0; pos < buf->size; pos++) {
			if (isspace((unsigned char)buf->data[pos]))
				break;
			/* Be sure to escape... */
			if (buf->data[pos] == '"') {
				HBUF_PUTSL(ob, "\\(dq");
				continue;
			} else if (buf->data[pos] == '\\') {
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
			if (!isprint((unsigned char)link->data[i]) ||
			    strchr("<>\\^`{|}\"", link->data[i]) != NULL)
				hbuf_printf(ob, "%%%.2X", link->data[i]);
			else
				hbuf_putc(ob, link->data[i]);
		}
		HBUF_PUTSL(ob, " ");
		if (text == NULL) {
			if (hbuf_prefix(link, "mailto:") == 0)
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

	if (link->size == 0)
		return 1;

	if (!nln)
		if (prev == NULL  ||
		    (ob->size && ob->data[ob->size - 1] != '\n'))
			HBUF_PUTSL(ob, "\n");

	return putlink(ob, st, link, NULL, next, prev, type);
}

static void
rndr_blockcode(hbuf *ob, const hbuf *content, 
	const hbuf *lang, const struct nstate *st)
{

	if (content->size == 0)
		return;

	if (st->mdoc) {
		HBUF_PUTSL(ob, ".sp 1\n");
		HBUF_PUTSL(ob, ".nf\n");
	} else
		HBUF_PUTSL(ob, ".LD\n");

	HBUF_PUTSL(ob, ".ft CR\n");
	rndr_block(ob, content->data, content->size);
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

	if (content->size == 0)
		return;

	HBUF_PUTSL(ob, ".RS\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_NEWLINE(content, ob);
	HBUF_PUTSL(ob, ".RE\n");
}

static void
rndr_codespan(hbuf *ob, const hbuf *content)
{

	rndr_span(ob, content->data, content->size);
}

/*
 * FIXME: not supported.
 */
static void
rndr_strikethrough(hbuf *ob, const hbuf *content)
{

	hbuf_putb(ob, content);
}

static void
rndr_linebreak(hbuf *ob)
{

	/* FIXME: should this always have a newline? */

	HBUF_PUTSL(ob, "\n.br\n");
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

	if (content->size == 0)
		return;

	if (st->mdoc) {
		if (level == 1)
			HBUF_PUTSL(ob, ".SH ");
		else 
			HBUF_PUTSL(ob, ".SS ");
		rndr_one_line_span(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\n");
		return;
	} 

	if ((st->flags & LOWDOWN_NROFF_NUMBERED))
		hbuf_printf(ob, ".NH %d\n", level);
	else if ((st->flags & LOWDOWN_NROFF_GROFF))
		hbuf_printf(ob, ".SH %d\n", level);
	else
		hbuf_printf(ob, ".SH\n");

	if ((st->flags & LOWDOWN_NROFF_NUMBERED) &&
	    (st->flags & LOWDOWN_NROFF_GROFF)) {
		HBUF_PUTSL(ob, ".XN ");
		rndr_one_line_span(ob, content->data, content->size);
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

	if (content->size == 0 && link->size == 0)
		return 1;

	if (!nln)
		if (prev == NULL ||
		    (ob->size && ob->data[ob->size - 1] != '\n'))
			HBUF_PUTSL(ob, "\n");

	return putlink(ob, st, link, 
		content, next, prev, HALINK_NORMAL);
}

static void
rndr_listitem(hbuf *ob, const hbuf *content, 
	const struct lowdown_node *prev,
	enum hlist_fl flags, size_t num)
{
	char	*cdata;
	size_t	 csize;

	if (content->size == 0)
		return;

	/* 
	 * If we're in a "block" list item or are starting the list,
	 * start vertical spacing.
	 * Then put us into an indented paragraph.
	 */

	if (prev == NULL || 
	    (content->size > 3 &&
	     memcmp(content->data, ".LP\n", 4) == 0))
		HBUF_PUTSL(ob, ".sp 1.0v\n");

	HBUF_PUTSL(ob, ".RS\n");

	/* 
	 * Now back out by the size of our list glyph(s) and print the
	 * glyph(s) (padding with two spaces).
	 */

	if ((flags & HLIST_FL_ORDERED))
		hbuf_printf(ob, ".ti -\\w'%zu.  \'u\n%zu.  ", 
			num, num);
	else
		HBUF_PUTSL(ob, ".ti -\\w'\\(bu  \'u\n\\(bu  ");

	/*
	 * Now we get shitty.
	 * If we have macros on the content, we need to handle them in a
	 * special way.
	 * Paragraphs (.LP) can be stripped out.
	 * Links need a newline.
	 */

	cdata = content->data;
	csize = content->size;

	/* Start by stripping out all paragraphs. */

	while (csize > 3 && memcmp(cdata, ".LP\n", 4) == 0) {
		cdata += 4;
		csize -= 4;
	}

	/* Now make sure we have a newline before links. */

	if (csize > 8 && memcmp(cdata, ".pdfhref ", 9) == 0)
		HBUF_PUTSL(ob, "\n");

	hbuf_put(ob, cdata, csize);
	BUFFER_NEWLINE(cdata, csize, ob);
	HBUF_PUTSL(ob, ".RE\n");
}

static void
rndr_paragraph(hbuf *ob, const hbuf *content, 
	const struct nstate *st, const struct lowdown_node *np)
{
	size_t	 i = 0, org;

	if (content->size == 0)
		return;

	/* Strip away initial white-space. */

	while (i < content->size && 
	       isspace((unsigned char)content->data[i]))
		i++;
	if (i == content->size)
		return;

	HBUF_PUTSL(ob, ".LP\n");

	if ((st->flags & LOWDOWN_NROFF_HARD_WRAP)) {
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
	size_t	 org, sz;

	if (content->size == 0)
		return;

	if ((st->flags & LOWDOWN_NROFF_SKIP_HTML)) {
		rndr_block(ob, content->data, content->size);
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
	if (!st->mdoc)
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

	if ((cp = memrchr(link->data, '.', link->size)) == NULL) {
		warnx("warning: no image suffix (ignoring)");
		return;
	}

	cp++;
	sz = link->size - (cp - link->data);

	if (sz == 0) {
		warnx("warning: empty image suffix (ignoring)");
		return;
	}

	if (!(sz == 2 && memcmp(cp, "ps", 2) == 0) &&
	    !(sz == 3 && memcmp(cp, "eps", 3) == 0)) {
		warnx("warning: unknown image suffix (ignoring)");
		return;
	}

	if (!nln)
		if (prev == NULL ||
		    (ob->size && ob->data[ob->size - 1] != '\n'))
			HBUF_PUTSL(ob, "\n");

	hbuf_printf(ob, ".PSPIC %.*s\n", 
		(int)link->size, link->data);
}

static void
rndr_raw_html(hbuf *ob, const hbuf *text, const struct nstate *st)
{

	if ((st->flags & LOWDOWN_NROFF_SKIP_HTML))
		return;
	rndr_block(ob, text->data, text->size);
}

static void
rndr_table(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, ".TS\n");
	HBUF_PUTSL(ob, "tab(|) expand allbox;\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_NEWLINE(content, ob);
	HBUF_PUTSL(ob, ".TE\n");
}

static void
rndr_table_header(hbuf *ob, const hbuf *content,
	const enum htbl_flags *fl, size_t columns)
{
	size_t	 i;

	/* 
	 * This specifies the header layout.
	 * We make the header bold, but this is arbitrary.
	 */

	for (i = 0; i < columns; i++) {
		if (i > 0)
			HBUF_PUTSL(ob, " ");
		switch (fl[i] & HTBL_FL_ALIGNMASK) {
		case HTBL_FL_ALIGN_CENTER:
			HBUF_PUTSL(ob, "cb");
			break;
		case HTBL_FL_ALIGN_RIGHT:
			HBUF_PUTSL(ob, "rb");
			break;
		default:
			HBUF_PUTSL(ob, "lb");
			break;
		}
	}
	HBUF_PUTSL(ob, "\n");

	/* Now the body layout. */

	for (i = 0; i < columns; i++) {
		if (i > 0)
			HBUF_PUTSL(ob, " ");
		switch (fl[i] & HTBL_FL_ALIGNMASK) {
		case HTBL_FL_ALIGN_CENTER:
			HBUF_PUTSL(ob, "c");
			break;
		case HTBL_FL_ALIGN_RIGHT:
			HBUF_PUTSL(ob, "r");
			break;
		default:
			HBUF_PUTSL(ob, "l");
			break;
		}
	}
	HBUF_PUTSL(ob, ".\n");

	/* Now the table data. */

	hbuf_putb(ob, content);
}

static void
rndr_table_body(hbuf *ob, const hbuf *content)
{

	hbuf_putb(ob, content);
}

static void
rndr_tablerow(hbuf *ob, const hbuf *content)
{

	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "\n");
}

static void
rndr_tablecell(hbuf *ob, const hbuf *content, size_t col)
{

	if (col > 0)
		HBUF_PUTSL(ob, "|");
	if (content->size) {
		HBUF_PUTSL(ob, "T{\n");
		hbuf_putb(ob, content);
		HBUF_PUTSL(ob, "\nT}");
	}
}

static void
rndr_superscript(hbuf *ob, const hbuf *content)
{

	if (content->size == 0)
		return;

	/*
	 * If we have a macro contents, it might be the usual macro
	 * (solo in its buffer) or starting with a newline.
	 */

	if (content->data[0] == '.' ||
	    (content->size &&
	     content->data[0] == '\n' &&
	     content->data[1] == '.')) {
		HBUF_PUTSL(ob, "\\u\\s-3");
		if (content->data[0] != '\n')
			HBUF_PUTSL(ob, "\n");
		hbuf_putb(ob, content);
		HBUF_NEWLINE(content, ob);
		HBUF_PUTSL(ob, "\\s+3\\d\n");
	} else {
		HBUF_PUTSL(ob, "\\u\\s-3");
		hbuf_putb(ob, content);
		HBUF_PUTSL(ob, "\\s+3\\d");
	}
}

static void
rndr_normal_text(hbuf *ob, const hbuf *content, size_t offs, 
	const struct lowdown_node *prev, 
	const struct lowdown_node *next, 
	const struct nstate *st, int nl)
{
	size_t	 	 i, size;
	const char 	*data;

	if (content->size == 0)
		return;

	data = content->data + offs;
	size = content->size - offs;

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
		rndr_block(ob, data + i, size - i);
	} else
		rndr_span(ob, data, size);
}

static void
rndr_footnotes(hbuf *ob, const hbuf *content, const struct nstate *st)
{

	/* Put a horizontal line in the case of man(7). */

	if (content->size && st->mdoc) {
		HBUF_PUTSL(ob, ".LP\n");
		HBUF_PUTSL(ob, ".sp 3\n");
		HBUF_PUTSL(ob, "\\l\'2i'\n");
	}

	hbuf_putb(ob, content);
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

	if (!st->mdoc) {
		HBUF_PUTSL(ob, ".FS\n");
		/* Ignore leading paragraph marker. */
		if (content->size > 3 &&
		    memcmp(content->data, ".LP\n", 4) == 0)
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
	    memcmp(content->data, ".LP\n", 4) == 0)
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

	if (!st->mdoc)
		HBUF_PUTSL(ob, "\\**");
	else
		hbuf_printf(ob, "\\u\\s-3%u\\s+3\\d", num);
}

static void
rndr_math(void)
{

	/* FIXME: use lowdown_opts warnings. */
	warnx("warning: math not supported");
}

/*
 * Split "b" at sequential white-space, outputting the results in the
 * line-based "env" macro.
 * The content in "b" has already been escaped, so there's no need to do
 * anything but manage white-space.
 */
static void
rndr_meta_multi(hbuf *ob, const hbuf *b, const char *env)
{
	const char	*start;
	size_t		 sz, i;

	for (i = 0; i < b->size; i++) {
		while (i < b->size &&
		       isspace((unsigned char)b->data[i]))
			i++;
		if (i == b->size)
			continue;
		start = &b->data[i];

		for (; i < b->size; i++)
			if (i < b->size - 1 &&
			    isspace((unsigned char)b->data[i]) &&
			    isspace((unsigned char)b->data[i + 1]))
				break;
		if ((sz = &b->data[i] - start) == 0)
			continue;
		hbuf_printf(ob, ".%s\n", env);
		rndr_one_line_noescape(ob, start, sz, 1);
		HBUF_PUTSL(ob, "\n");
	}
}

static void
rndr_meta(hbuf *ob, const hbuf *tmp, struct lowdown_metaq *mq,
	const struct lowdown_node *n, const struct nstate *st)
{
	enum rndr_meta_key	 	 key;
	const struct lowdown_node	*copy;
	struct lowdown_meta		*m;

	if (mq != NULL) {
		m = xcalloc(1, sizeof(struct lowdown_meta));
		TAILQ_INSERT_TAIL(mq, m, entries);
		m->key = malloc(n->rndr_meta.key.size);
		memcpy(m->key, n->rndr_meta.key.data, 
			n->rndr_meta.key.size);
		m->value = xmalloc(tmp->size);
		memcpy(m->value, tmp->data, tmp->size);
	}

	if (!(st->flags & LOWDOWN_STANDALONE))
		return;

	for (key = 0; key < RNDR_META__MAX; key++)
		if (hbuf_streq(&n->rndr_meta.key, rndr_meta_keys[key]))
			break;

	/* 
	 * If we're printing the date and have a copyright, we'll also
	 * set the copyright date.
	 */

	TAILQ_FOREACH(copy, &n->parent->children, entries) {
		if (copy->type != LOWDOWN_META)
			continue;
		if (hbuf_streq(&copy->rndr_meta.key, "copyright"))
			break;
	}

	/* FIXME: AU must happen after TL. */

	if (!st->mdoc) {
		switch (key) {
		case RNDR_META_COPY:
			HBUF_PUTSL(ob, ".ds LF \\s-2Copyright \\(co ");
			rndr_one_lineb_noescape(ob, tmp, 0);
			HBUF_PUTSL(ob, "\\s+2\n");
			break;
		case RNDR_META_DATE:
			if (copy != NULL) {
				HBUF_PUTSL(ob, ".ds RF \\s-2");
				rndr_one_lineb_noescape(ob, tmp, 0);
				HBUF_PUTSL(ob, "\\s+2\n");
			}
			HBUF_PUTSL(ob, ".DA \\s-2");
			rndr_one_lineb_noescape(ob, tmp, 0);
			HBUF_PUTSL(ob, "\\s+2\n");
			break;
		case RNDR_META_TITLE:
			HBUF_PUTSL(ob, ".TL\n");
			rndr_one_lineb_noescape(ob, tmp, 1);
			HBUF_PUTSL(ob, "\n");
			break;
		case RNDR_META_AUTHOR:
			rndr_meta_multi(ob, tmp, "AU");
			break;
		case RNDR_META_AFFIL:
			rndr_meta_multi(ob, tmp, "AI");
			break;
		default:
			break;
		}
	} else {
		switch (key) {
		case RNDR_META_TITLE:
			HBUF_PUTSL(ob, ".TH \"");
			rndr_one_lineb_noescape(ob, tmp, 0);
			HBUF_PUTSL(ob, "\" 7\n");
			/* FIXME: print date. */
			/*hbuf_printf(ob, "\" 7 %s\n", date);*/
			break;
		default:
			break;
		}
	}
}

static void
rndr_doc_header(hbuf *ob, const hbuf *tmp,
	const struct lowdown_node *n, const struct nstate *st)
{
	struct lowdown_node	*title;

	if (!(st->flags & LOWDOWN_STANDALONE))
		return;

	TAILQ_FOREACH(title, &n->children, entries) {
		if (title->type != LOWDOWN_META)
			continue;
		if (hbuf_streq(&title->rndr_meta.key, "title"))
			break;
	}

	if (title == NULL && !st->mdoc)
		HBUF_PUTSL(ob, ".TL\nUntitled Article\n");
	else if (title == NULL)
		HBUF_PUTSL(ob, ".TH \"Untitled Article\" 7\n");

	hbuf_putb(ob, tmp);
}

/*
 * Actually render the node "root" and all of its children into the
 * output buffer "ob".
 * Return whether we should remove nodes relative to "root".
 */
static void
rndr(hbuf *ob, struct lowdown_metaq *metaq, 
	struct nstate *ref, struct lowdown_node *root)
{
	struct lowdown_node *n, *next, *prev;
	hbuf		*tmp;
	int		 pnln, keepnext = 1;
	int32_t		 ent;
	enum nfont	 fonts[NFONT__MAX];

	assert(root != NULL);

	tmp = hbuf_new(64);

	memcpy(fonts, ref->fonts, sizeof(fonts));

	/*
	 * Font management.
	 * roff doesn't handle its own font stack, so we can't set fonts
	 * and step out of them in a nested way.
	 */

	switch (root->type) {
	case LOWDOWN_CODESPAN:
		ref->fonts[NFONT_FIXED]++;
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	case LOWDOWN_EMPHASIS:
		ref->fonts[NFONT_ITALIC]++;
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
		ref->fonts[NFONT_BOLD]++;
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	case LOWDOWN_TRIPLE_EMPHASIS:
		ref->fonts[NFONT_ITALIC]++;
		ref->fonts[NFONT_BOLD]++;
		hbuf_puts(ob, nstate_fonts(ref));
		break;
	default:
		break;
	}

	TAILQ_FOREACH(n, &root->children, entries)
		rndr(tmp, metaq, ref, n);

	/* 
	 * Compute whether the previous output does have a newline:
	 * if we don't have a previous node, scan up to the parent and
	 * see if it's a block node.
	 * If it's a block node, we're absolutely starting on a newline;
	 * otherwise, we don't know.
	 */

	if (root->parent == NULL ||
	    (n = TAILQ_PREV(root, lowdown_nodeq, entries)) == NULL)
		n = root->parent;
	pnln = n == NULL || nscopes[n->type] == NSCOPE_BLOCK;

	/* Get the last and next emitted node. */

	prev = root->parent == NULL ? NULL : 
		TAILQ_PREV(root, lowdown_nodeq, entries);
	next = TAILQ_NEXT(root, entries);

	if (nscopes[root->type] == NSCOPE_BLOCK) {
		if (root->chng == LOWDOWN_CHNG_INSERT)
			HBUF_PUTSL(ob, ".gcolor blue\n");
		else if (root->chng == LOWDOWN_CHNG_DELETE)
			HBUF_PUTSL(ob, ".gcolor red\n");
	} else {
		/*
		 * FIXME: this is going to disrupt our newline
		 * computation.
		 */
		if (root->chng == LOWDOWN_CHNG_INSERT)
			HBUF_PUTSL(ob, "\\m[blue]");
		else if (root->chng == LOWDOWN_CHNG_DELETE)
			HBUF_PUTSL(ob, "\\m[red]");
	}

	switch (root->type) {
	case LOWDOWN_BLOCKCODE:
		rndr_blockcode(ob, 
			&root->rndr_blockcode.text, 
			&root->rndr_blockcode.lang, ref);
		break;
	case LOWDOWN_BLOCKQUOTE:
		rndr_blockquote(ob, tmp);
		break;
	case LOWDOWN_DOC_HEADER:
		rndr_doc_header(ob, tmp, root, ref);
		break;
	case LOWDOWN_META:
		rndr_meta(ob, tmp, metaq, root, ref);
		break;
	case LOWDOWN_HEADER:
		rndr_header(ob, tmp, 
			root->rndr_header.level, ref);
		break;
	case LOWDOWN_HRULE:
		rndr_hrule(ob, ref);
		break;
	case LOWDOWN_LISTITEM:
		rndr_listitem(ob, tmp, prev,
			root->rndr_listitem.flags,
			root->rndr_listitem.num);
		break;
	case LOWDOWN_PARAGRAPH:
		rndr_paragraph(ob, tmp, ref, root->parent);
		break;
	case LOWDOWN_TABLE_BLOCK:
		rndr_table(ob, tmp);
		break;
	case LOWDOWN_TABLE_HEADER:
		rndr_table_header(ob, tmp, 
			root->rndr_table_header.flags,
			root->rndr_table_header.columns);
		break;
	case LOWDOWN_TABLE_BODY:
		rndr_table_body(ob, tmp);
		break;
	case LOWDOWN_TABLE_ROW:
		rndr_tablerow(ob, tmp);
		break;
	case LOWDOWN_TABLE_CELL:
		rndr_tablecell(ob, tmp, 
			root->rndr_table_cell.col);
		break;
	case LOWDOWN_FOOTNOTES_BLOCK:
		rndr_footnotes(ob, tmp, ref);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rndr_footnote_def(ob, tmp, 
			root->rndr_footnote_def.num, ref);
		break;
	case LOWDOWN_BLOCKHTML:
		rndr_raw_block(ob, tmp, ref);
		break;
	case LOWDOWN_LINK_AUTO:
		keepnext = rndr_autolink(ob, 
			&root->rndr_autolink.link,
			root->rndr_autolink.type,
			prev, next, ref, pnln);
		break;
	case LOWDOWN_CODESPAN:
		rndr_codespan(ob, &root->rndr_codespan.text);
		break;
	case LOWDOWN_IMAGE:
		rndr_image(ob, &root->rndr_image.link,
			ref, pnln, prev);
		break;
	case LOWDOWN_LINEBREAK:
		rndr_linebreak(ob);
		break;
	case LOWDOWN_LINK:
		keepnext = rndr_link(ob, tmp,
			&root->rndr_link.link,
			ref, prev, next, pnln);
		break;
	case LOWDOWN_STRIKETHROUGH:
		rndr_strikethrough(ob, tmp);
		break;
	case LOWDOWN_SUPERSCRIPT:
		rndr_superscript(ob, tmp);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		rndr_footnote_ref(ob, 
			root->rndr_footnote_ref.num, ref);
		break;
	case LOWDOWN_MATH_BLOCK:
		rndr_math();
		break;
	case LOWDOWN_RAW_HTML:
		rndr_raw_html(ob, &root->rndr_raw_html.text, ref);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rndr_normal_text(ob, 
			&root->rndr_normal_text.text, 
			root->rndr_normal_text.offs,
			prev, next, ref, pnln);
		break;
	case LOWDOWN_ENTITY:
		ent = entity_find(&root->rndr_entity.text);
		if (ent > 0)
			hbuf_printf(ob, "\\[u%.4llX]", 
				(unsigned long long)ent);
		else
			hbuf_put(ob,
				root->rndr_entity.text.data,
				root->rndr_entity.text.size);
		break;
	default:
		hbuf_put(ob, tmp->data, tmp->size);
		break;
	}


	if (root->chng == LOWDOWN_CHNG_INSERT ||
	    root->chng == LOWDOWN_CHNG_DELETE) {
		if (nscopes[root->type] == NSCOPE_BLOCK)
			HBUF_PUTSL(ob, ".gcolor\n");
		else
			HBUF_PUTSL(ob, "\\m[]");
	}

	/* Restore the font stack. */

	switch (root->type) {
	case LOWDOWN_CODESPAN:
	case LOWDOWN_EMPHASIS:
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_DOUBLE_EMPHASIS:
	case LOWDOWN_TRIPLE_EMPHASIS:
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

	if (!keepnext) {
		assert(next->parent != NULL);
		TAILQ_REMOVE(&next->parent->children, next, entries);
		lowdown_node_free(next);
	}
}

void
lowdown_nroff_rndr(hbuf *ob, struct lowdown_metaq *metaq,
	void *ref, struct lowdown_node *root)
{
	struct nstate	*st = ref;

	memset(st->fonts, 0, sizeof(st->fonts));
	rndr(ob, metaq, ref, root);
}

void *
lowdown_nroff_new(const struct lowdown_opts *opts)
{
	struct nstate 	*state;

	state = xcalloc(1, sizeof(struct nstate));
	state->flags = opts != NULL ? opts->oflags : 0;
	state->mdoc = opts != NULL && opts->type == LOWDOWN_MAN;
	return state;
}

void
lowdown_nroff_free(void *data)
{

	free(data);
}
