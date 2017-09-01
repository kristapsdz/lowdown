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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

/*
 * Queue entry for header names.
 * Keep these so we can make sure that headers have a unique "id" for
 * themselves.
 */
struct	hentry {
	char		*str; /* header name (raw) */
	size_t	 	 count; /* references */
	TAILQ_ENTRY(hentry) entries;
};

/*
 * Our internal state object.
 */
struct 	hstate {
	TAILQ_HEAD(, hentry) headers_used;
	unsigned int flags; /* output flags */
};

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

static int
rndr_autolink(hbuf *ob, const hbuf *link, halink_type type)
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
rndr_blockcode(hbuf *ob, const hbuf *text, const hbuf *lang)
{
	if (ob->size) hbuf_putc(ob, '\n');

	if (lang && lang->size) {
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
rndr_blockquote(hbuf *ob, const hbuf *content)
{
	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<blockquote>\n");
	if (content)
		hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</blockquote>\n");
}

static int
rndr_codespan(hbuf *ob, const hbuf *text)
{

	HBUF_PUTSL(ob, "<code>");
	if (text)
		escape_html(ob, text->data, text->size);
	HBUF_PUTSL(ob, "</code>");
	return 1;
}

static int
rndr_strikethrough(hbuf *ob, const hbuf *content)
{
	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<del>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</del>");
	return 1;
}

static int
rndr_double_emphasis(hbuf *ob, const hbuf *content)
{
	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<strong>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</strong>");

	return 1;
}

static int
rndr_emphasis(hbuf *ob, const hbuf *content)
{
	if (!content || !content->size) return 0;
	HBUF_PUTSL(ob, "<em>");
	if (content) hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</em>");
	return 1;
}

static int
rndr_highlight(hbuf *ob, const hbuf *content)
{
	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<mark>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</mark>");

	return 1;
}

static int
rndr_linebreak(hbuf *ob)
{

	hbuf_puts(ob, "<br/>\n");
	return 1;
}

/*
 * Given the header with non-empty content "header", fill "ob" with the
 * identifier used for the header.
 * This will reference-count the header so we don't have duplicates.
 */
static void
rndr_header_id(hbuf *ob, const hbuf *header, struct hstate *state)
{
	struct hentry	*hentry;

	/* 
	 * See if the header was previously already defind. 
	 * Note that in HTML5, the identifier is case sensitive.
	 */

	TAILQ_FOREACH(hentry, &state->headers_used, entries) {
		if (strlen(hentry->str) != header->size)
			continue;
		if (0 == strncmp(hentry->str, 
		    (const char *)header->data, header->size))
			break;
	}

	/* Convert to escaped values. */

	escape_href(ob, header->data, header->size);

	/*
	 * If we're non-unique, then append a "count" value.
	 * XXX: if we have a header named "foo-2", then two headers
	 * named "foo", we'll inadvertently have a collision.
	 * This is a bit much to keep track of, though...
	 */

	if (NULL != hentry) {
		hentry->count++;
		hbuf_printf(ob, "-%zu", hentry->count);
		return;
	} 

	/* Create new header entry. */

	hentry = xcalloc(1, sizeof(struct hentry));
	hentry->count = 1;
	hentry->str = xstrndup
		((const char *)header->data, header->size);
	TAILQ_INSERT_TAIL(&state->headers_used, hentry, entries);
}

static void
rndr_header(hbuf *ob, const hbuf *content, 
	int level, struct hstate *state)
{

	if (ob->size)
		hbuf_putc(ob, '\n');

	if (NULL != content && content->size &&
	   	   LOWDOWN_HTML_HEAD_IDS & state->flags) {
		hbuf_printf(ob, "<h%d id=\"", level);
		rndr_header_id(ob, content, state);
		HBUF_PUTSL(ob, "\">");
	} else
		hbuf_printf(ob, "<h%d>", level);

	if (NULL != content) 
		hbuf_put(ob, content->data, content->size);

	hbuf_printf(ob, "</h%d>\n", level);
}

static int
rndr_link(hbuf *ob, const hbuf *content, const hbuf *link, const hbuf *title)
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
rndr_list(hbuf *ob, const hbuf *content, hlist_fl flags)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	if (flags & HLIST_FL_ORDERED)
		HBUF_PUTSL(ob, "<ol>\n");
	else
		HBUF_PUTSL(ob, "<ul>\n");
	if (content)
		hbuf_put(ob, content->data, content->size);
	if (flags & HLIST_FL_ORDERED)
		HBUF_PUTSL(ob, "</ol>\n");
	else
		HBUF_PUTSL(ob, "</ul>\n");
}

static void
rndr_listitem(hbuf *ob, const hbuf *content, hlist_fl flags, size_t num)
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
rndr_paragraph(hbuf *ob, const hbuf *content, struct hstate *state)
{
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

			rndr_linebreak(ob);
			i++;
		}
	} else {
		hbuf_put(ob, content->data + i, content->size - i);
	}
	HBUF_PUTSL(ob, "</p>\n");
}

