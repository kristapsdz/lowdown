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

#define	BUFFER_NEWLINE(_buf, _sz, _ob) \
	do if ((_sz) > 0 && '\n' != (_buf)[(_sz) - 1]) \
		hbuf_putc((_ob), '\n'); \
	while (/* CONSTCOND */ 0)

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
	NSCOPE_SPAN, /* LOWDOWN_IMAGE */
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
escape_span(hbuf *ob, const uint8_t *source, size_t length)
{

	hesc_nroff(ob, source, length, 1, 0);
}

static void
escape_block(hbuf *ob, const uint8_t *source, size_t length)
{

	hesc_nroff(ob, source, length, 0, 0);
}

static void
escape_oneline_span(hbuf *ob, const uint8_t *source, size_t length)
{

	hesc_nroff(ob, source, length, 1, 1);
}

/*
 * Manage hypertext linking with the groff "pdfhref" macro.
 */
static int
putlink(hbuf *ob, const hbuf *link, const hbuf *text, 
	struct lowdown_node *next, struct lowdown_node *prev)
{
	const hbuf	*buf;
	size_t		 i, pos;
	int		 ret = 1;

	HBUF_PUTSL(ob, ".pdfhref W ");

	/*
	 * If we're followed by normal text that doesn't begin with a
	 * space, use the "-A" (affix) option to prevent a space before
	 * what follows.
	 */

	if (NULL != next && 
	    LOWDOWN_NORMAL_TEXT == next->type &&
	    next->rndr_normal_text.text.size > 0 &&
	    ' ' != next->rndr_normal_text.text.data[0]) {
		buf = &next->rndr_normal_text.text;
		HBUF_PUTSL(ob, "-A \"");
		for (pos = 0; pos < buf->size; pos++) {
			if (isspace((int)buf->data[pos]))
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
		HBUF_PUTSL(ob, "\" ");
	}

	/*
	 * If we're preceded by normal text that doesn't end with space,
	 * then put that text into the "-P" (prefix) argument.
	 */

	if (NULL != prev &&
	    LOWDOWN_NORMAL_TEXT == prev->type) {
		buf = &prev->rndr_normal_text.text;
		i = buf->size;
		while (i && ! isspace((int)buf->data[i - 1]))
			i--;
		if (i != buf->size) 
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
		if (i != buf->size) 
			HBUF_PUTSL(ob, "\" ");
	}

	/* Encode the URL. */

	HBUF_PUTSL(ob, "-D ");
	for (i = 0; i < link->size; i++) {
		if ( ! isprint((int)link->data[i]) ||
		    NULL != strchr("<>\\^`{|}\"", link->data[i]))
			hbuf_printf(ob, "%%%.2X", link->data[i]);
		else
			hbuf_putc(ob, link->data[i]);
	}
	HBUF_PUTSL(ob, " ");
	if (NULL == text)
		hbuf_put(ob, link->data, link->size);
	else
		hbuf_put(ob, text->data, text->size);

	HBUF_PUTSL(ob, "\n");
	return ret;
}

static int
rndr_autolink(hbuf *ob, const hbuf *link, halink_type type, 
	struct lowdown_node *prev, struct lowdown_node *next,
	const struct nstate *st, int nln)
{

	if (NULL == link || 0 == link->size)
		return 1;

	/*
	 * If we're not using groff extensions, just italicise.
	 * Otherwise, use UR/UE in -man mode and pdfhref in -ms.
	 */

	if ( ! nln)
		HBUF_PUTSL(ob, "\n");

	if ( ! (st->flags & LOWDOWN_NROFF_GROFF)) {
		HBUF_PUTSL(ob, ".I\n");
		if (hbuf_prefix(link, "mailto:") == 0)
			escape_oneline_span(ob, 
				link->data + 7, link->size - 7);
		else
			escape_oneline_span(ob, 
				link->data, link->size);
		HBUF_PUTSL(ob, "\n.R\n");
		return 1;
	} else if (st->mdoc) {
		HBUF_PUTSL(ob, ".UR ");
		hbuf_put(ob, link->data, link->size);
		HBUF_PUTSL(ob, "\n.UE\n");
		return 1;
	}

	return putlink(ob, link, NULL, next, prev);
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
		HBUF_PUTSL(ob, ".DS\n");

	HBUF_PUTSL(ob, ".ft CR\n");
	escape_block(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
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

	HBUF_PUTSL(ob, ".B1\n");
	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".B2\n");
}

static int
rndr_codespan(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\f[CR]");
	escape_span(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fR");
	return 1;
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
rndr_double_emphasis(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\fB");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fP");

	return 1;
}

static int
rndr_triple_emphasis(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\f[BI]");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fP");

	return 1;
}


static int
rndr_emphasis(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\fI");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fP");

