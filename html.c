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

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
struct 	html {
	TAILQ_HEAD(, hentry) headers_used;
	unsigned int flags; /* same as "oflags" in lowdown_opts */
};

/*
 * Escape regular text that shouldn't be HTML.
 */
static void
escape_html(hbuf *ob, const char *source,
	size_t length, const struct html *st)
{

	hesc_html(ob, source, length, 
		(st->flags & LOWDOWN_HTML_OWASP),
		0,
		(st->flags & LOWDOWN_HTML_NUM_ENT));
}

/*
 * Escape literal text.
 * This is the same as escaping regular text except a bit more
 * restrictive in what we encode.
 */
static void
escape_literal(hbuf *ob, const char *source,
	size_t length, const struct html *st)
{

	hesc_html(ob, source, length, 
		(st->flags & LOWDOWN_HTML_OWASP),
		1,
		(st->flags & LOWDOWN_HTML_NUM_ENT));
}

static void
rndr_autolink(hbuf *ob, const hbuf *link,
	enum halink_type type, const struct html *st)
{

	if (link->size == 0)
		return;

	HBUF_PUTSL(ob, "<a href=\"");
	if (type == HALINK_EMAIL)
		HBUF_PUTSL(ob, "mailto:");
	hesc_href(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\">");

	/*
	 * Pretty printing: if we get an email address as
	 * an actual URI, e.g. `mailto:foo@bar.com`, we don't
	 * want to print the `mailto:` prefix
	 */

	if (hbuf_strprefix(link, "mailto:"))
		escape_html(ob, link->data + 7, link->size - 7, st);
	else
		escape_html(ob, link->data, link->size, st);

	HBUF_PUTSL(ob, "</a>");
}

static void
rndr_blockcode(hbuf *ob, const hbuf *text,
	const hbuf *lang, const struct html *st)
{
	if (ob->size) 
		hbuf_putc(ob, '\n');

	if (lang->size) {
		HBUF_PUTSL(ob, "<pre><code class=\"language-");
		hesc_href(ob, lang->data, lang->size);
		HBUF_PUTSL(ob, "\">");
	} else
		HBUF_PUTSL(ob, "<pre><code>");

	escape_literal(ob, text->data, text->size, st);
	HBUF_PUTSL(ob, "</code></pre>\n");
}

static void
rndr_definition_data(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<dd>\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "\n</dd>\n");
}

static void
rndr_definition_title(hbuf *ob, const hbuf *content)
{
	size_t	 sz;

	HBUF_PUTSL(ob, "<dt>");
	if ((sz = content->size) > 0) {
		while (sz && content->data[sz - 1] == '\n')
			sz--;
		hbuf_put(ob, content->data, sz);
	}
	HBUF_PUTSL(ob, "</dt>\n");
}

static void
rndr_definition(hbuf *ob, const hbuf *content)
{
	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<dl>\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</dl>\n");
}

static void
rndr_blockquote(hbuf *ob, const hbuf *content)
{
	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<blockquote>\n");
	hbuf_put(ob, content->data, content->size);
	HBUF_PUTSL(ob, "</blockquote>\n");
}

static void
rndr_codespan(hbuf *ob, const hbuf *text, const struct html *st)
{

	HBUF_PUTSL(ob, "<code>");
	escape_html(ob, text->data, text->size, st);
	HBUF_PUTSL(ob, "</code>");
}

static void
rndr_strikethrough(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<del>");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</del>");
}

static void
rndr_double_emphasis(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<strong>");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</strong>");
}

static void
rndr_emphasis(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<em>");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</em>");
}

static void
rndr_highlight(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<mark>");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</mark>");
}

static void
rndr_linebreak(hbuf *ob)
{

	HBUF_PUTSL(ob, "<br/>\n");
}

/*
 * Given the header with non-empty content "header", fill "ob" with the
 * identifier used for the header.
 * This will reference-count the header so we don't have duplicates.
 */