/*
 * FIXME: verify behaviour.
 */
static void
rndr_raw_block(hbuf *ob, const hbuf *text, const struct hstate *state)
{
	size_t org, sz;

	if (NULL == text)
		return;

	if (state->flags & LOWDOWN_HTML_SKIP_HTML || 
	    state->flags & LOWDOWN_HTML_ESCAPE) {
		escape_html(ob, text->data, text->size);
		return;
	}

	/* 
	 * FIXME: Do we *really* need to trim the HTML? How does that
	 * make a difference? 
	 */

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
rndr_triple_emphasis(hbuf *ob, const hbuf *content)
{

	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<strong><em>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</em></strong>");
	return 1;
}

static void
rndr_hrule(hbuf *ob)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	hbuf_puts(ob, "<hr/>\n");
}

static int
rndr_image(hbuf *ob, const hbuf *link, const hbuf *title, 
	const hbuf *dims, const hbuf *alt)
{
	char	 	dimbuf[32];
	int		x, y, rc = 0;

	/*
	 * Scan in our dimensions, if applicable.
	 * It's unreasonable for them to be over 32 characters, so use
	 * that as a cap to the size.
	 */

	if (NULL != dims && dims->size &&
	    dims->size < sizeof(dimbuf) - 1) {
		memset(dimbuf, 0, sizeof(dimbuf));
		memcpy(dimbuf, dims->data, dims->size);
		rc = sscanf(dimbuf, "%ux%u", &x, &y);
	}

	HBUF_PUTSL(ob, "<img src=\"");
	if (NULL != link)
		escape_href(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\" alt=\"");
	if (NULL != alt && alt->size)
		escape_html(ob, alt->data, alt->size);
	HBUF_PUTSL(ob, "\"");

	if (NULL != dims && rc > 0) {
		hbuf_printf(ob, " width=\"%u\"", x);
		if (rc > 1)
			hbuf_printf(ob, " height=\"%u\"", y);
	}

	if (title && title->size) {
		HBUF_PUTSL(ob, " title=\"");
		escape_html(ob, title->data, title->size); 
		HBUF_PUTSL(ob, "\"");
	}

	hbuf_puts(ob, " />");
	return 1;
}

static int
rndr_raw_html(hbuf *ob, const hbuf *text, const struct hstate *state)
{

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
rndr_table(hbuf *ob, const hbuf *content)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<table>\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</table>\n");
}

static void
rndr_table_header(hbuf *ob, const hbuf *content, 
	const htbl_flags *fl, size_t columns)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<thead>\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</thead>\n");
}

static void
rndr_table_body(hbuf *ob, const hbuf *content)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<tbody>\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</tbody>\n");
}

static void
rndr_tablerow(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<tr>\n");
	if (content)
		hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</tr>\n");
}

static void
rndr_tablecell(hbuf *ob, const hbuf *content, htbl_flags flags, size_t col, size_t columns)
{

	if (flags & HTBL_FL_HEADER)
		HBUF_PUTSL(ob, "<th");
	else
		HBUF_PUTSL(ob, "<td");

	switch (flags & HTBL_FL_ALIGNMASK) {
	case HTBL_FL_ALIGN_CENTER:
		HBUF_PUTSL(ob, " style=\"text-align: center\">");
		break;
	case HTBL_FL_ALIGN_LEFT:
		HBUF_PUTSL(ob, " style=\"text-align: left\">");
		break;
	case HTBL_FL_ALIGN_RIGHT:
		HBUF_PUTSL(ob, " style=\"text-align: right\">");
		break;
	default:
		HBUF_PUTSL(ob, ">");
	}

	if (content)
		hbuf_put(ob, content->data, content->size);

	if (flags & HTBL_FL_HEADER)
		HBUF_PUTSL(ob, "</th>\n");
	else
		HBUF_PUTSL(ob, "</td>\n");
}