	return 1;
}

static int
rndr_highlight(hbuf *ob, const hbuf *content)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\fB");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fP");

	return 1;
}

static int
rndr_linebreak(hbuf *ob)
{

	/* FIXME: should this always have a newline? */

	HBUF_PUTSL(ob, "\n.br\n");
	return 1;
}

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
	} else {
		if (st->flags & LOWDOWN_NROFF_NUMBERED) 
			hbuf_printf(ob, ".NH %d\n", level);
		else if (st->flags & LOWDOWN_NROFF_GROFF) 
			hbuf_printf(ob, ".SH %d\n", level);
		else
			hbuf_printf(ob, ".SH\n");
	}

	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
}

static int
rndr_link(hbuf *ob, const hbuf *content, const hbuf *link, 
	const hbuf *title, const struct nstate *st, 
	struct lowdown_node *prev, struct lowdown_node *next, int nln)
{

	if ((NULL == content || 0 == content->size) &&
	    (NULL == title || 0 == title->size) &&
	    (NULL == link || 0 == link->size))
		return 1;

	if ( ! nln)
		HBUF_PUTSL(ob, "\n");

	if ( ! (st->flags & LOWDOWN_NROFF_GROFF)) {
		HBUF_PUTSL(ob, ".I\n");
		if (NULL != content && content->size)
			hbuf_put(ob, content->data, content->size);
		else if (NULL != title && title->size)
			escape_block(ob, title->data, title->size);
		else if (NULL != link && link->size)
			escape_block(ob, link->data, link->size);
		if (ob->size && '\n' != ob->data[ob->size - 1])
			HBUF_PUTSL(ob, "\n");
		if ( ! st->mdoc)
			HBUF_PUTSL(ob, ".R\n");
		return 1;
	} else if (st->mdoc) {
		HBUF_PUTSL(ob, ".UR ");
		if (NULL != link && link->size)
			escape_oneline_span(ob, 
				link->data, link->size);
		HBUF_PUTSL(ob, "\n");
		if (NULL != content && content->size)
			hbuf_put(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\n.UE\n");
		return 1;
	}

	return putlink(ob, link, content, next, prev);
}

static void
rndr_list(hbuf *ob, const hbuf *content, hlist_fl flags)
{

	HBUF_PUTSL(ob, ".RS\n");
	if (NULL != content && content->size)
		hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, ".RE\n");
}

static void
rndr_listitem(hbuf *ob, const hbuf *content, 
	hlist_fl flags, size_t num)
{

	if (NULL == content || 0 == content->size)
		return;

	if (HLIST_FL_ORDERED & flags)
		hbuf_printf(ob, ".IP %zu.\n", num);
	else
		HBUF_PUTSL(ob, ".IP \\(bu\n");

	/* 
	 * Don't have a superfluous `LP' following the IP.
	 * This would create useless whitespace following the number of
	 * bullet.
	 */

	if (content->size > 4 && 
	    0 == strncmp((const char *)content->data, ".LP\n", 4))
		hbuf_put(ob, content->data + 4, content->size - 4);
	else
		hbuf_put(ob, content->data, content->size);

	BUFFER_NEWLINE(content->data, content->size, ob);
}