static void
rndr_header_id(hbuf *ob, const hbuf *header, struct html *state)
{
	struct hentry	*hentry;

	/* 
	 * See if the header was previously already defind. 
	 * Note that in HTML5, the identifier is case sensitive.
	 */

	TAILQ_FOREACH(hentry, &state->headers_used, entries) {
		if (strlen(hentry->str) != header->size)
			continue;
		if (strncmp(hentry->str, 
		    header->data, header->size) == 0)
			break;
	}

	/* Convert to escaped values. */

	hesc_href(ob, header->data, header->size);

	/*
	 * If we're non-unique, then append a "count" value.
	 * XXX: if we have a header named "foo-2", then two headers
	 * named "foo", we'll inadvertently have a collision.
	 * This is a bit much to keep track of, though...
	 */

	if (hentry != NULL) {
		hentry->count++;
		hbuf_printf(ob, "-%zu", hentry->count);
		return;
	} 

	/* Create new header entry. */

	hentry = xcalloc(1, sizeof(struct hentry));
	hentry->count = 1;
	hentry->str = xstrndup(header->data, header->size);
	TAILQ_INSERT_TAIL(&state->headers_used, hentry, entries);
}

static void
rndr_header(hbuf *ob, const hbuf *content, 
	int level, struct html *state)
{

	if (ob->size)
		hbuf_putc(ob, '\n');

	if (content->size && (state->flags & LOWDOWN_HTML_HEAD_IDS)) {
		hbuf_printf(ob, "<h%d id=\"", level);
		rndr_header_id(ob, content, state);
		HBUF_PUTSL(ob, "\">");
	} else
		hbuf_printf(ob, "<h%d>", level);

	hbuf_putb(ob, content);
	hbuf_printf(ob, "</h%d>\n", level);
}

static void
rndr_link(hbuf *ob, const hbuf *content, const hbuf *link,
	const hbuf *title, const struct html *st)
{

	HBUF_PUTSL(ob, "<a href=\"");
	hesc_href(ob, link->data, link->size);
	if (title->size) {
		HBUF_PUTSL(ob, "\" title=\"");
		hesc_attr(ob, title->data, title->size);
	}
	HBUF_PUTSL(ob, "\">");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</a>");
}

static void
rndr_list(hbuf *ob, const hbuf *content, const struct rndr_list *p)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	if ((p->flags & HLIST_FL_ORDERED)) {
		if (p->start[0] != '\0') 
			hbuf_printf(ob, "<ol start=\"%s\">\n", p->start);
		else
			HBUF_PUTSL(ob, "<ol>\n");
	} else
		HBUF_PUTSL(ob, "<ul>\n");

	hbuf_putb(ob, content);

	if ((p->flags & HLIST_FL_ORDERED))
		HBUF_PUTSL(ob, "</ol>\n");
	else
		HBUF_PUTSL(ob, "</ul>\n");
}

static void
rndr_listitem(hbuf *ob, const hbuf *content,
	const struct lowdown_node *n)
{
	size_t	 size;
	int	 blk = 0;

	/*
	 * If we're in block mode (which can be assigned post factum in
	 * the parser), make sure that we have an extra <p> around
	 * non-block content.
	 */

	if (((n->rndr_listitem.flags & HLIST_FL_DEF) &&
	     n->parent != NULL &&
	     n->parent->parent != NULL &&
	     n->parent->parent->type == LOWDOWN_DEFINITION &&
	     (n->parent->parent->rndr_definition.flags & 
	      HLIST_FL_BLOCK)) ||
	    (!(n->rndr_listitem.flags & HLIST_FL_DEF) &&
	     n->parent != NULL &&
	     n->parent->type == LOWDOWN_LIST &&
	     (n->parent->rndr_list.flags & HLIST_FL_BLOCK))) {
		if (!(hbuf_strprefix(content, "<ul") ||
		      hbuf_strprefix(content, "<ol") ||
		      hbuf_strprefix(content, "<dl") ||
		      hbuf_strprefix(content, "<div") ||
		      hbuf_strprefix(content, "<table") ||
		      hbuf_strprefix(content, "<blockquote") ||
		      hbuf_strprefix(content, "<pre>") ||
		      hbuf_strprefix(content, "<h") ||
		      hbuf_strprefix(content, "<p>")))
			blk = 1;
	}

	/* Only emit <li> if we're not a <dl> list. */

	if (!(n->rndr_listitem.flags & HLIST_FL_DEF))
		HBUF_PUTSL(ob, "<li>");
	if (blk)
		HBUF_PUTSL(ob, "<p>");

	/* Cut off any trailing space. */

	if ((size = content->size) > 0) {
		while (size && content->data[size - 1] == '\n')
			size--;
		hbuf_put(ob, content->data, size);
	}

	if (blk)
		HBUF_PUTSL(ob, "</p>");
	if (!(n->rndr_listitem.flags & HLIST_FL_DEF))
		HBUF_PUTSL(ob, "</li>\n");
}

