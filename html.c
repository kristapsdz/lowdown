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
struct 	hstate {
	TAILQ_HEAD(, hentry) headers_used;
	unsigned int flags; /* output flags */
};

enum rndr_meta_key {
	RNDR_META_AFFIL,
	RNDR_META_AUTHOR,
	RNDR_META_CSS,
	RNDR_META_DATE,
	RNDR_META_RCSAUTHOR,
	RNDR_META_RCSDATE,
	RNDR_META_SCRIPT,
	RNDR_META_TITLE,
	RNDR_META__MAX
};

static const char *rndr_meta_keys[RNDR_META__MAX] = {
	"affiliation", /* RNDR_META_AFFIL */
	"author", /* RNDR_META_AUTHOR */
	"css", /* RNDR_META_CSS */
	"date", /* RNDR_META_DATE */
	"rcsauthor", /* RNDR_META_RCSAUTHOR */
	"rcsdate", /* RNDR_META_RCSDATE */
	"javascript", /* RNDR_META_SCRIPT */
	"title", /* RNDR_META_TITLE */
};

/*
 * Escape regular text that shouldn't be HTML.
 */
static void
escape_html(hbuf *ob, const char *source,
	size_t length, const struct hstate *st)
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
	size_t length, const struct hstate *st)
{

	hesc_html(ob, source, length, 
		(st->flags & LOWDOWN_HTML_OWASP),
		1,
		(st->flags & LOWDOWN_HTML_NUM_ENT));
}

/*
 * Except URLs.
 * Don't use this for HTML attributes!
 */
static void
escape_href(hbuf *ob, const char *source, size_t length)
{

	hesc_href(ob, source, length);
}

static void
rndr_autolink(hbuf *ob, const hbuf *link,
	enum halink_type type, const struct hstate *st)
{

	if (link->size == 0)
		return;

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

	if (hbuf_prefix(link, "mailto:") == 0)
		escape_html(ob, link->data + 7, link->size - 7, st);
	else
		escape_html(ob, link->data, link->size, st);

	HBUF_PUTSL(ob, "</a>");
}

static void
rndr_blockcode(hbuf *ob, const hbuf *text,
	const hbuf *lang, const struct hstate *st)
{
	if (ob->size) 
		hbuf_putc(ob, '\n');

	if (lang->size) {
		HBUF_PUTSL(ob, "<pre><code class=\"language-");
		escape_href(ob, lang->data, lang->size);
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
rndr_codespan(hbuf *ob, const hbuf *text, const struct hstate *st)
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
		if (strncmp(hentry->str, 
		    header->data, header->size) == 0)
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
	int level, struct hstate *state)
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
	const hbuf *title, const struct hstate *st)
{

	HBUF_PUTSL(ob, "<a href=\"");
	escape_href(ob, link->data, link->size);
	if (title->size) {
		HBUF_PUTSL(ob, "\" title=\"");
		escape_html(ob, title->data, title->size, st);
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
	const struct rndr_listitem *p)
{
	size_t	 size;

	/* Only emit <li> if we're not a <dl> list. */

	if (!(p->flags & HLIST_FL_DEF))
		HBUF_PUTSL(ob, "<li>");

	/* Cut off any trailing space. */

	if ((size = content->size) > 0) {
		while (size && content->data[size - 1] == '\n')
			size--;
		hbuf_put(ob, content->data, size);
	}

	if (!(p->flags & HLIST_FL_DEF))
		HBUF_PUTSL(ob, "</li>\n");
}

static void
rndr_paragraph(hbuf *ob, const hbuf *content, struct hstate *state)
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
rndr_raw_block(hbuf *ob, const hbuf *text, const struct hstate *st)
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
	const hbuf *dims, const hbuf *alt, const struct hstate *st)
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
	escape_href(ob, link->data, link->size);
	HBUF_PUTSL(ob, "\" alt=\"");
	escape_html(ob, alt->data, alt->size, st);
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
rndr_raw_html(hbuf *ob, const hbuf *text, const struct hstate *st)
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
	const struct hstate *st)
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
rndr_footnote_def(hbuf *ob, const hbuf *content, unsigned int num)
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

	hbuf_printf(ob, "\n<li id=\"fn%d\">\n", num);

	if (pfound) {
		hbuf_put(ob, content->data, i);
		hbuf_printf(ob, "&#160;"
			"<a href=\"#fnref%d\" rev=\"footnote\">"
			"&#8617;</a>", num);
		hbuf_put(ob, content->data + i, content->size - i);
	} else 
		hbuf_putb(ob, content);

	HBUF_PUTSL(ob, "</li>\n");
}

