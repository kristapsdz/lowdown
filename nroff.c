/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016, Kristaps Dzonsons
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

#include <ctype.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

#define	BUFFER_NEWLINE(_buf, _sz, _ob) \
	do if ((_sz) > 0 && '\n' != (_buf)[(_sz) - 1]) \
		hbuf_putc((_ob), '\n'); \
	while (/* CONSTCOND */ 0)

typedef struct nroff_state {
	int mdoc;
	struct {
		int header_count;
		int current_level;
		int level_offset;
	} toc_data;
	unsigned int flags;
} nroff_state;

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

static int
rndr_autolink(hbuf *ob, const hbuf *link, 
	halink_type type, void *data, int nln)
{
	nroff_state	*st = data;

	if (NULL == link || 0 == link->size)
		return 0;

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

	HBUF_PUTSL(ob, ".pdfhref W ");
	hbuf_put(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\n");
	return 1;
}

static void
rndr_blockcode(hbuf *ob, const hbuf *content, const hbuf *lang, void *data)
{
	nroff_state 	*st = data;

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
rndr_blockquote(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return;

	HBUF_PUTSL(ob, ".B1\n");
	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".B2\n");
}

static int
rndr_codespan(hbuf *ob, const hbuf *content, void *data, int nln)
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
rndr_strikethrough(hbuf *ob, const hbuf *content, void *data, int nln)
{

	if (NULL == content || 0 == content->size)
		return(0);

	hbuf_put(ob, content->data, content->size);
	return 1;
}

static int
rndr_double_emphasis(hbuf *ob, const hbuf *content, void *data, int nln)
{
	nroff_state *st	= data;

	if (NULL == content || 0 == content->size)
		return(0);

	if ('.' == content->data[0]) {
		if ( ! nln)
			HBUF_PUTSL(ob, "\n");
		/* FIXME: for man(7), this is next-line scope. */
		HBUF_PUTSL(ob, ".B\n");
		hbuf_put(ob, content->data, content->size);
		if (content->size && 
		    '\n' != content->data[content->size - 1])
			HBUF_PUTSL(ob, "\n");
		if ( ! st->mdoc)
			HBUF_PUTSL(ob, ".R\n");
	} else { 
		HBUF_PUTSL(ob, "\\fB");
		hbuf_put(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\\fP");
	}

	return 1;
}

static int
rndr_triple_emphasis(hbuf *ob, const hbuf *content, void *data, int nln)
{
	nroff_state	*st = data;

	if (NULL == content || 0 == content->size)
		return(0);

	if ('.' == content->data[0]) {
		if ( ! nln)
			HBUF_PUTSL(ob, "\n");
		/* FIXME: for man(7), this is next-line scope. */
		HBUF_PUTSL(ob, ".BI\n");
		hbuf_put(ob, content->data, content->size);
		if (content->size && 
		    '\n' != content->data[content->size - 1])
			HBUF_PUTSL(ob, "\n");
		if ( ! st->mdoc)
			HBUF_PUTSL(ob, ".R\n");
	} else { 
		HBUF_PUTSL(ob, "\\f[BI]");
		hbuf_put(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\\fP");
	}

	return 1;
}


static int
rndr_emphasis(hbuf *ob, const hbuf *content, void *data, int nln)
{
	nroff_state	*st = data;

	if (NULL == content || 0 == content->size)
		return(0);

	if ('.' == content->data[0]) {
		if ( ! nln)
			HBUF_PUTSL(ob, "\n");
		/* FIXME: for man(7), this is next-line scope. */
		HBUF_PUTSL(ob, ".I\n");
		hbuf_put(ob, content->data, content->size);
		if (content->size && 
		    '\n' != content->data[content->size - 1])
			HBUF_PUTSL(ob, "\n");
		if ( ! st->mdoc)
			HBUF_PUTSL(ob, ".R\n");
	} else {
		HBUF_PUTSL(ob, "\\fI");
		hbuf_put(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\\fP");
	}

	return 1;
}

static int
rndr_highlight(hbuf *ob, const hbuf *content, void *data, int nln)
{
	nroff_state	*st = data;

	if (NULL == content || 0 == content->size)
		return(0);

	if ('.' == content->data[0]) {
		if ( ! nln)
			HBUF_PUTSL(ob, "\n");
		/* FIXME: for man(7), this is next-line scope. */
		HBUF_PUTSL(ob, ".B\n");
		hbuf_put(ob, content->data, content->size);
		if (content->size && 
		    '\n' != content->data[content->size - 1])
			HBUF_PUTSL(ob, "\n");
		if ( ! st->mdoc)
			HBUF_PUTSL(ob, ".R\n");
	} else {
		HBUF_PUTSL(ob, "\\fB");
		hbuf_put(ob, content->data, content->size);
		HBUF_PUTSL(ob, "\\fP");
	}

	return 1;
}

static int
rndr_linebreak(hbuf *ob, void *data)
{

	/* FIXME: should this always have a newline? */

	HBUF_PUTSL(ob, "\n.br\n");
	return 1;
}

static void
rndr_header(hbuf *ob, const hbuf *content, int level, void *data)
{
	nroff_state	*st = data;

	if (NULL == content || 0 == content->size)
		return;

	if (st->mdoc && 1 == level)
		HBUF_PUTSL(ob, ".SH ");
	else if (st->mdoc)
		HBUF_PUTSL(ob, ".SS ");
	else if (st->flags & LOWDOWN_NROFF_GROFF) 
		hbuf_printf(ob, ".SH %d\n", level);
	else
		hbuf_printf(ob, ".SH\n");

	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
}

static int
rndr_link(hbuf *ob, const hbuf *content, 
	const hbuf *link, const hbuf *title, void *data, int nln)
{
	nroff_state	*st = data;

	if ((NULL == content || 0 == content->size) &&
	    (NULL == title || 0 == title->size) &&
	    (NULL == link || 0 == link->size))
		return 0;

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

	HBUF_PUTSL(ob, ".pdfhref W -D ");
	if (NULL != link && link->size)
		escape_oneline_span(ob, 
			link->data, link->size);
	HBUF_PUTSL(ob, " ");
	if (NULL != content && content->size)
		hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\n");
	return 1;
}

static void
rndr_list(hbuf *ob, const hbuf *content, hlist_fl flags, void *data)
{

	HBUF_PUTSL(ob, ".RS\n");
	if (NULL != content && content->size)
		hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, ".RE\n");
}

static void
rndr_listitem(hbuf *ob, const hbuf *content, 
	hlist_fl flags, void *data, size_t num)
{

	if (NULL == content || 0 == content->size)
		return;

	if (HLIST_ORDERED & flags)
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
rndr_paragraph(hbuf *ob, const hbuf *content, void *data, size_t par_count)
{
	nroff_state	*state = data;
	size_t	 	 i = 0, org;

	if (NULL == content || 0 == content->size)
		return;
	while (i < content->size && isspace((int)content->data[i]))
		i++;
	if (i == content->size)
		return;

	HBUF_PUTSL(ob, ".LP\n");

	if (state->flags & LOWDOWN_NROFF_HARD_WRAP) {
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

			rndr_linebreak(ob, data);
			i++;
		}
	} else
		hbuf_put(ob, content->data + i, content->size - i);

	BUFFER_NEWLINE(content->data + i, content->size - i, ob);
}

static void
rndr_raw_block(hbuf *ob, const hbuf *content, void *data)
{
	size_t org, sz;

	if ( ! content)
		return;

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
rndr_hrule(hbuf *ob, void *data)
{
	nroff_state 	*st = data;

	/*
	 * I'm not sure how else to do horizontal lines.
	 * The LP is to reset the margins.
	 */

	HBUF_PUTSL(ob, ".LP\n");
	if ( ! st->mdoc)
		HBUF_PUTSL(ob, "\\l\'\\n(.lu-\\n(\\n[.in]u\'\n");
}

static int
rndr_image(hbuf *ob, const hbuf *link, const hbuf *title, 
	const hbuf *dims, const hbuf *alt, void *data)
{

	warnx("warning: graphics not supported");
	return 1;
}

static int
rndr_raw_html(hbuf *ob, const hbuf *text, void *data)
{
	nroff_state 	*state = data;

	if ((state->flags & LOWDOWN_NROFF_SKIP_HTML) != 0)
		return 1;

	escape_block(ob, text->data, text->size);
	return 1;
}

static void
rndr_table(hbuf *ob, const hbuf *content, void *data)
{

	HBUF_PUTSL(ob, ".TS\n");
	HBUF_PUTSL(ob, "tab(|) allbox;\n");
	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".TE\n");
}

static void
rndr_table_header(hbuf *ob, const hbuf *content,
	void *data, const htbl_flags *fl, size_t columns)
{
	size_t	 i;

	for (i = 0; i < columns; i++) {
		if (i > 0)
			HBUF_PUTSL(ob, " ");
		switch (fl[i] & HTBL_ALIGNMASK) {
		case (HTBL_ALIGN_CENTER):
			HBUF_PUTSL(ob, "c");
			break;
		case (HTBL_ALIGN_RIGHT):
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
rndr_table_body(hbuf *ob, const hbuf *content, void *data)
{

	hbuf_put(ob, content->data, content->size);
}

static void
rndr_tablerow(hbuf *ob, const hbuf *content, void *data)
{

	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\n");
}

static void
rndr_tablecell(hbuf *ob, const hbuf *content,
	htbl_flags flags, void *data, size_t col, size_t columns)
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
rndr_superscript(hbuf *ob, const hbuf *content, void *data, int nln)
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
rndr_backspace(hbuf *ob)
{

	HBUF_PUTSL(ob, "\\h[-0.475]\n");
}

static void
rndr_normal_text(hbuf *ob, const hbuf *content, void *data, int nl)
{

	if (NULL == content || 0 == content->size)
		return;

	if (nl)
		escape_block(ob, content->data, content->size);
	else
		escape_span(ob, content->data, content->size);
}

static void
rndr_footnotes(hbuf *ob, const hbuf *content, void *data)
{
	nroff_state 	*st = data;

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
rndr_footnote_def(hbuf *ob, const hbuf *content, unsigned int num, void *data)
{

	HBUF_PUTSL(ob, ".LP\n");
	hbuf_printf(ob, "\\fI%u.\\fP\n", num);
	HBUF_PUTSL(ob, ".RS\n");
	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".RE\n");
}

static int
rndr_footnote_ref(hbuf *ob, unsigned int num, void *data)
{

	hbuf_printf(ob, "\\u\\s-3%u\\s+3\\d", num);
	return 1;
}

static int
rndr_math(hbuf *ob, const hbuf *text, int displaymode, void *data)
{

	warnx("warning: math not supported");
	return 1;
}

hrend *
hrend_nroff_new(unsigned int render_flags, int mdoc)
{
	static const hrend cb_default = {
		NULL,

		rndr_blockcode,
		rndr_blockquote,
		rndr_header,
		rndr_hrule,
		rndr_list,
		rndr_listitem,
		rndr_paragraph,
		rndr_table,
		rndr_table_header,
		rndr_table_body,
		rndr_tablerow,
		rndr_tablecell,
		rndr_footnotes,
		rndr_footnote_def,
		rndr_raw_block,

		rndr_autolink,
		rndr_codespan,
		rndr_double_emphasis,
		rndr_emphasis,
		rndr_highlight,
		rndr_image,
		rndr_linebreak,
		rndr_link,
		rndr_triple_emphasis,
		rndr_strikethrough,
		rndr_superscript,
		rndr_footnote_ref,
		rndr_math,
		rndr_raw_html,

		NULL,
		rndr_normal_text,
		rndr_backspace,

		NULL,
		NULL
	};
	nroff_state 	*state;
	hrend 		*renderer;

	/* Prepare the state pointer. */

	state = xcalloc(1, sizeof(nroff_state));

	state->flags = render_flags;
	state->mdoc = mdoc;

	/* Prepare the renderer. */

	renderer = xmalloc(sizeof(hrend));
	memcpy(renderer, &cb_default, sizeof(hrend));

	if (render_flags & LOWDOWN_NROFF_SKIP_HTML)
		renderer->blockhtml = NULL;

	renderer->opaque = state;
	return renderer;
}

void
hrend_nroff_free(hrend *renderer)
{

	free(renderer->opaque);
	free(renderer);
}