static void
rndr_paragraph(hbuf *ob, const hbuf *content, struct html *state)
{
	size_t	i = 0, org;

	if (ob->size) 
		hbuf_putc(ob, '\n');

	if (content->size == 0)
		return;

	while (i < content->size &&
	       isspace((unsigned char)content->data[i])) 
		i++;

	if (i == content->size)
		return;

	HBUF_PUTSL(ob, "<p>");
	if (state->flags & LOWDOWN_HTML_HARD_WRAP) {
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

	HBUF_PUTSL(ob, "</p>\n");
}

static void
rndr_raw_block(hbuf *ob, const hbuf *text, const struct html *st)
{
	size_t	org, sz;

	if ((st->flags & LOWDOWN_HTML_SKIP_HTML))
		return;
	if ((st->flags & LOWDOWN_HTML_ESCAPE)) {
		escape_html(ob, text->data, text->size, st);
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

static void
rndr_triple_emphasis(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<strong><em>");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</em></strong>");
}

static void
rndr_hrule(hbuf *ob)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	hbuf_puts(ob, "<hr/>\n");
}

static void
rndr_image(hbuf *ob, const hbuf *link, const hbuf *title, 
	const hbuf *dims, const hbuf *alt, const struct html *st)
{
	char	dimbuf[32];
	int	x, y, rc = 0;

	/*
	 * Scan in our dimensions, if applicable.
	 * It's unreasonable for them to be over 32 characters, so use
	 * that as a cap to the size.
	 */

	if (dims->size && dims->size < sizeof(dimbuf) - 1) {
		memset(dimbuf, 0, sizeof(dimbuf));
		memcpy(dimbuf, dims->data, dims->size);
		rc = sscanf(dimbuf, "%ux%u", &x, &y);
	}

	/* Require an "alt", even if blank. */

	HBUF_PUTSL(ob, "<img src=\"");
	hesc_href(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\" alt=\"");
	hesc_attr(ob, alt->data, alt->size);
	HBUF_PUTSL(ob, "\"");

	if (dims->size && rc > 0) {
		hbuf_printf(ob, " width=\"%u\"", x);
		if (rc > 1)
			hbuf_printf(ob, " height=\"%u\"", y);
	}

	if (title->size) {
		HBUF_PUTSL(ob, " title=\"");
		escape_html(ob, title->data, title->size, st); 
		HBUF_PUTSL(ob, "\"");
	}

	hbuf_puts(ob, " />");
}

static void
rndr_raw_html(hbuf *ob, const hbuf *text, const struct html *st)
{

	if ((st->flags & LOWDOWN_HTML_SKIP_HTML))
		return;
	if ((st->flags & LOWDOWN_HTML_ESCAPE))
		escape_html(ob, text->data, text->size, st);
	else
		hbuf_putb(ob, text);
}

static void
rndr_table(hbuf *ob, const hbuf *content)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<table>\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</table>\n");
}

static void
rndr_table_header(hbuf *ob, const hbuf *content, 
	const enum htbl_flags *fl, size_t columns)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<thead>\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</thead>\n");
}

static void
rndr_table_body(hbuf *ob, const hbuf *content)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<tbody>\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</tbody>\n");
}

static void
rndr_tablerow(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<tr>\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</tr>\n");
}

static void
rndr_tablecell(hbuf *ob, const hbuf *content, 
	enum htbl_flags flags, size_t col, size_t columns)
{

	if ((flags & HTBL_FL_HEADER))
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

	hbuf_putb(ob, content);

	if ((flags & HTBL_FL_HEADER))
		HBUF_PUTSL(ob, "</th>\n");
	else
		HBUF_PUTSL(ob, "</td>\n");
}

static void
rndr_superscript(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "<sup>");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "</sup>");
}

static void
rndr_normal_text(hbuf *ob, const hbuf *content,
	const struct html *st)
{

	escape_html(ob, content->data, content->size, st);
}