static void
rndr_footnote_ref(hbuf *ob, unsigned int num)
{

	hbuf_printf(ob, 
		"<sup id=\"fnref%d\">"
		"<a href=\"#fn%d\" rel=\"footnote\">"
		"%d</a></sup>", num, num, num);
}

static void
rndr_math(hbuf *ob, const struct rndr_math *n,
	const struct hstate *st)
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
rndr_doc_footer(hbuf *ob, const struct hstate *st)
{

	if ((st->flags & LOWDOWN_STANDALONE))
		HBUF_PUTSL(ob, "</body>\n");
}

static void
rndr_root(hbuf *ob, const hbuf *content, const struct hstate *st)
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
rndr_doc_header_multi(hbuf *ob, const hbuf *b,
	const char *starttag, const char *endtag)
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
	const struct lowdown_node *n, const struct hstate *st)
{
	enum rndr_meta_key	 key;
	struct lowdown_meta	*m;

	if (mq != NULL) {
		m = xcalloc(1, sizeof(struct lowdown_meta));
		TAILQ_INSERT_TAIL(mq, m, entries);
		m->key = xstrndup(n->rndr_meta.key.data,
			n->rndr_meta.key.size);
		m->value = xstrndup(tmp->data, tmp->size);
	}

	if (!(st->flags & LOWDOWN_STANDALONE))
		return;

	for (key = 0; key < RNDR_META__MAX; key++)
		if (hbuf_streq(&n->rndr_meta.key, rndr_meta_keys[key]))
			break;

	/* TODO: rcsauthor, rcsdate. */

	switch (key) {
	case RNDR_META_AFFIL:
		rndr_doc_header_multi(ob, tmp, 
			"<meta name=\"creator\" content=", 
			" />");
		break;
	case RNDR_META_AUTHOR:
		rndr_doc_header_multi(ob, tmp, 
			"<meta name=\"author\" content=", 
			" />");
		break;
	case RNDR_META_CSS:
		rndr_doc_header_multi(ob, tmp, 
			"<link rel=\"stylesheet\" href=", 
			" />");
		break;
	case RNDR_META_DATE:
		hbuf_printf(ob, "<meta name=\"date\" "
			"scheme=\"YYYY-MM-DD\" content=\"");
		hbuf_putb(ob, tmp);
		HBUF_PUTSL(ob, "\" />\n");
		break;
	case RNDR_META_SCRIPT:
		rndr_doc_header_multi(ob, tmp, 
			"<script src=", 
			"></script>");
		break;
	case RNDR_META_TITLE:
		HBUF_PUTSL(ob, "<title>");
		hbuf_putb(ob, tmp);
		HBUF_PUTSL(ob, "</title>\n");
		break;
	default:
		break;
	};
}

static void
rndr_doc_header(hbuf *ob, const hbuf *content,
	const struct hstate *st, const struct lowdown_node *n)
{
	struct lowdown_node	*nn;

	if (!(LOWDOWN_STANDALONE & st->flags))
		return;

	HBUF_PUTSL(ob, 
	      "<head>\n"
	      "<meta charset=\"utf-8\" />\n"
	      "<meta name=\"viewport\""
	      " content=\"width=device-width,initial-scale=1\" />\n");

	hbuf_putb(ob, content);

	/* If we don't have a title, print out a default one. */

	TAILQ_FOREACH(nn, &n->children, entries) {
		if (nn->type != LOWDOWN_META)
			continue;
		if (hbuf_streq(&nn->rndr_meta.key, "title"))
			break;
	}
	if (nn == NULL)
		hbuf_puts(ob, "<title>Untitled Article</title>");

	HBUF_PUTSL(ob, "</head>\n<body>\n");
}