static int
rndr_superscript(hbuf *ob, const hbuf *content)
{

	if (!content || !content->size)
		return 0;

	HBUF_PUTSL(ob, "<sup>");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</sup>");
	return 1;
}

static void
rndr_normal_text(hbuf *ob, const hbuf *content)
{

	if (content)
		escape_html(ob, content->data, content->size);
}

static void
rndr_footnotes(hbuf *ob, const hbuf *content)
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
rndr_footnote_def(hbuf *ob, const hbuf *content, unsigned int num)
{
	size_t i = 0;
	int pfound = 0;

	/* Insert anchor at the end of first paragraph block. */

	if (content) {
		while ((i+3) < content->size) {
			if (content->data[i++] != '<') 
				continue;
			if (content->data[i++] != '/') 
				continue;
			if (content->data[i++] != 'p' && 
			    content->data[i] != 'P') 
				continue;
			if (content->data[i] != '>') 
				continue;
			i -= 3;
			pfound = 1;
			break;
		}
	}

	hbuf_printf(ob, "\n<li id=\"fn%d\">\n", num);

	if (pfound) {
		hbuf_put(ob, content->data, i);
		hbuf_printf(ob, "&nbsp;"
			"<a href=\"#fnref%d\" rev=\"footnote\">"
			"&#8617;</a>", num);
		hbuf_put(ob, content->data + i, content->size - i);
	} else if (content) {
		hbuf_put(ob, content->data, content->size);
	}

	HBUF_PUTSL(ob, "</li>\n");
}

static int
rndr_footnote_ref(hbuf *ob, unsigned int num)
{

	hbuf_printf(ob, 
		"<sup id=\"fnref%d\">"
		"<a href=\"#fn%d\" rel=\"footnote\">"
		"%d</a></sup>", num, num, num);
	return 1;
}