static void
rndr_footnotes(hbuf *ob, const hbuf *content)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "<div class=\"footnotes\">\n");
	hbuf_puts(ob, "<hr/>\n");
	HBUF_PUTSL(ob, "<ol>\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "\n</ol>\n</div>\n");
}

static void
rndr_footnote_def(hbuf *ob, const hbuf *content, size_t num)
{
	size_t	i = 0;
	int	pfound = 0;

	/* Insert anchor at the end of first paragraph block. */

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

	hbuf_printf(ob, "\n<li id=\"fn%zu\">\n", num);

	if (pfound) {
		hbuf_put(ob, content->data, i);
		hbuf_printf(ob, "&#160;"
			"<a href=\"#fnref%zu\" rev=\"footnote\">"
			"&#8617;</a>", num);
		hbuf_put(ob, content->data + i, content->size - i);
	} else 
		hbuf_putb(ob, content);

	HBUF_PUTSL(ob, "</li>\n");
}

static void
rndr_footnote_ref(hbuf *ob, size_t num)
{

	hbuf_printf(ob, 
		"<sup id=\"fnref%zu\">"
		"<a href=\"#fn%zu\" rel=\"footnote\">"
		"%zu</a></sup>", num, num, num);
}

static void
rndr_math(hbuf *ob, const struct rndr_math *n,
	const struct html *st)
{

	if (n->blockmode)
		HBUF_PUTSL(ob, "\\[");
	else
		HBUF_PUTSL(ob, "\\(");

	escape_html(ob, n->text.data, n->text.size, st);

	if (n->blockmode)
		HBUF_PUTSL(ob, "\\]");
	else
		HBUF_PUTSL(ob, "\\)");
}

static void
rndr_doc_footer(hbuf *ob, const struct html *st)
{

	if ((st->flags & LOWDOWN_STANDALONE))
		HBUF_PUTSL(ob, "</body>\n");
}

static void
rndr_root(hbuf *ob, const hbuf *content, const struct html *st)
{

	if ((st->flags & LOWDOWN_STANDALONE))
		HBUF_PUTSL(ob, 
			"<!DOCTYPE html>\n"
			"<html>\n");
	hbuf_putb(ob, content);
	if ((st->flags & LOWDOWN_STANDALONE))
		HBUF_PUTSL(ob, "</html>\n");
}

/*
 * Split "val" into multiple strings delimited by two or more whitespace
 * characters, padding the output with "starttag" and "endtag".
 */
static void
rndr_meta_multi(hbuf *ob, const char *b,
	const char *starttag, const char *endtag)
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

		hbuf_puts(ob, starttag);
		HBUF_PUTSL(ob, "\"");
		hbuf_put(ob, start, sz);
		HBUF_PUTSL(ob, "\"");
		hbuf_puts(ob, endtag);
		HBUF_PUTSL(ob, "\n");
	}
}

static void
rndr_meta(hbuf *ob, const hbuf *tmp, struct lowdown_metaq *mq,
	const struct lowdown_node *n)
{
	struct lowdown_meta	*m;

	m = xcalloc(1, sizeof(struct lowdown_meta));
	TAILQ_INSERT_TAIL(mq, m, entries);
	m->key = xstrndup(n->rndr_meta.key.data,
		n->rndr_meta.key.size);
	m->value = xstrndup(tmp->data, tmp->size);
}