void
lowdown_html_rndr(hbuf *ob, struct lowdown_metaq *metaq,
	void *ref, const struct lowdown_node *root)
{
	const struct lowdown_node	*n;
	hbuf			*tmp;
	int32_t			 ent;
	struct hstate		*st = ref;

	tmp = hbuf_new(64);

	TAILQ_FOREACH(n, &root->children, entries)
		lowdown_html_rndr(tmp, metaq, st, n);

	/*
	 * These elements can be put in either a block or an inline
	 * context, so we're safe to just use them and forget.
	 */

	if (root->chng == LOWDOWN_CHNG_INSERT)
		HBUF_PUTSL(ob, "<ins>");
	if (root->chng == LOWDOWN_CHNG_DELETE)
		HBUF_PUTSL(ob, "<del>");

	switch (root->type) {
	case LOWDOWN_ROOT:
		rndr_root(ob, tmp, st);
		break;
	case LOWDOWN_BLOCKCODE:
		rndr_blockcode(ob, 
			&root->rndr_blockcode.text, 
			&root->rndr_blockcode.lang, st);
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
		rndr_doc_header(ob, tmp, st, root);
		break;
	case LOWDOWN_META:
		rndr_meta(ob, tmp, metaq, root, st);
		break;
	case LOWDOWN_DOC_FOOTER:
		rndr_doc_footer(ob, st);
		break;
	case LOWDOWN_HEADER:
		rndr_header(ob, tmp, 
			root->rndr_header.level, st);
		break;
	case LOWDOWN_HRULE:
		rndr_hrule(ob);
		break;
	case LOWDOWN_LIST:
		rndr_list(ob, tmp, &root->rndr_list);
		break;
	case LOWDOWN_LISTITEM:
		rndr_listitem(ob, tmp, &root->rndr_listitem);
		break;
	case LOWDOWN_PARAGRAPH:
		rndr_paragraph(ob, tmp, st);
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
			root->rndr_table_cell.flags, 
			root->rndr_table_cell.col,
			root->rndr_table_cell.columns);
		break;
	case LOWDOWN_FOOTNOTES_BLOCK:
		rndr_footnotes(ob, tmp);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rndr_footnote_def(ob, tmp, 
			root->rndr_footnote_def.num);
		break;
	case LOWDOWN_BLOCKHTML:
		rndr_raw_block(ob, 
			&root->rndr_blockhtml.text, st);
		break;
	case LOWDOWN_LINK_AUTO:
		rndr_autolink(ob, 
			&root->rndr_autolink.link,
			root->rndr_autolink.type, st);
		break;
	case LOWDOWN_CODESPAN:
		rndr_codespan(ob, 
			&root->rndr_codespan.text, st);
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
			&root->rndr_image.link,
			&root->rndr_image.title,
			&root->rndr_image.dims,
			&root->rndr_image.alt, st);
		break;
	case LOWDOWN_LINEBREAK:
		rndr_linebreak(ob);
		break;
	case LOWDOWN_LINK:
		rndr_link(ob, tmp,
			&root->rndr_link.link,
			&root->rndr_link.title, st);
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
			root->rndr_footnote_ref.num);
		break;
	case LOWDOWN_MATH_BLOCK:
		rndr_math(ob, &root->rndr_math, st);
		break;
	case LOWDOWN_RAW_HTML:
		rndr_raw_html(ob, &root->rndr_raw_html.text, st);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rndr_normal_text(ob,
			&root->rndr_normal_text.text, st);
		break;
	case LOWDOWN_ENTITY:
		if (!(st->flags & LOWDOWN_HTML_NUM_ENT)) {
			hbuf_put(ob,
				root->rndr_entity.text.data,
				root->rndr_entity.text.size);
			break;
		}

		/*
		 * Prefer numeric entities.
		 * This is because we're emitting XML (XHTML5) and it's
		 * not clear whether the processor can handle HTML
		 * entities.
		 */

		ent = entity_find(&root->rndr_entity.text);
		if (ent > 0)
			hbuf_printf(ob, "&#%lld;", (long long)ent);
		else
			hbuf_put(ob,
				root->rndr_entity.text.data,
				root->rndr_entity.text.size);
		break;
	default:
		hbuf_put(ob, tmp->data, tmp->size);
		break;
	}

	if (root->chng == LOWDOWN_CHNG_INSERT)
		HBUF_PUTSL(ob, "</ins>");
	if (root->chng == LOWDOWN_CHNG_DELETE)
		HBUF_PUTSL(ob, "</del>");

	hbuf_free(tmp);
}

void *
lowdown_html_new(const struct lowdown_opts *opts)
{
	struct hstate *st;

	st = xcalloc(1, sizeof(struct hstate));

	TAILQ_INIT(&st->headers_used);
	st->flags = NULL == opts ? 0 : opts->oflags;

	return st;
}

void
lowdown_html_free(void *arg)
{
	struct hstate	*st = arg;
	struct hentry	*hentry;

	while (NULL != (hentry = TAILQ_FIRST(&st->headers_used))) {
		TAILQ_REMOVE(&st->headers_used, hentry, entries);
		free(hentry->str);
		free(hentry);
	}

	free(st);
}
