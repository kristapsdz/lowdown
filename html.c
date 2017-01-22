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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

typedef struct html_state {
	struct {
		int header_count;
		int current_level;
		int level_offset;
		int nesting_level;
	} toc_data;
	unsigned int flags;
} html_state;

static void
escape_html(hbuf *ob, const uint8_t *source, size_t length)
{

	hesc_html(ob, source, length, 0);
}

static void
escape_href(hbuf *ob, const uint8_t *source, size_t length)
{

	hesc_href(ob, source, length);
}

/* checks if data starts with a specific tag, returns the tag type or NONE */
hhtml_tag
hhtml_get_tag(const uint8_t *data, size_t size, const char *tagname)
{
	size_t i;
	int closed = 0;

	if (size < 3 || data[0] != '<')
		return HOEDOWN_HTML_TAG_NONE;

	i = 1;

	if (data[i] == '/') {
		closed = 1;
		i++;
	}

	for (; i < size; ++i, ++tagname) {
		if (*tagname == 0)
			break;

		if (data[i] != *tagname)
			return HOEDOWN_HTML_TAG_NONE;
	}

	if (i == size)
		return HOEDOWN_HTML_TAG_NONE;

	if (isspace(data[i]) || data[i] == '>')
		return closed ? HOEDOWN_HTML_TAG_CLOSE : HOEDOWN_HTML_TAG_OPEN;

	return HOEDOWN_HTML_TAG_NONE;
}

static int
rndr_autolink(hbuf *ob, const hbuf *link, halink_type type, void *data)
{

	if (!link || !link->size)
		return 0;

	HBUF_PUTSL(ob, "<a href=\"");
	if (type == HALINK_EMAIL)
		HBUF_PUTSL(ob, "mailto:");
	escape_href(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\">");

	/*
	 * Pretty printing: if we get an email address as
	 * an actual URI, e.g. `mailto:foo@bar.com`, we don't
	 * want to print the `mailto:` prefix
	 */
	if (hbuf_prefix(link, "mailto:") == 0) {
		escape_html(ob, link->data + 7, link->size - 7);
	} else {
		escape_html(ob, link->data, link->size);
	}

	HBUF_PUTSL(ob, "</a>");

	return 1;
}

static void
rndr_blockcode(hbuf *ob, const hbuf *text, const hbuf *lang, void *data)
{
	if (ob->size) hbuf_putc(ob, '\n');

	if (lang) {
		HBUF_PUTSL(ob, "<pre><code class=\"language-");
		escape_html(ob, lang->data, lang->size);
		HBUF_PUTSL(ob, "\">");
	} else {
		HBUF_PUTSL(ob, "<pre><code>");
	}

	if (text)
		escape_html(ob, text->data, text->size);

	HBUF_PUTSL(ob, "</code></pre>\n");
}

static void
rndr_blockquote(hbuf *ob, const hbuf *content, void *data)
{
	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<blockquote>\n");
	if (content)
		hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</blockquote>\n");
}

static int
rndr_codespan(hbuf *ob, const hbuf *text, void *data)
{

	HBUF_PUTSL(ob, "<code>");
	if (text)
		escape_html(ob, text->data, text->size);
	HBUF_PUTSL(ob, "</code>");
	return 1;
}

static int
rndr_strikethrough(hbuf *ob, const hbuf *content, void *data)
{
	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<del>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</del>");
	return 1;
}

static int
rndr_double_emphasis(hbuf *ob, const hbuf *content, void *data)
{
	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<strong>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</strong>");

	return 1;
}

static int
rndr_emphasis(hbuf *ob, const hbuf *content, void *data)
{
	if (!content || !content->size) return 0;
	HBUF_PUTSL(ob, "<em>");
	if (content) hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</em>");
	return 1;
}

static int
rndr_underline(hbuf *ob, const hbuf *content, void *data)
{
	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<u>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</u>");

	return 1;
}

static int
rndr_highlight(hbuf *ob, const hbuf *content, void *data)
{
	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<mark>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</mark>");

	return 1;
}

static int
rndr_quote(hbuf *ob, const hbuf *content, void *data)
{
	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<q>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</q>");

	return 1;
}

static int
rndr_linebreak(hbuf *ob, void *data)
{

	hbuf_puts(ob, "<br/>\n");
	return 1;
}

static void
rndr_header(hbuf *ob, const hbuf *content, int level, void *data)
{
	html_state	*state = data;

	if (ob->size)
		hbuf_putc(ob, '\n');

	if (level <= state->toc_data.nesting_level)
		hbuf_printf(ob, "<h%d id=\"toc_%d\">", level, state->toc_data.header_count++);
	else
		hbuf_printf(ob, "<h%d>", level);

	if (content) hbuf_put(ob, content->data, content->size);
	hbuf_printf(ob, "</h%d>\n", level);
}