static void
rndr_doc_header(hbuf *ob, const hbuf *content,
	const struct lowdown_metaq *mq, const struct html *st)
{
	const struct lowdown_meta	*m;
	const char			*author = NULL, *title = NULL,
					*affil = NULL, *date = NULL,
					*copy = NULL, *rcsauthor = NULL, 
					*rcsdate = NULL, *css = NULL,
					*script = NULL;

	if (!(st->flags & LOWDOWN_STANDALONE))
		return;

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
		else if (strcasecmp(m->key, "css") == 0)
			css = m->value;
		else if (strcasecmp(m->key, "javascript") == 0)
			script = m->value;

	hbuf_putb(ob, content);

	HBUF_PUTSL(ob, 
	      "<head>\n"
	      "<meta charset=\"utf-8\" />\n"
	      "<meta name=\"viewport\""
	      " content=\"width=device-width,initial-scale=1\" />\n");

	/* Overrides. */

	if (title == NULL)
		title = "Untitled article";
	if (rcsdate != NULL)
		date = rcsdate;
	if (rcsauthor != NULL)
		author = rcsauthor;

	if (affil != NULL)
		rndr_meta_multi(ob, affil, 
			"<meta name=\"creator\" content=", " />");
	if (author != NULL)
		rndr_meta_multi(ob, author, 
			"<meta name=\"author\" content=", " />");
	if (copy != NULL)
		rndr_meta_multi(ob, copy, 
			"<meta name=\"copyright\" content=", " />");
	if (css != NULL)
		rndr_meta_multi(ob, css, 
			"<link rel=\"stylesheet\" href=", " />");
	if (date != NULL) {
		hbuf_printf(ob, "<meta name=\"date\" "
			"scheme=\"YYYY-MM-DD\" content=\"");
		hbuf_puts(ob, date);
		HBUF_PUTSL(ob, "\" />\n");
	}
	if (script != NULL)
		rndr_meta_multi(ob, script, 
			"<script src=", "></script>");

	HBUF_PUTSL(ob, "<title>");
	hbuf_puts(ob, title);
	HBUF_PUTSL(ob, "</title>\n");
	HBUF_PUTSL(ob, "</head>\n<body>\n");
}

