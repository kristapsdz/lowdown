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

#include "extern.h"

#define USE_XHTML(opt) (opt->flags & HOEDOWN_HTML_USE_XHTML)

static void escape_buffer(hoedown_buffer *ob, const uint8_t *source, size_t length)
{
	hoedown_escape_nroff(ob, source, length, 0);
}

#define	BUFFER_NEWLINE(_buf, _sz, _ob) \
	do if ((_sz) > 0 && '\n' != (_buf)[(_sz) - 1]) \
		hoedown_buffer_putc((_ob), '\n'); \
	while (/* CONSTCOND */ 0)

/********************
 * GENERIC RENDERER *
 ********************/
static int
rndr_autolink(hoedown_buffer *ob, const hoedown_buffer *link, hoedown_autolink_type type, const hoedown_renderer_data *data)
{

	if (NULL == link || 0 == link->size)
		return 0;

	/*
	 * Pretty printing: if we get an email address as
	 * an actual URI, e.g. `mailto:foo@bar.com`, we don't
	 * want to print the `mailto:` prefix
	 */

	HOEDOWN_BUFPUTSL(ob, "\\fI");
	if (hoedown_buffer_prefix(link, "mailto:") == 0)
		hoedown_buffer_put(ob, link->data + 7, link->size - 7);
	else
		hoedown_buffer_put(ob, link->data, link->size);
	HOEDOWN_BUFPUTSL(ob, "\\fR");
	return 1;
}

static void
rndr_blockcode(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_buffer *lang, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return;

	HOEDOWN_BUFPUTSL(ob, ".DS\n");
	escape_buffer(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HOEDOWN_BUFPUTSL(ob, ".DE\n");
}

static void
rndr_blockquote(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return;

	HOEDOWN_BUFPUTSL(ob, ".RS\n");
	hoedown_buffer_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
	HOEDOWN_BUFPUTSL(ob, ".RE\n");
}

static int
rndr_codespan(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	escape_buffer(ob, content->data, content->size);
	return 1;
}

static int
rndr_strikethrough(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	hoedown_buffer_put(ob, content->data, content->size);
	return 1;
}

static int
rndr_double_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HOEDOWN_BUFPUTSL(ob, "\\fB");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\\fR");
	return 1;
}

static int
rndr_triple_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HOEDOWN_BUFPUTSL(ob, "\\f(BI");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\\fR");
	return 1;
}


static int
rndr_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HOEDOWN_BUFPUTSL(ob, "\\fI");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\\fR");
	return 1;
}

static int
rndr_underline(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	hoedown_buffer_put(ob, content->data, content->size);
	return 1;
}

static int
rndr_highlight(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HOEDOWN_BUFPUTSL(ob, "\\fB");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\\fR");
	return 1;
}

