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
#include <ctype.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	hhtml_fl flags;
} nroff_state;

static void
escape_buffer(hbuf *ob, const uint8_t *source, size_t length)
{

	hesc_nroff(ob, source, length, 0);
}

static int
rndr_autolink(hbuf *ob, const hbuf *link, halink_type type, void *data)
{

	if (NULL == link || 0 == link->size)
		return 0;

	/*
	 * Pretty printing: if we get an email address as
	 * an actual URI, e.g. `mailto:foo@bar.com`, we don't
	 * want to print the `mailto:` prefix
	 */

	HBUF_PUTSL(ob, "\\fI");
	if (hbuf_prefix(link, "mailto:") == 0)
		hbuf_put(ob, link->data + 7, link->size - 7);
	else
		hbuf_put(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\\fR");
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
		HBUF_PUTSL(ob, ".RS 0\n");
	} else
		HBUF_PUTSL(ob, ".DS\n");

	HBUF_PUTSL(ob, ".ft CR\n");
	escape_buffer(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".ft\n");

	if (st->mdoc)
		HBUF_PUTSL(ob, ".RE\n");
	else
		HBUF_PUTSL(ob, ".DE\n");
}

static void
rndr_blockquote(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return;

	HBUF_PUTSL(ob, ".RS\n");
	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".RE\n");
}

static int
rndr_codespan(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\f[CW]");
	escape_buffer(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fR");
	return 1;
}

static int
rndr_strikethrough(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	hbuf_put(ob, content->data, content->size);
	return 1;
}

static int
rndr_double_emphasis(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\fB");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fR");
	return 1;
}

static int
rndr_triple_emphasis(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\f(BI");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fR");
	return 1;
}


static int
rndr_emphasis(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\fI");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fR");
	return 1;
}

static int
rndr_underline(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	hbuf_put(ob, content->data, content->size);
	return 1;
}

static int
rndr_highlight(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\fB");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\fR");
	return 1;
}

static int
rndr_quote(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HBUF_PUTSL(ob, "\\(lq");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "\\(rq");
	return 1;
}

static int
rndr_linebreak(hbuf *ob, void *data)
{

	HBUF_PUTSL(ob, ".br\n");
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
	else
		HBUF_PUTSL(ob, ".SH\n");

	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
}

static int
rndr_link(hbuf *ob, const hbuf *content, const hbuf *link, const hbuf *title, void *data)
{

	if ((NULL == content || 0 == content->size) &&
	    (NULL == title || 0 == title->size) &&
	    (NULL == link || 0 == link->size))
		return 0;

	HBUF_PUTSL(ob, "\\fI");
	if (NULL != content && 0 != content->size)
		hbuf_put(ob, content->data, content->size);
	else if (NULL != title && 0 != title->size)
		hbuf_put(ob, title->data, title->size);
	else
		hbuf_put(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\\fR");

	return 1;
}

static void
rndr_list(hbuf *ob, const hbuf *content, hlist_fl flags, void *data)
{

	if (NULL != content && content->size) 
		hbuf_put(ob, content->data, content->size);
}

static void
rndr_listitem(hbuf *ob, const hbuf *content, hlist_fl flags, void *data)
{

	if (NULL == content || 0 == content->size) 
		return;

	HBUF_PUTSL(ob, ".IP -\n");
	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
}

static void
rndr_paragraph(hbuf *ob, const hbuf *content, void *data)
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

	if (state->flags & HOEDOWN_HTML_HARD_WRAP) {
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

	/* FIXME: Do we *really* need to trim the HTML? How does that make a difference? */

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

	/* 
	 * FIXME: over-complicated. 
	 * I'm not sure how else to do horizontal lines.
	 */

	HBUF_PUTSL(ob, ".br\n");
	HBUF_PUTSL(ob, "\\l\'\\n(.l/1\'\n");
}

static int
rndr_image(hbuf *ob, const hbuf *link, const hbuf *title, const hbuf *alt, void *data)
{

	warnx("warning: graphics not supported");
	return 1;
}

static int
rndr_raw_html(hbuf *ob, const hbuf *text, void *data)
{
	nroff_state 	*state = data;

	/* 
	 * ESCAPE overrides SKIP_HTML. It doesn't look to see if
	 * there are any valid tags, just escapes all of them. 
	 */
	if ((state->flags & HOEDOWN_HTML_ESCAPE) != 0) {
		escape_buffer(ob, text->data, text->size);
		return 1;
	}

	if ((state->flags & HOEDOWN_HTML_SKIP_HTML) != 0)
		return 1;

	hbuf_put(ob, text->data, text->size);
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
	if (NULL != content && content->size)
		hbuf_put(ob, content->data, content->size);
}

static int
rndr_superscript(hbuf *ob, const hbuf *content, void *data)
{
	if (!content || !content->size) return 0;
	HBUF_PUTSL(ob, "<sup>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</sup>");
	return 1;
}

static void
rndr_normal_text(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL != content)
		escape_buffer(ob, content->data, content->size);
}

static void
rndr_footnotes(hbuf *ob, const hbuf *content, void *data)
{

	if (NULL == content || 0 == content->size) 
		return;

	HBUF_PUTSL(ob, ".br\n");
	HBUF_PUTSL(ob, "\\l\'\\n(.l/1.5\'\n");
	hbuf_put(ob, content->data, content->size);
}

static void
rndr_footnote_def(hbuf *ob, const hbuf *content, unsigned int num, void *data)
{

	HBUF_PUTSL(ob, ".LP\n");
	hbuf_printf(ob, "\\fIFootnote [%u]\\fP\n", num);
	HBUF_PUTSL(ob, ".RS\n");
	hbuf_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HBUF_PUTSL(ob, ".RE\n");
}

static int
rndr_footnote_ref(hbuf *ob, unsigned int num, void *data)
{

	hbuf_printf(ob, " [%u]", num);
	return 1;
}

static int
rndr_math(hbuf *ob, const hbuf *text, int displaymode, void *data)
{

	warnx("warning: math not supported");
	return 1;
}

hrend *
hrend_nroff_new(hhtml_fl render_flags, int mdoc)
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
		rndr_underline,
		rndr_highlight,
		rndr_quote,
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

		NULL,
		NULL
	};

	nroff_state 	*state;
	hrend 		*renderer;

	/* Prepare the state pointer */
	state = xmalloc(sizeof(nroff_state));
	memset(state, 0x0, sizeof(nroff_state));

	state->flags = render_flags;
	state->mdoc = mdoc;

	/* Prepare the renderer */
	renderer = xmalloc(sizeof(hrend));
	memcpy(renderer, &cb_default, sizeof(hrend));

	if (render_flags & HOEDOWN_HTML_SKIP_HTML || render_flags & HOEDOWN_HTML_ESCAPE)
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