void
lowdown_html_rndr(hbuf *ob, struct lowdown_metaq *mq,
	void *ref, const struct lowdown_node *n)
{
	const struct lowdown_node	*child;
	struct lowdown_metaq		 metaq;
	hbuf				*tmp;
	int32_t				 ent;
	struct html			*st = ref;

	/* Temporary metaq if not provided. */

	if (mq == NULL) {
		TAILQ_INIT(&metaq);
		mq = &metaq;
	}

	tmp = hbuf_new(64);

	TAILQ_FOREACH(child, &n->children, entries)
		lowdown_html_rndr(tmp, mq, st, child);

	/*
	 * These elements can be put in either a block or an inline
	 * context, so we're safe to just use them and forget.
	 */

	if (n->chng == LOWDOWN_CHNG_INSERT)
		HBUF_PUTSL(ob, "<ins>");
	if (n->chng == LOWDOWN_CHNG_DELETE)
		HBUF_PUTSL(ob, "<del>");

	switch (n->type) {
	case LOWDOWN_ROOT:
		rndr_root(ob, tmp, st);
		break;
	case LOWDOWN_BLOCKCODE:
		rndr_blockcode(ob, 
			&n->rndr_blockcode.text, 
			&n->rndr_blockcode.lang, st);
		break;
	case LOWDOWN_BLOCKQUOTE:
		rndr_blockquote(ob, tmp);
		break;
	case LOWDOWN_DEFINITION:
		rndr_definition(ob, tmp);
		break;
	case LOWDOWN_DEFINITION_TITLE:
		rndr_definition_title(ob, tmp);
		break;
	case LOWDOWN_DEFINITION_DATA:
		rndr_definition_data(ob, tmp);
		break;
	case LOWDOWN_DOC_HEADER:
		rndr_doc_header(ob, tmp, mq, st);
		break;
	case LOWDOWN_META:
		rndr_meta(ob, tmp, mq, n);
		break;
	case LOWDOWN_DOC_FOOTER:
		rndr_doc_footer(ob, st);
		break;
	case LOWDOWN_HEADER:
		rndr_header(ob, tmp, 
			n->rndr_header.level, st);
		break;
	case LOWDOWN_HRULE:
		rndr_hrule(ob);
		break;
	case LOWDOWN_LIST:
		rndr_list(ob, tmp, &n->rndr_list);
		break;
	case LOWDOWN_LISTITEM:
		rndr_listitem(ob, tmp, n);
		break;
	case LOWDOWN_PARAGRAPH:
		rndr_paragraph(ob, tmp, st);
		break;
	case LOWDOWN_TABLE_BLOCK:
		rndr_table(ob, tmp);
		break;
	case LOWDOWN_TABLE_HEADER:
		rndr_table_header(ob, tmp, 
			n->rndr_table_header.flags,
			n->rndr_table_header.columns);
		break;
	case LOWDOWN_TABLE_BODY:
		rndr_table_body(ob, tmp);
		break;
	case LOWDOWN_TABLE_ROW:
		rndr_tablerow(ob, tmp);
		break;
	case LOWDOWN_TABLE_CELL:
		rndr_tablecell(ob, tmp, 
			n->rndr_table_cell.flags, 
			n->rndr_table_cell.col,
			n->rndr_table_cell.columns);
		break;
	case LOWDOWN_FOOTNOTES_BLOCK:
		rndr_footnotes(ob, tmp);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rndr_footnote_def(ob, tmp, 
			n->rndr_footnote_def.num);
		break;
	case LOWDOWN_BLOCKHTML:
		rndr_raw_block(ob, 
			&n->rndr_blockhtml.text, st);
		break;
	case LOWDOWN_LINK_AUTO:
		rndr_autolink(ob, 
			&n->rndr_autolink.link,
			n->rndr_autolink.type, st);
		break;
	case LOWDOWN_CODESPAN:
		rndr_codespan(ob, 
			&n->rndr_codespan.text, st);
		break;
	case LOWDOWN_DOUBLE_EMPHASIS:
		rndr_double_emphasis(ob, tmp);
		break;
	case LOWDOWN_EMPHASIS:
		rndr_emphasis(ob, tmp);
		break;
	case LOWDOWN_HIGHLIGHT:
		rndr_highlight(ob, tmp);
		break;
	case LOWDOWN_IMAGE:
		rndr_image(ob, 
			&n->rndr_image.link,
			&n->rndr_image.title,
			&n->rndr_image.dims,
			&n->rndr_image.alt, st);
		break;
	case LOWDOWN_LINEBREAK:
		rndr_linebreak(ob);
		break;
	case LOWDOWN_LINK:
		rndr_link(ob, tmp,
			&n->rndr_link.link,
			&n->rndr_link.title, st);
		break;
	case LOWDOWN_TRIPLE_EMPHASIS:
		rndr_triple_emphasis(ob, tmp);
		break;
	case LOWDOWN_STRIKETHROUGH:
		rndr_strikethrough(ob, tmp);
		break;
	case LOWDOWN_SUPERSCRIPT:
		rndr_superscript(ob, tmp);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		rndr_footnote_ref(ob, 
			n->rndr_footnote_ref.num);
		break;
	case LOWDOWN_MATH_BLOCK:
		rndr_math(ob, &n->rndr_math, st);
		break;
	case LOWDOWN_RAW_HTML:
		rndr_raw_html(ob, &n->rndr_raw_html.text, st);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rndr_normal_text(ob,
			&n->rndr_normal_text.text, st);
		break;
	case LOWDOWN_ENTITY:
		if (!(st->flags & LOWDOWN_HTML_NUM_ENT)) {
			hbuf_put(ob,
				n->rndr_entity.text.data,
				n->rndr_entity.text.size);
			break;
		}

		/*
		 * Prefer numeric entities.
		 * This is because we're emitting XML (XHTML5) and it's
		 * not clear whether the processor can handle HTML
		 * entities.
		 */

		ent = entity_find_iso(&n->rndr_entity.text);
		if (ent > 0)
			hbuf_printf(ob, "&#%" PRId32 ";", ent);
		else
			hbuf_putb(ob, &n->rndr_entity.text);
		break;
	default:
		hbuf_put(ob, tmp->data, tmp->size);
		break;
	}

	if (n->chng == LOWDOWN_CHNG_INSERT)
		HBUF_PUTSL(ob, "</ins>");
	if (n->chng == LOWDOWN_CHNG_DELETE)
		HBUF_PUTSL(ob, "</del>");

	hbuf_free(tmp);

	/* Release temporary metaq. */

	if (mq == &metaq)
		lowdown_metaq_free(mq);
}

void *
lowdown_html_new(const struct lowdown_opts *opts)
{
	struct html *st;

	st = xcalloc(1, sizeof(struct html));

	TAILQ_INIT(&st->headers_used);
	st->flags = NULL == opts ? 0 : opts->oflags;

	return st;
}

void
lowdown_html_free(void *arg)
{
	struct html	*st = arg;
	struct hentry	*hentry;

	while (NULL != (hentry = TAILQ_FIRST(&st->headers_used))) {
		TAILQ_REMOVE(&st->headers_used, hentry, entries);
		free(hentry->str);
		free(hentry);
	}

	free(st);
}