static int
rndr_quote(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return(0);

	HOEDOWN_BUFPUTSL(ob, "\\(lq");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\\(rq");
	return 1;
}

static int
rndr_linebreak(hoedown_buffer *ob, const hoedown_renderer_data *data)
{

	HOEDOWN_BUFPUTSL(ob, ".br\n");
	return 1;
}

static void
rndr_header(hoedown_buffer *ob, const hoedown_buffer *content, int level, const hoedown_renderer_data *data)
{

	if (NULL == content || 0 == content->size)
		return;

	HOEDOWN_BUFPUTSL(ob, ".SH\n");
	hoedown_buffer_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
}

static int
rndr_link(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_renderer_data *data)
{

	if (NULL == content && 0 == content->size &&
	    NULL == title && 0 == title->size &&
	    NULL == link && 0 == link->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "\\fI");
	if (NULL != content && 0 != content->size)
		hoedown_buffer_put(ob, content->data, content->size);
	else if (NULL != title && 0 != title->size)
		hoedown_buffer_put(ob, title->data, title->size);
	else
		hoedown_buffer_put(ob, link->data, link->size);
	HOEDOWN_BUFPUTSL(ob, "\\fR");

	return 1;
}

static void
rndr_list(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data)
{

	if (NULL != content && content->size) 
		hoedown_buffer_put(ob, content->data, content->size);
}

static void
rndr_listitem(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data)
{

	if (NULL == content && 0 == content->size) 
		return;

	HOEDOWN_BUFPUTSL(ob, ".IP -\n");
	hoedown_buffer_put(ob, content->data, content->size);
	BUFFER_NEWLINE(content->data, content->size, ob);
}

static void
rndr_paragraph(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;
	size_t	 i = 0, org;

	if (NULL == content || 0 == content->size)
		return;
	while (i < content->size && isspace((int)content->data[i])) 
		i++;
	if (i == content->size)
		return;

	HOEDOWN_BUFPUTSL(ob, ".PP\n");

	if (state->flags & HOEDOWN_HTML_HARD_WRAP) {
		while (i < content->size) {
			org = i;
			while (i < content->size && content->data[i] != '\n')
				i++;

			if (i > org)
				hoedown_buffer_put(ob, content->data + org, i - org);

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
		hoedown_buffer_put(ob, content->data + i, content->size - i);

	BUFFER_NEWLINE(content->data + i, content->size - i, ob);
}

static void
rndr_raw_block(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
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
		hoedown_buffer_putc(ob, '\n');

	hoedown_buffer_put(ob, content->data + org, sz - org);
	hoedown_buffer_putc(ob, '\n');
}

static void
rndr_hrule(hoedown_buffer *ob, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	hoedown_buffer_puts(ob, USE_XHTML(state) ? "<hr/>\n" : "<hr>\n");
}

static int
rndr_image(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_buffer *alt, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;
	if (!link || !link->size) return 0;

	HOEDOWN_BUFPUTSL(ob, "<img src=\"");
	hoedown_buffer_put(ob, link->data, link->size);
	HOEDOWN_BUFPUTSL(ob, "\" alt=\"");

	if (alt && alt->size)
		escape_buffer(ob, alt->data, alt->size);

	if (title && title->size) {
		HOEDOWN_BUFPUTSL(ob, "\" title=\"");
		escape_buffer(ob, title->data, title->size); }

	hoedown_buffer_puts(ob, USE_XHTML(state) ? "\"/>" : "\">");
	return 1;
}

static int
rndr_raw_html(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;

	/* ESCAPE overrides SKIP_HTML. It doesn't look to see if
	 * there are any valid tags, just escapes all of them. */
	if((state->flags & HOEDOWN_HTML_ESCAPE) != 0) {
		escape_buffer(ob, text->data, text->size);
		return 1;
	}

	if ((state->flags & HOEDOWN_HTML_SKIP_HTML) != 0)
		return 1;

	hoedown_buffer_put(ob, text->data, text->size);
	return 1;
}

static void
rndr_table(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
    if (ob->size) hoedown_buffer_putc(ob, '\n');
    HOEDOWN_BUFPUTSL(ob, "<table>\n");
    hoedown_buffer_put(ob, content->data, content->size);
    HOEDOWN_BUFPUTSL(ob, "</table>\n");
}

static void
rndr_table_header(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
    if (ob->size) hoedown_buffer_putc(ob, '\n');
    HOEDOWN_BUFPUTSL(ob, "<thead>\n");
    hoedown_buffer_put(ob, content->data, content->size);
    HOEDOWN_BUFPUTSL(ob, "</thead>\n");
}

static void
rndr_table_body(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
    if (ob->size) hoedown_buffer_putc(ob, '\n');
    HOEDOWN_BUFPUTSL(ob, "<tbody>\n");
    hoedown_buffer_put(ob, content->data, content->size);
    HOEDOWN_BUFPUTSL(ob, "</tbody>\n");
}

static void
rndr_tablerow(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "<tr>\n");
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</tr>\n");
}

static void
rndr_tablecell(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_table_flags flags, const hoedown_renderer_data *data)
{
	if (flags & HOEDOWN_TABLE_HEADER) {
		HOEDOWN_BUFPUTSL(ob, "<th");
	} else {
		HOEDOWN_BUFPUTSL(ob, "<td");
	}

	switch (flags & HOEDOWN_TABLE_ALIGNMASK) {
	case HOEDOWN_TABLE_ALIGN_CENTER:
		HOEDOWN_BUFPUTSL(ob, " style=\"text-align: center\">");
		break;

	case HOEDOWN_TABLE_ALIGN_LEFT:
		HOEDOWN_BUFPUTSL(ob, " style=\"text-align: left\">");
		break;

	case HOEDOWN_TABLE_ALIGN_RIGHT:
		HOEDOWN_BUFPUTSL(ob, " style=\"text-align: right\">");
		break;

	default:
		HOEDOWN_BUFPUTSL(ob, ">");
	}

	if (content)
		hoedown_buffer_put(ob, content->data, content->size);

	if (flags & HOEDOWN_TABLE_HEADER) {
		HOEDOWN_BUFPUTSL(ob, "</th>\n");
	} else {
		HOEDOWN_BUFPUTSL(ob, "</td>\n");
	}
}

static int
rndr_superscript(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size) return 0;
	HOEDOWN_BUFPUTSL(ob, "<sup>");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</sup>");
	return 1;
}

static void
rndr_normal_text(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (content)
		escape_buffer(ob, content->data, content->size);
}

static void
rndr_footnotes(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;

	if (ob->size) hoedown_buffer_putc(ob, '\n');
	HOEDOWN_BUFPUTSL(ob, "<div class=\"footnotes\">\n");
	hoedown_buffer_puts(ob, USE_XHTML(state) ? "<hr/>\n" : "<hr>\n");
	HOEDOWN_BUFPUTSL(ob, "<ol>\n");

	if (content) hoedown_buffer_put(ob, content->data, content->size);

	HOEDOWN_BUFPUTSL(ob, "\n</ol>\n</div>\n");
}

static void
rndr_footnote_def(hoedown_buffer *ob, const hoedown_buffer *content, unsigned int num, const hoedown_renderer_data *data)
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

	hoedown_buffer_printf(ob, "\n<li id=\"fn%d\">\n", num);
	if (pfound) {
		hoedown_buffer_put(ob, content->data, i);
		hoedown_buffer_printf(ob, "&nbsp;<a href=\"#fnref%d\" rev=\"footnote\">&#8617;</a>", num);
		hoedown_buffer_put(ob, content->data + i, content->size - i);
	} else if (content) {
		hoedown_buffer_put(ob, content->data, content->size);
	}
	HOEDOWN_BUFPUTSL(ob, "</li>\n");
}