static int
rndr_link(hbuf *ob, const hbuf *content, const hbuf *link, const hbuf *title, void *data)
{

	HBUF_PUTSL(ob, "<a href=\"");

	if (link && link->size)
		escape_href(ob, link->data, link->size);

	if (title && title->size) {
		HBUF_PUTSL(ob, "\" title=\"");
		escape_html(ob, title->data, title->size);
	}

	HBUF_PUTSL(ob, "\">");

	if (content && content->size)
		hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</a>");
	return 1;
}

static void
rndr_list(hbuf *ob, const hbuf *content, hlist_fl flags, void *data)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	hbuf_put(ob, (const uint8_t *)(flags & HLIST_ORDERED ?
		"<ol>\n" : "<ul>\n"), 5);
	if (content)
		hbuf_put(ob, content->data, content->size);
	hbuf_put(ob, (const uint8_t *)(flags & HLIST_ORDERED ?
		"</ol>\n" : "</ul>\n"), 6);
}

static void
rndr_listitem(hbuf *ob, const hbuf *content, hlist_fl flags, void *data, size_t num)
{
	size_t	 size;

	HBUF_PUTSL(ob, "<li>");
	if (content) {
		size = content->size;
		while (size && content->data[size - 1] == '\n')
			size--;

		hbuf_put(ob, content->data, size);
	}
	HBUF_PUTSL(ob, "</li>\n");
}

static void
rndr_paragraph(hbuf *ob, const hbuf *content, void *data, size_t par_count)
{
	html_state *state = data;
	size_t i = 0;

	if (ob->size) hbuf_putc(ob, '\n');

	if (!content || !content->size)
		return;

	while (i < content->size && isspace(content->data[i])) i++;

	if (i == content->size)
		return;

	HBUF_PUTSL(ob, "<p>");
	if (state->flags & LOWDOWN_HTML_HARD_WRAP) {
		size_t org;
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
	} else {
		hbuf_put(ob, content->data + i, content->size - i);
	}
	HBUF_PUTSL(ob, "</p>\n");
}

static void
rndr_raw_block(hbuf *ob, const hbuf *text, void *data)
{
	size_t org, sz;

	if (!text)
		return;

	/* FIXME: Do we *really* need to trim the HTML? How does that make a difference? */
	sz = text->size;
	while (sz > 0 && text->data[sz - 1] == '\n')
		sz--;

	org = 0;
	while (org < sz && text->data[org] == '\n')
		org++;

	if (org >= sz)
		return;

	if (ob->size)
		hbuf_putc(ob, '\n');

	hbuf_put(ob, text->data + org, sz - org);
	hbuf_putc(ob, '\n');
}

static int
rndr_triple_emphasis(hbuf *ob, const hbuf *content, void *data)
{

	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<strong><em>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</em></strong>");
	return 1;
}

static void
rndr_hrule(hbuf *ob, void *data)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	hbuf_puts(ob, "<hr/>\n");
}

static int
rndr_image(hbuf *ob, const hbuf *link, const hbuf *title, const hbuf *alt, void *data)
{

	if (!link || !link->size) return 0;

	HBUF_PUTSL(ob, "<img src=\"");
	escape_href(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\" alt=\"");

	if (alt && alt->size)
		escape_html(ob, alt->data, alt->size);

	if (title && title->size) {
		HBUF_PUTSL(ob, "\" title=\"");
		escape_html(ob, title->data, title->size); }

	hbuf_puts(ob, "\"/>");
	return 1;
}

static int
rndr_raw_html(hbuf *ob, const hbuf *text, void *data)
{
	html_state *state = data;

	/* ESCAPE overrides SKIP_HTML. It doesn't look to see if
	 * there are any valid tags, just escapes all of them. */
	if((state->flags & LOWDOWN_HTML_ESCAPE) != 0) {
		escape_html(ob, text->data, text->size);
		return 1;
	}

	if ((state->flags & LOWDOWN_HTML_SKIP_HTML) != 0)
		return 1;

	hbuf_put(ob, text->data, text->size);
	return 1;
}

static void
rndr_table(hbuf *ob, const hbuf *content, void *data)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<table>\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</table>\n");
}

static void
rndr_table_header(hbuf *ob, const hbuf *content, void *data, const htbl_flags *fl, size_t columns)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<thead>\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</thead>\n");
}