static void
rndr_paragraph(hbuf *ob, const hbuf *content, const struct nstate *st)
{
	size_t	 	 i = 0, org;

	if (NULL == content || 0 == content->size)
		return;
	while (i < content->size && isspace((int)content->data[i]))
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

static int
rndr_image(void)
{

	warnx("warning: graphics not supported");
	return 1;
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
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".TE\n");
}

static void
rndr_table_header(hbuf *ob, const hbuf *content,
	const htbl_flags *fl, size_t columns)
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
	htbl_flags flags, size_t col, size_t columns)
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
	const uint8_t 	*data;

	if (NULL == content || 0 == content->size)
		return;

	data = content->data + offs;
	size = content->size - offs;

	/* 
	 * If we have a link next, and we have a trailing newline, don't
	 * print the newline.
	 * Furthermore, if we don't have trailing space, omit the final
	 * word because we'll put that in the link's pdfhref.
	 * This is because the link will emit a newline based upon the
	 * previous type, *not* looking at whether there's a newline.
	 * We could do this there, but whatever.
	 */

	if (NULL != next && ! st->mdoc &&
	    (st->flags & LOWDOWN_NROFF_GROFF) &&
	    (LOWDOWN_LINK_AUTO == next->type ||
	     LOWDOWN_LINK == next->type)) {
		if ('\n' == data[size - 1]) {
			if (0 == --size)
				return;
		} else if ( ! isspace((int)data[size - 1])) {
			while (size && ! isspace((int)data[size - 1]))
				size--;
			if (0 == size)
				return;
		}
	}

	if (nl) {
		for (i = 0; i < size; i++)
			if ( ! isspace((int)data[i]))
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

	/* The LP is to reset the margins. */

	HBUF_PUTSL(ob, ".LP\n");
	if ( ! st->mdoc) {
		HBUF_PUTSL(ob, ".sp 2\n");
		HBUF_PUTSL(ob, "\\l\'\\n(.lu-\\n(\\n[.in]u\'\n");
	}
	hbuf_put(ob, content->data, content->size);
}

static void
rndr_footnote_def(hbuf *ob, const hbuf *content, unsigned int num)
{

	HBUF_PUTSL(ob, ".LP\n");
	hbuf_printf(ob, "\\fI%u.\\fP\n", num);
	HBUF_PUTSL(ob, ".RS\n");
	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".RE\n");
}

static int
rndr_footnote_ref(hbuf *ob, unsigned int num)
{

	hbuf_printf(ob, "\\u\\s-3%u\\s+3\\d", num);
	return 1;
}