static int
rndr_footnote_ref(hoedown_buffer *ob, unsigned int num, const hoedown_renderer_data *data)
{
	hoedown_buffer_printf(ob, "<sup id=\"fnref%d\"><a href=\"#fn%d\" rel=\"footnote\">%d</a></sup>", num, num, num);
	return 1;
}

static int
rndr_math(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, const hoedown_renderer_data *data)
{
	hoedown_buffer_put(ob, (const uint8_t *)(displaymode ? "\\[" : "\\("), 2);
	escape_buffer(ob, text->data, text->size);
	hoedown_buffer_put(ob, (const uint8_t *)(displaymode ? "\\]" : "\\)"), 2);
	return 1;
}

hoedown_renderer *
hoedown_nroff_renderer_new(hoedown_html_flags render_flags, int nesting_level)
{
	static const hoedown_renderer cb_default = {
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

	hoedown_html_renderer_state *state;
	hoedown_renderer *renderer;

	/* Prepare the state pointer */
	state = xmalloc(sizeof(hoedown_html_renderer_state));
	memset(state, 0x0, sizeof(hoedown_html_renderer_state));

	state->flags = render_flags;
	state->toc_data.nesting_level = nesting_level;

	/* Prepare the renderer */
	renderer = xmalloc(sizeof(hoedown_renderer));
	memcpy(renderer, &cb_default, sizeof(hoedown_renderer));

	if (render_flags & HOEDOWN_HTML_SKIP_HTML || render_flags & HOEDOWN_HTML_ESCAPE)
		renderer->blockhtml = NULL;

	renderer->opaque = state;
	return renderer;
}

void
hoedown_nroff_renderer_free(hoedown_renderer *renderer)
{
	free(renderer->opaque);
	free(renderer);
}