static void
rndr_table_body(hbuf *ob, const hbuf *content, void *data)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<tbody>\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</tbody>\n");
}

static void
rndr_tablerow(hbuf *ob, const hbuf *content, void *data)
{

	HBUF_PUTSL(ob, "<tr>\n");
	if (content)
		hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</tr>\n");
}

static void
rndr_tablecell(hbuf *ob, const hbuf *content, htbl_flags flags, void *data, size_t col, size_t columns)
{

	if (flags & HTBL_HEADER)
		HBUF_PUTSL(ob, "<th");
	else
		HBUF_PUTSL(ob, "<td");

	switch (flags & HTBL_ALIGNMASK) {
	case HTBL_ALIGN_CENTER:
		HBUF_PUTSL(ob, " style=\"text-align: center\">");
		break;
	case HTBL_ALIGN_LEFT:
		HBUF_PUTSL(ob, " style=\"text-align: left\">");
		break;
	case HTBL_ALIGN_RIGHT:
		HBUF_PUTSL(ob, " style=\"text-align: right\">");
		break;
	default:
		HBUF_PUTSL(ob, ">");
	}

	if (content)
		hbuf_put(ob, content->data, content->size);

	if (flags & HTBL_HEADER)
		HBUF_PUTSL(ob, "</th>\n");
	else
		HBUF_PUTSL(ob, "</td>\n");
}

static int
rndr_superscript(hbuf *ob, const hbuf *content, void *data)
{

	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<sup>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</sup>");
	return 1;
}

static void
rndr_normal_text(hbuf *ob, const hbuf *content, void *data, int nl)
{

	if (content)
		escape_html(ob, content->data, content->size);
}

static void
rndr_footnotes(hbuf *ob, const hbuf *content, void *data)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<div class=\"footnotes\">\n");
	hbuf_puts(ob, "<hr/>\n");
	HBUF_PUTSL(ob, "<ol>\n");

	if (content)
		hbuf_put(ob, content->data, content->size);

	HBUF_PUTSL(ob, "\n</ol>\n</div>\n");
}

static void
rndr_footnote_def(hbuf *ob, const hbuf *content, unsigned int num, void *data)
{
	size_t i = 0;
	int pfound = 0;

	/* insert anchor at the end of first paragraph block */
	if (content) {
		while ((i+3) < content->size) {
			if (content->data[i++] != '<') continue;
			if (content->data[i++] != '/') continue;
			if (content->data[i++] != 'p' && content->data[i] != 'P') continue;
			if (content->data[i] != '>') continue;
			i -= 3;
			pfound = 1;
			break;
		}
	}

	hbuf_printf(ob, "\n<li id=\"fn%d\">\n", num);
	if (pfound) {
		hbuf_put(ob, content->data, i);
		hbuf_printf(ob, "&nbsp;<a href=\"#fnref%d\" rev=\"footnote\">&#8617;</a>", num);
		hbuf_put(ob, content->data + i, content->size - i);
	} else if (content) {
		hbuf_put(ob, content->data, content->size);
	}
	HBUF_PUTSL(ob, "</li>\n");
}

static int
rndr_footnote_ref(hbuf *ob, unsigned int num, void *data)
{
	hbuf_printf(ob, "<sup id=\"fnref%d\"><a href=\"#fn%d\" rel=\"footnote\">%d</a></sup>", num, num, num);
	return 1;
}

static int
rndr_math(hbuf *ob, const hbuf *text, int displaymode, void *data)
{
	hbuf_put(ob, (const uint8_t *)(displaymode ? "\\[" : "\\("), 2);
	escape_html(ob, text->data, text->size);
	hbuf_put(ob, (const uint8_t *)(displaymode ? "\\]" : "\\)"), 2);
	return 1;
}

/* allocates a regular HTML renderer */
hrend *
hrend_html_new(unsigned int render_flags, int nesting_level)
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

	html_state *state;
	hrend *renderer;

	/* Prepare the state pointer */
	state = xmalloc(sizeof(html_state));
	memset(state, 0x0, sizeof(html_state));

	state->flags = render_flags;
	state->toc_data.nesting_level = nesting_level;

	/* Prepare the renderer */
	renderer = xmalloc(sizeof(hrend));
	memcpy(renderer, &cb_default, sizeof(hrend));

	if (render_flags & LOWDOWN_HTML_SKIP_HTML || render_flags & LOWDOWN_HTML_ESCAPE)
		renderer->blockhtml = NULL;

	renderer->opaque = state;
	return renderer;
}

/* deallocate an HTML renderer */
void
hrend_html_free(hrend *renderer)
{
	free(renderer->opaque);
	free(renderer);
}