static int
rndr_math(void)
{

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

static void
rndr_doc_header(hbuf *ob, 
	const struct lowdown_meta *m, size_t msz, 
	const struct nstate *st)
{
	const char	*date = NULL, *author = NULL, *cp,
	      		*title = "Untitled article", *start;
	time_t		 t;
	char		 buf[32];
	struct tm	*tm;
	size_t		 i, sz;

	if ( ! (LOWDOWN_STANDALONE & st->flags))
		return;

	/* Acquire metadata that we'll fill in. */

	for (i = 0; i < msz; i++) 
		if (0 == strcmp(m[i].key, "title"))
			title = m[i].value;
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

	while (isspace((int)*title))
		title++;

	/* Emit our authors and title. */

	if ( ! st->mdoc) {
		HBUF_PUTSL(ob, ".nr PS 10\n");
		HBUF_PUTSL(ob, ".nr GROWPS 3\n");
		hbuf_printf(ob, ".DA %s\n.TL\n", date);
		escape_block(ob, 
			(const uint8_t *)title, strlen(title));
		HBUF_PUTSL(ob, "\n");
		if (NULL != author)
			for (cp = author; '\0' != *cp; ) {
				while (isspace((int)*cp))
					cp++;
				if ('\0' == *cp)
					continue;
				start = cp;
				sz = 0;
				while ('\0' != *cp) {
					if ( ! isspace((int)cp[0]) ||
					     ! isspace((int)cp[1])) {
						sz++;
						cp++;
						continue;
					}
					cp += 2;
					break;
				}
				if (0 == sz)
					continue;
				HBUF_PUTSL(ob, ".AU\n");
				hesc_nroff(ob, 
					(const uint8_t *)start, sz, 0, 1);
				HBUF_PUTSL(ob, "\n");
			}
	} else {
		HBUF_PUTSL(ob, ".TH \"");
		escape_oneline_span(ob, 
			(const uint8_t *)title, strlen(title));
		hbuf_printf(ob, "\" 7 %s\n", date);
	}
}

static void
rndr(hbuf *ob, const struct nstate *ref, 
	struct lowdown_node *root)
{
	struct lowdown_node *n, *next, *prev;
	hbuf	*tmp;
	int	 pnln, keep;

	assert(NULL != root);

	tmp = hbuf_new(64);

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
	keep = 1;

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
	case (LOWDOWN_LIST):
		rndr_list(ob, tmp, root->rndr_list.flags);
		break;
	case (LOWDOWN_LISTITEM):
		rndr_listitem(ob, tmp, 
			root->rndr_listitem.flags,
			root->rndr_listitem.num);
		break;
	case (LOWDOWN_PARAGRAPH):
		rndr_paragraph(ob, tmp, ref);
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
			root->rndr_footnote_def.num);
		break;
	case (LOWDOWN_BLOCKHTML):
		rndr_raw_block(ob, tmp, ref);
		break;
	case (LOWDOWN_LINK_AUTO):
		keep = rndr_autolink(ob, 
			&root->rndr_autolink.link,
			root->rndr_autolink.type,
			prev, next, ref, pnln);
		break;
	case (LOWDOWN_CODESPAN):
		rndr_codespan(ob, &root->rndr_codespan.text);
		break;
	case (LOWDOWN_DOUBLE_EMPHASIS):
		rndr_double_emphasis(ob, tmp);
		break;
	case (LOWDOWN_EMPHASIS):
		rndr_emphasis(ob, tmp);
		break;
	case (LOWDOWN_HIGHLIGHT):
		rndr_highlight(ob, tmp);
		break;
	case (LOWDOWN_IMAGE):
		rndr_image();
		break;
	case (LOWDOWN_LINEBREAK):
		rndr_linebreak(ob);
		break;
	case (LOWDOWN_LINK):
		keep = rndr_link(ob, tmp,
			&root->rndr_link.link,
			&root->rndr_link.title,
			ref, prev, next, pnln);
		break;
	case (LOWDOWN_TRIPLE_EMPHASIS):
		rndr_triple_emphasis(ob, tmp);
		break;
	case (LOWDOWN_STRIKETHROUGH):
		rndr_strikethrough(ob, tmp);
		break;
	case (LOWDOWN_SUPERSCRIPT):
		rndr_superscript(ob, tmp);
		break;
	case (LOWDOWN_FOOTNOTE_REF):
		rndr_footnote_ref(ob, root->rndr_footnote_ref.num);
		break;
	case (LOWDOWN_MATH_BLOCK):
		rndr_math();
		break;
	case (LOWDOWN_RAW_HTML):
		rndr_raw_html(ob, tmp, ref);
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

	hbuf_free(tmp);

	if ( ! keep) {
		assert(NULL != root->parent);
		TAILQ_REMOVE(&next->parent->children, next, entries);
		lowdown_node_free(next);
	}
}

void
lowdown_nroff_rndr(hbuf *ob, void *ref, struct lowdown_node *root)
{

	rndr(ob, ref, root);
}

void *
hrend_nroff_new(const struct lowdown_opts *opts)
{
	struct nstate 	*state;

	/* Prepare the state pointer. */

	state = xcalloc(1, sizeof(struct nstate));

	state->flags = NULL != opts ? opts->oflags : 0;
	state->mdoc = NULL != opts && LOWDOWN_MAN == opts->type;

	return state;
}

void
hrend_nroff_free(void *data)
{

	free(data);
}