static int
rndr_math(hbuf *ob, const hbuf *text, int displaymode)
{

	if (displaymode)
		HBUF_PUTSL(ob, "\\[");
	else
		HBUF_PUTSL(ob, "\\(");
	escape_html(ob, text->data, text->size);
	if (displaymode)
		HBUF_PUTSL(ob, "\\]");
	else
		HBUF_PUTSL(ob, "\\)");
	return 1;
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
 * Itereate through multiple multi-white-space separated values in
 * "val", filling them in to "env".
 * If "href", escape the value as an HTML attribute.
 * Otherwise, just do the minimal HTML escaping.
 */
static void
rndr_doc_header_multi(hbuf *ob, int href,
	const char *val, const char *env)
{
	const char	*cp, *start;
	size_t		 sz;

	for (cp = val; '\0' != *cp; ) {
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
		hbuf_puts(ob, env);
		hbuf_putc(ob, '"');
		if (href)
			hesc_href(ob, (const uint8_t *)start, sz);
		else
			hesc_html(ob, (const uint8_t *)start, sz, 0);
		HBUF_PUTSL(ob, "\" />\n");
	}
}

static void
rndr_doc_footer(hbuf *ob, const struct hstate *st)
{

	if (LOWDOWN_STANDALONE & st->flags)
		HBUF_PUTSL(ob, "</body>\n</html>\n");
}

static void
rndr_doc_header(hbuf *ob, 
	const struct lowdown_meta *m, size_t msz, 
	const struct hstate *st)
{
	const char	*author = NULL, *title = "Untitled article", 
	     	 	*css = NULL;
	size_t		 i;

	if ( ! (LOWDOWN_STANDALONE & st->flags))
		return;

	/* 
	 * Acquire metadata that we'll fill in.
	 * We format this as well.
	 */

	for (i = 0; i < msz; i++) 
		if (0 == strcmp(m[i].key, "title"))
			title = m[i].value;
		else if (0 == strcmp(m[i].key, "author"))
			author = m[i].value;
		else if (0 == strcmp(m[i].key, "rcsauthor"))
			author = rcsauthor2str(m[i].value);
		else if (0 == strcmp(m[i].key, "css"))
			css = m[i].value;

	HBUF_PUTSL(ob, 
	      "<!DOCTYPE html>\n"
	      "<html>\n"
	      "<head>\n"
	      "<meta charset=\"utf-8\" />\n"
	      "<meta name=\"viewport\" content=\""
	       "width=device-width,initial-scale=1\" />\n");

	if (NULL != author)
		rndr_doc_header_multi(ob, 0, author, 
			"<meta name=\"author\" content=");
	if (NULL != css)
		rndr_doc_header_multi(ob, 1, css, 
			"<link rel=\"stylesheet\" href=");

	/* HTML-escape and trim the title (0-length ok but weird). */

	while (isspace((int)*title))
		title++;

	HBUF_PUTSL(ob, "<title>");
	hesc_html(ob, (const uint8_t *)title, strlen(title), 0);
	HBUF_PUTSL(ob, 
	      "</title>\n"
	      "</head>\n"
	      "<body>\n");
}

void
lowdown_html_rndr(hbuf *ob, void *ref, struct lowdown_node *root)
{
	struct lowdown_node *n;
	hbuf	*tmp;

	tmp = hbuf_new(64);

	TAILQ_FOREACH(n, &root->children, entries)
		lowdown_html_rndr(tmp, ref, n);

	switch (root->type) {
	case (LOWDOWN_BLOCKCODE):
		rndr_blockcode(ob, 
			&root->rndr_blockcode.text, 
			&root->rndr_blockcode.lang);
		break;
	case (LOWDOWN_BLOCKQUOTE):
		rndr_blockquote(ob, tmp);
		break;
	case (LOWDOWN_DOC_HEADER):
		rndr_doc_header(ob, 
			root->rndr_doc_header.m, 
			root->rndr_doc_header.msz, ref);
		break;
	case (LOWDOWN_DOC_FOOTER):
		rndr_doc_footer(ob, ref);
		break;
	case (LOWDOWN_HEADER):
		rndr_header(ob, tmp, 
			root->rndr_header.level, ref);
		break;
	case (LOWDOWN_HRULE):
		rndr_hrule(ob);
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
		rndr_footnotes(ob, tmp);
		break;
	case (LOWDOWN_FOOTNOTE_DEF):
		rndr_footnote_def(ob, tmp, 
			root->rndr_footnote_def.num);
		break;
	case (LOWDOWN_BLOCKHTML):
		rndr_raw_block(ob, 
			&root->rndr_blockhtml.text, ref);
		break;
	case (LOWDOWN_LINK_AUTO):
		rndr_autolink(ob, 
			&root->rndr_autolink.link,
			root->rndr_autolink.type);
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
		rndr_image(ob, 
			&root->rndr_image.link,
			&root->rndr_image.title,
			&root->rndr_image.dims,
			&root->rndr_image.alt);
		break;
	case (LOWDOWN_LINEBREAK):
		rndr_linebreak(ob);
		break;
	case (LOWDOWN_LINK):
		rndr_link(ob, tmp,
			&root->rndr_link.link,
			&root->rndr_link.title);
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
		rndr_footnote_ref(ob, 
			root->rndr_footnote_ref.num);
		break;
	case (LOWDOWN_MATH_BLOCK):
		rndr_math(ob, tmp, root->rndr_math.displaymode);
		break;
	case (LOWDOWN_RAW_HTML):
		rndr_raw_html(ob, tmp, ref);
		break;
	case (LOWDOWN_NORMAL_TEXT):
		rndr_normal_text(ob, &root->rndr_normal_text.text);
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
}

/* allocates a regular HTML renderer */
void *
lowdown_html_new(const struct lowdown_opts *opts)
{
	struct hstate *state;

	state = xcalloc(1, sizeof(struct hstate));

	TAILQ_INIT(&state->headers_used);
	state->flags = NULL == opts ? 0 : opts->oflags;

	return state;
}

/* 
 * Deallocate an HTML renderer. 
 */
void
lowdown_html_free(void *renderer)
{
	struct hstate	*state = renderer;
	struct hentry	*hentry;

	while (NULL != (hentry = TAILQ_FIRST(&state->headers_used))) {
		TAILQ_REMOVE(&state->headers_used, hentry, entries);
		free(hentry->str);
		free(hentry);
	}

	free(state);
}
