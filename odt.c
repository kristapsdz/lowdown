/*
 * Copyright (c) 2021 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lowdown.h"
#include "extern.h"

/*
 * A style in <office:styles> or <office-automatic-styles>.  The
 * difference between these two, according to section 3.15 of the v1.3,
 * is that automatic styles are ad hoc and regular styles are linked to
 * a central style that may be changed.  Span styles are in-line, blocks
 * can have offsets.
 */
struct	odt_sty {
	char			 name[128]; /* name */
	size_t			 offs; /* offset ("tabs") from zero */
	size_t			 parent; /* list parent or (size_t)-1*/
	enum lowdown_rndrt	 type; /* node type of style */
	int			 span; /* span/block? */
	int			 autosty; /* automatic-style? */
};

/*
 * Our internal state object.  Beyond retaining our flags, this also
 * keeps output state in terms of the styles that need printing.
 */
struct 	odt {
	ssize_t			 headers_offs; /* header offset */
	unsigned int 		 flags; /* "oflags" in lowdown_opts */
	struct odt_sty		*stys; /* styles for content */
	size_t			 stysz; /* number of styles */
	size_t			 offs; /* offs or (size_t)-1 in list */
	size_t			 list; /* root list or (size_t)-1 */
};

/*
 * Append a new zeroed style with an unset parent.  Return NULL on
 * memory failure or the new style.
 */
static struct odt_sty *
odt_style_add(struct odt *st)
{
	void		*pp;

	pp = reallocarray(st->stys,
		st->stysz + 1, sizeof(struct odt_sty));
	if (pp == NULL)
		return NULL;
	st->stys = pp;
	memset(&st->stys[st->stysz], 0, sizeof(struct odt_sty));
	st->stys[st->stysz].parent = (size_t)-1;
	return &st->stys[st->stysz++];
}

/*
 * Create or fetch an inline style corresponding to the node type.
 * Return NULL on error or the style name on success.
 */
static const char *
odt_style_add_span(struct odt *st, enum lowdown_rndrt type)
{
	size_t		 i;
	struct odt_sty	*s;

	for (i = 0; i < st->stysz; i++)
		if (st->stys[i].type == type) {
			assert(st->stys[i].span);
			return st->stys[i].name;
		}

	if ((s = odt_style_add(st)) == NULL)
		return NULL;

	s->span = 1;
	s->type = type;

	switch (type) {
	case LOWDOWN_CODESPAN:
		strlcpy(s->name, "Source_20_Text", sizeof(s->name));
		break;
	case LOWDOWN_LINK:
		strlcpy(s->name, "Internet_20_link", sizeof(s->name));
		break;
	default:
		s->autosty = 1;
		snprintf(s->name, sizeof(s->name), "T%zu", st->stysz);
		break;
	}
	return s->name;
}

/*
 * Flush out all of the styles and automatic styles.  Return FALSE on
 * failure, TRUE on success.
 */
static int
odt_sty_flush(struct lowdown_buf *ob,
	const struct odt *st, const struct odt_sty *sty)
{
	size_t	 i;

	/* 
	 * Lists and non-lists have a different XML element name, and
	 * non-lists designate whether in-line or paragraphs.
	 */

	switch (sty->type) {
	case LOWDOWN_LIST:
		if (!HBUF_PUTSL(ob, "<text:list-style"))
			return 0;
		break;
	default:
		if (!hbuf_printf(ob,
		    "<style:style style:family=\"%s\"",
		    sty->span ? "text" : "paragraph"))
			return 0;
		break;
	}

	if (!hbuf_printf(ob, " style:name=\"%s\"", sty->name))
		return 0;

	/*
	 * Paragraphs in lists need to link to the list, then set some
	 * other crap found in libreoffice output.
	 */

	switch (sty->type) {
	case LOWDOWN_PARAGRAPH:
		if (!HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Standard\""))
			return 0;
		if (sty->parent != (size_t)-1 && !hbuf_printf(ob,
		    " style:list-style-name=\"%s\"", 
		    st->stys[sty->parent].name))
			return 0;
		break;
	case LOWDOWN_LINK:
		if (!HBUF_PUTSL(ob,
		    " style:display-name=\"Internet link\""))
			return 0;
		break;
	case LOWDOWN_CODESPAN:
		if (!HBUF_PUTSL(ob,
		    " style:display-name=\"Source Text\""))
			return 0;
		break;
	default:
		break;
	}

	if (!HBUF_PUTSL(ob, ">\n"))
		return 0;

	/*
	 * I'm not sure what in this is necessary and what isn't yet.
	 * The template followed is from libreoffice output.
	 */

	switch (sty->type) {
	case LOWDOWN_PARAGRAPH:
		if (sty->offs == 0)
			break;
		if (!hbuf_printf(ob,
		    "<style:paragraph-properties"
		    " fo:margin-left=\"%.3fcm\""
		    " fo:margin-right=\"0cm\""
		    " fo:text-indent=\"0cm\""
		    " style:auto-text-indent=\"false\"/>\n",
		    (1.25 * sty->offs)))
			return 0;
		break;
	case LOWDOWN_LIST:
		for (i = 0; i < 5; i++) 
			if (!hbuf_printf(ob,
			    "<text:list-level-style-bullet"
			    " text:level=\"%zu\""
			    " text:style-name=\"Bullet_20_Symbols\""
			    " text:bullet-char=\"•\">\n"
			    "<style:list-level-properties"
			    " text:list-level-position-and-space-mode=\"label-alignment\">\n"
			    "<style:list-level-label-alignment"
			    " text:label-followed-by=\"listtab\""
			    " text:list-tab-stop-position=\"%.3fcm\""
			    " fo:text-indent=\"-0.635cm\""
			    " fo:margin-left=\"%.3fcm\"/>\n"
			    "</style:list-level-properties>\n"
			    "</text:list-level-style-bullet>\n",
			    i + 1, 
			    (1.25 * sty->offs) + (1.25 * (i + 1)),
			    (1.25 * sty->offs) + (1.25 * (i + 1))))
				return 0;
		break;
	case LOWDOWN_SUPERSCRIPT:
		if (!HBUF_PUTSL(ob,
		    "<style:text-properties"
		    " style:text-position=\"super 58%\"/>\n"))
			return 0;
		break;
	case LOWDOWN_CODESPAN:
		if (!HBUF_PUTSL(ob,
		    "<style:text-properties"
		    " style:font-name=\"Liberation Mono\""
		    " fo:font-family=\"&apos;Liberation Mono&apos;\""
		    " style:font-family-generic=\"modern\""
		    " style:font-pitch=\"fixed\""
		    " style:font-name-asian=\"Liberation Mono\""
		    " style:font-family-asian=\"&apos;Liberation Mono&apos;\""
		    " style:font-family-generic-asian=\"modern\""
		    " style:font-pitch-asian=\"fixed\""
		    " style:font-name-complex=\"Liberation Mono\""
		    " style:font-family-complex=\"&apos;Liberation Mono&apos;\""
		    " style:font-family-generic-complex=\"modern\""
		    " style:font-pitch-complex=\"fixed\"/>\n"))
			return 0;
		break;
	case LOWDOWN_LINK:
		if (!HBUF_PUTSL(ob,
		    "<style:text-properties"
   		    " fo:color=\"#000080\""
		    " loext:opacity=\"100%\""
		    " fo:language=\"zxx\""
		    " fo:country=\"none\""
		    " style:language-asian=\"zxx\""
		    " style:country-asian=\"none\""
		    " style:language-complex=\"zxx\""
		    " style:country-complex=\"none\""
   		    " style:text-underline-style=\"solid\""
   		    " style:text-underline-color=\"font-color\""
		    " style:text-underline-width=\"auto\"/>\n"))
			return 0;
		break;
	case LOWDOWN_TRIPLE_EMPHASIS:
		if (!HBUF_PUTSL(ob,
		    "<style:text-properties"
		    " fo:font-style=\"italic\""
		    " style:font-style-asian=\"italic\""
		    " style:font-style-complex=\"italic\""
		    " fo:font-weight=\"bold\""
		    " style:font-weight-asian=\"bold\""
		    " style:font-weight-complex=\"bold\"/>\n"))
			return 0;
		break;
	case LOWDOWN_DOUBLE_EMPHASIS:
		if (!HBUF_PUTSL(ob,
		    "<style:text-properties"
		    " fo:font-weight=\"bold\""
		    " style:font-weight-asian=\"bold\""
		    " style:font-weight-complex=\"bold\"/>\n"))
			return 0;
		break;
	case LOWDOWN_EMPHASIS:
		if (!HBUF_PUTSL(ob,
		    "<style:text-properties"
		    " fo:font-style=\"italic\""
		    " style:font-style-asian=\"italic\""
		    " style:font-style-complex=\"italic\"/>\n"))
			return 0;
		break;
	case LOWDOWN_STRIKETHROUGH:
		if (!HBUF_PUTSL(ob,
		    "<style:text-properties"
   		    " style:text-line-through-style=\"solid\""
		    " style:text-line-through-type=\"single\"/>\n"))
			return 0;
		break;
	case LOWDOWN_HIGHLIGHT:
		if (!HBUF_PUTSL(ob,
		    "<style:text-properties"
   		    " style:text-underline-style=\"solid\""
   		    " style:text-underline-color=\"font-color\""
		    " style:text-underline-width=\"auto\"/>\n"))
			return 0;
		break;
	default:
		abort();
		/* NOTREACHED */
	}

	return hbuf_printf(ob, "</%s>\n",
		sty->type == LOWDOWN_LIST ? 
		"text:list-style" : "style:style");
}

static int
odt_styles_flush(struct lowdown_buf *ob, const struct odt *st)
{
	size_t	 i;
	int	 xlink = 0;

	if (!HBUF_PUTSL(ob, "<office:styles>\n"))
		return 0;
	for (i = 0; i < st->stysz; i++) {
		if (st->stys[i].type == LOWDOWN_LINK)
			xlink = 1;
		if (!st->stys[i].autosty &&
		    !odt_sty_flush(ob, st, &st->stys[i]))
			return 0;
	}
	if (!HBUF_PUTSL(ob, "</office:styles>\n"))
		return 0;

	if (!HBUF_PUTSL(ob, "<office:automatic-styles>\n"))
		return 0;
	for (i = 0; i < st->stysz; i++) {
		if (st->stys[i].autosty &&
		    !odt_sty_flush(ob, st, &st->stys[i]))
			return 0;
	}
	if (!HBUF_PUTSL(ob, "</office:automatic-styles>\n"))
		return 0;

	/*
	 * This doesn't appear to make a difference if it's specified or
	 * not, but I'm adding it because libreoffice does.
	 */

	if (xlink && !HBUF_PUTSL(ob,
	    "<office:scripts>\n"
	    " <office:script script:language=\"ooo:Basic\">\n"
	    "  <ooo:libraries xmlns:ooo=\"http://openoffice.org/2004/office\""
	    "   xmlns:xlink=\"http://www.w3.org/1999/xlink\"/>\n"
	    " </office:script>\n"
	    "</office:scripts>\n"))
		return 0;

	return 1;
}

/*
 * Escape regular text that shouldn't be HTML.  Return FALSE on failure,
 * TRUE on success.
 */
static int
escape_html(struct lowdown_buf *ob, const char *source,
	size_t length, const struct odt *st)
{

	return hesc_html(ob, source, length, 1, 0, 1);
}

/*
 * See escape_html().
 */
static int
escape_htmlb(struct lowdown_buf *ob, 
	const struct lowdown_buf *in, const struct odt *st)
{

	return escape_html(ob, in->data, in->size, st);
}

/*
 * Escape literal text.  Like escape_html() except more restrictive.
 * Return FALSE on failure, TRUE on success.
 */
static int
escape_literal(struct lowdown_buf *ob, 
	const struct lowdown_buf *in, const struct odt *st)
{

	return hesc_html(ob, in->data, in->size, 1, 1, 1);
}

/*
 * Escape an href link.  Return FALSE on failure, TRUE on success.
 */
static int
escape_href(struct lowdown_buf *ob, const struct lowdown_buf *in,
	const struct odt *st)
{

	return hesc_href(ob, in->data, in->size);
}

/*
 * Escape an HTML attribute.  Return FALSE on failure, TRUE on success.
 */
static int
escape_attr(struct lowdown_buf *ob, const struct lowdown_buf *in)
{

	return hesc_attr(ob, in->data, in->size);
}

static int
rndr_autolink(struct lowdown_buf *ob, 
	const struct rndr_autolink *parm,
	struct odt *st)
{
	const char	*sty;

	if (parm->link.size == 0)
		return 1;

	if ((sty = odt_style_add_span(st, LOWDOWN_LINK)) == NULL)
		return 0;
	if (!hbuf_printf(ob,
	    "<text:a xlink:type=\"simple\""
	    " text:style-name=\"%s\" xlink:href=\"", sty))
		return 0;
	if (parm->type == HALINK_EMAIL && !HBUF_PUTSL(ob, "mailto:"))
		return 0;
	if (!escape_href(ob, &parm->link, st))
		return 0;
	if (!HBUF_PUTSL(ob, "\">"))
		return 0;

	/*
	 * Pretty printing: if we get an email address as
	 * an actual URI, e.g. `mailto:foo@bar.com`, we don't
	 * want to print the `mailto:` prefix
	 */

	if (hbuf_strprefix(&parm->link, "mailto:")) {
		if (!escape_html(ob, 
		    parm->link.data + 7, 
		    parm->link.size - 7, st))
			return 0;
	} else {
		if (!escape_htmlb(ob, &parm->link, st))
			return 0;
	}

	return HBUF_PUTSL(ob, "</text:a>");
}

/* TODO */
static int
rndr_blockcode(struct lowdown_buf *ob, 
	const struct rndr_blockcode *parm,
	const struct odt *st)
{
	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;

	if (parm->lang.size) {
		if (!HBUF_PUTSL(ob, "<pre><code class=\"language-"))
			return 0;
		if (!escape_href(ob, &parm->lang, st))
			return 0;
		if (!HBUF_PUTSL(ob, "\">"))
			return 0;
	} else {
		if (! HBUF_PUTSL(ob, "<pre><code>"))
			return 0;
	}

	if (!escape_literal(ob, &parm->text, st))
		return 0;
	return HBUF_PUTSL(ob, "</code></pre>\n");
}

static int
rndr_definition_data(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "<text:p text:style-name=\"dd\">\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "\n</text:p>\n");
}

static int
rndr_definition_title(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{
	size_t	 sz;

	if (!HBUF_PUTSL(ob, "<text:p text:style-name=\"dt\">\n"))
		return 0;
	if ((sz = content->size) > 0) {
		while (sz && content->data[sz - 1] == '\n')
			sz--;
		if (!hbuf_put(ob, content->data, sz))
			return 0;
	}
	return HBUF_PUTSL(ob, "</text:p>\n");
}

static int
rndr_codespan(struct lowdown_buf *ob,
	const struct rndr_codespan *param, 
	struct odt *st)
{
	const char	*sty;

	if ((sty = odt_style_add_span(st, LOWDOWN_CODESPAN)) == NULL)
		return 0;
	if (!hbuf_printf(ob,
	    "<text:span text:style-name=\"%s\">", sty))
		return 0;
	if (!escape_htmlb(ob, &param->text, st))
		return 0;
	return HBUF_PUTSL(ob, "</text:span>");
}

static int
rndr_span(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
       	const struct lowdown_node *n, struct odt *st)
{
	const char	*sty;

	if ((sty = odt_style_add_span(st, n->type)) == NULL)
		return 0;
	if (!hbuf_printf(ob,
	    "<text:span text:style-name=\"%s\">", sty))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</text:span>");
}

static int
rndr_linebreak(struct lowdown_buf *ob)
{

	return HBUF_PUTSL(ob, "<text:line-break/>\n");
}

static int
rndr_header(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_header *param, 
	struct odt *st)
{
	ssize_t	level;

	level = (ssize_t)param->level + st->headers_offs;
	if (level < 1)
		level = 1;
	else if (level > 6)
		level = 6;

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;

	if (!hbuf_printf(ob, "<text:h text:style-name=\"h%zu\">", level))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</text:h>\n");
}

static int
rndr_link(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_link *param,
	struct odt *st)
{
	const char	*sty;

	if ((sty = odt_style_add_span(st, LOWDOWN_LINK)) == NULL)
		return 0;
	if (!hbuf_printf(ob,
	    "<text:a xlink:type=\"simple\" "
	    "text:style-name=\"%s\" xlink:href=\"", sty))
		return 0;
	if (!escape_href(ob, &param->link, st))
		return 0;
	if (!HBUF_PUTSL(ob, "\">") ||
	    !hbuf_putb(ob, content) ||
	    !HBUF_PUTSL(ob, "</text:a>"))
		return 0;

	return 1;
}

static int
rndr_list(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_list *param,
	const struct odt_sty *sty)
{

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	if (!HBUF_PUTSL(ob, "<text:list"))
		return 0;
	if (sty != NULL && !hbuf_printf(ob,
	    " text:style-name=\"%s\"", sty->name))
		return 0;
	if (!HBUF_PUTSL(ob, ">\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</text:list>\n");
}

static int
rndr_listitem(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct lowdown_node *n,
	struct odt *st)
{
	size_t	 	 i, size;
	struct odt_sty	*sty;

	/*
	 * Non-definition lists have an initial paragraph that must link
	 * to the root list of the current tree.
	 */

	if (!(n->rndr_listitem.flags & HLIST_FL_DEF)) {
		if (!HBUF_PUTSL(ob, "<text:list-item>"))
			return 0;

		assert(st->list != (size_t)-1);
		for (i = 0; i < st->stysz; i++)
			if (st->stys[i].type == LOWDOWN_PARAGRAPH &&
			    st->stys[i].parent == st->list)
				break;
		if (i == st->stysz) {
			if ((sty = odt_style_add(st)) == NULL)
				return 0;
			sty->autosty = 1;
			sty->parent = st->list;
			sty->type = LOWDOWN_PARAGRAPH;
			snprintf(sty->name, sizeof(sty->name),
				"P%zu", st->stysz);
		} else
			sty = &st->stys[i];

		if (!hbuf_printf(ob,
		    "<text:p text:style-name=\"%s\">", sty->name))
			return 0;
	} else
		if (!HBUF_PUTSL(ob, "<text:p>"))
			return 0;

#if 0
	if (n->rndr_listitem.flags &
	    (HLIST_FL_CHECKED|HLIST_FL_UNCHECKED))
		HBUF_PUTSL(ob, "<input type=\"checkbox\" ");
	if (n->rndr_listitem.flags & HLIST_FL_CHECKED)
		HBUF_PUTSL(ob, "checked=\"checked\" ");
	if (n->rndr_listitem.flags &
	    (HLIST_FL_CHECKED|HLIST_FL_UNCHECKED))
		HBUF_PUTSL(ob, "/>");
#endif

	/* Cut off any trailing space. */

	if ((size = content->size) > 0) {
		while (size && content->data[size - 1] == '\n')
			size--;
		if (!hbuf_put(ob, content->data, size))
			return 0;
	}

	if (!HBUF_PUTSL(ob, "</text:p>"))
		return 0;
	if (!(n->rndr_listitem.flags & HLIST_FL_DEF) &&
	    !HBUF_PUTSL(ob, "</text:list-item>\n"))
		return 0;

	return 1;
}

static int
rndr_paragraph(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	struct odt *st)
{
	size_t		 i = 0, j;
	struct odt_sty	*sty;

	if (content->size == 0)
		return 1;
	while (i < content->size &&
	       isspace((unsigned char)content->data[i])) 
		i++;
	if (i == content->size)
		return 1;

	/*
	 * Paragraphs need to either set their left margin, if in
	 * blockquotes, or link to the root list, if applicable.
	 */

	for (j = 0; j < st->stysz; j++)
		if (st->stys[j].type == LOWDOWN_PARAGRAPH &&
		    st->stys[j].parent == st->list &&
		    (st->stys[j].parent != (size_t)-1 ||
		     st->stys[j].offs == st->offs))
			break;

	if (j == st->stysz) {
		if ((sty = odt_style_add(st)) == NULL)
			return 0;
		sty->autosty = 1;
		sty->parent = st->list;
		sty->type = LOWDOWN_PARAGRAPH;
		if (st->list == (size_t)-1)
			sty->offs = st->offs;
		snprintf(sty->name, sizeof(sty->name),
			"P%zu", st->stysz);
	} else
		sty = &st->stys[j];

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	if (!hbuf_printf(ob,
	    "<text:p text:style-name=\"%s\">", sty->name))
		return 0;
	if (!hbuf_put(ob, content->data + i, content->size - i))
		return 0;
	return HBUF_PUTSL(ob, "</text:p>\n");
}

static int
rndr_html(struct lowdown_buf *ob,
	const struct lowdown_buf *param,
	const struct odt *st)
{

	if (st->flags & LOWDOWN_ODT_SKIP_HTML)
		return 1;
	return escape_htmlb(ob, param, st);
}

static int
rndr_hrule(struct lowdown_buf *ob)
{

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	return hbuf_puts(ob, "<text:p text:style-name=\"hr\"/>\n");
}

/* TODO */
static int
rndr_image(struct lowdown_buf *ob,
	const struct rndr_image *param, 
	const struct odt *st)
{
	char		 dimbuf[32];
	unsigned int	 x, y;
	int		 rc = 0;

	/*
	 * Scan in our dimensions, if applicable.
	 * It's unreasonable for them to be over 32 characters, so use
	 * that as a cap to the size.
	 */

	if (param->dims.size && 
	    param->dims.size < sizeof(dimbuf) - 1) {
		memset(dimbuf, 0, sizeof(dimbuf));
		memcpy(dimbuf, param->dims.data, param->dims.size);
		rc = sscanf(dimbuf, "%ux%u", &x, &y);
	}

	/* Require an "alt", even if blank. */

	if (!HBUF_PUTSL(ob, "<img src=\"") ||
	    !escape_href(ob, &param->link, st) ||
	    !HBUF_PUTSL(ob, "\" alt=\"") ||
	    !escape_attr(ob, &param->alt) ||
	    !HBUF_PUTSL(ob, "\""))
		return 0;

	if (param->attr_cls.size)
		if (!HBUF_PUTSL(ob, " class=\"") ||
		    !escape_attr(ob, &param->attr_cls) ||
		    !HBUF_PUTSL(ob, "\""))
			return 0;
	if (param->attr_id.size)
		if (!HBUF_PUTSL(ob, " id=\"") ||
		    !escape_attr(ob, &param->attr_id) ||
		    !HBUF_PUTSL(ob, "\""))
			return 0;

	if (param->attr_width.size || param->attr_height.size) {
		if (!HBUF_PUTSL(ob, " style=\""))
			return 0;
		if (param->attr_width.size)
			if (!HBUF_PUTSL(ob, "width:") ||
			    !escape_attr(ob, &param->attr_width) ||
			    !HBUF_PUTSL(ob, ";"))
				return 0;
		if (param->attr_height.size)
			if (!HBUF_PUTSL(ob, "height:") ||
			    !escape_attr(ob, &param->attr_height) ||
			    !HBUF_PUTSL(ob, ";"))
				return 0;
		if (!HBUF_PUTSL(ob, "\""))
			return 0;
	} else if (param->dims.size && rc > 0) {
		if (!hbuf_printf(ob, " width=\"%u\"", x))
			return 0;
		if (rc > 1 && !hbuf_printf(ob, " height=\"%u\"", y))
			return 0;
	}

	if (param->title.size)
		if (!HBUF_PUTSL(ob, " title=\"") ||
		    !escape_htmlb(ob, &param->title, st) ||
		    !HBUF_PUTSL(ob, "\""))
			return 0;

	return hbuf_puts(ob, " />");
}

static int
rndr_table(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	if (!HBUF_PUTSL(ob, "<table:table>\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</table:table>\n");
}

static int
rndr_tablerow(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "<table:table-row>\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</table:table-row>\n");
}

static int
rndr_tablecell(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_table_cell *param)
{

	if (!HBUF_PUTSL(ob, "<table:table-cell><text:p>"))
		return 0;

	if (!hbuf_putb(ob, content))
		return 0;

	return HBUF_PUTSL(ob, "</text:p></table:table-cell>\n");
}

static int
rndr_normal_text(struct lowdown_buf *ob,
	const struct rndr_normal_text *param,
	const struct odt *st)
{

	return escape_htmlb(ob, &param->text, st);
}

/* TODO */
static int
rndr_footnotes(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	if (!HBUF_PUTSL(ob, "<div class=\"footnotes\">\n"))
		return 0;
	if (!hbuf_puts(ob, "<hr/>\n"))
		return 0;
	if (!HBUF_PUTSL(ob, "<ol>\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "\n</ol>\n</div>\n");
}

/* TODO */
static int
rndr_footnote_def(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	const struct rndr_footnote_def *param)
{
	size_t	i = 0;
	int	pfound = 0;

	/* Insert anchor at the end of first paragraph block. */

	while ((i + 3) < content->size) {
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

	if (!hbuf_printf(ob, "\n<li id=\"fn%zu\">\n", param->num))
		return 0;

	if (pfound) {
		if (!hbuf_put(ob, content->data, i))
			return 0;
		if (!hbuf_printf(ob, "&#160;"
		    "<a href=\"#fnref%zu\" rev=\"footnote\">"
		    "&#8617;"
		    "</a>", param->num))
			return 0;
		if (!hbuf_put(ob, 
		    content->data + i, content->size - i))
			return 0;
	} else {
		if (!hbuf_putb(ob, content))
			return 0;
	}

	return HBUF_PUTSL(ob, "</li>\n");
}

/* TODO */
static int
rndr_footnote_ref(struct lowdown_buf *ob,
	const struct rndr_footnote_ref *param)
{

	return hbuf_printf(ob, 
		"<sup id=\"fnref%zu\">"
		"<a href=\"#fn%zu\" rel=\"footnote\">"
		"%zu</a></sup>", 
		param->num, param->num, param->num);
}

static int
rndr_math(struct lowdown_buf *ob,
	const struct rndr_math *param, 
	const struct odt *st)
{

	if (param->blockmode && !HBUF_PUTSL(ob, "\\["))
		return 0;
	else if (!param->blockmode && !HBUF_PUTSL(ob, "\\("))
		return 0;
	if (!escape_htmlb(ob, &param->text, st))
		return 0;
	return param->blockmode ?
		HBUF_PUTSL(ob, "\\]") :
		HBUF_PUTSL(ob, "\\)");
}

static int
rndr_doc_footer(struct lowdown_buf *ob, const struct odt *st)
{

	return HBUF_PUTSL(ob, "</office:text>\n</office:body>\n");
}

static int
rndr_root(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct odt *st)
{

	if (!HBUF_PUTSL(ob,
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<office:document\n"
	    " xmlns:css3t=\"http://www.w3.org/TR/css3-text/\"\n"
	    " xmlns:grddl=\"http://www.w3.org/2003/g/data-view#\"\n"
	    " xmlns:xhtml=\"http://www.w3.org/1999/xhtml\"\n"
	    " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
	    " xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"\n"
	    " xmlns:xforms=\"http://www.w3.org/2002/xforms\"\n"
	    " xmlns:dom=\"http://www.w3.org/2001/xml-events\"\n"
	    " xmlns:script=\"urn:oasis:names:tc:opendocument:xmlns:script:1.0\"\n"
	    " xmlns:form=\"urn:oasis:names:tc:opendocument:xmlns:form:1.0\"\n"
	    " xmlns:math=\"http://www.w3.org/1998/Math/MathML\"\n"
	    " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\"\n"
	    " xmlns:loext=\"urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0\"\n"
	    " xmlns:field=\"urn:openoffice:names:experimental:ooo-ms-interop:xmlns:field:1.0\"\n"
	    " xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\"\n"
	    " xmlns:officeooo=\"http://openoffice.org/2009/office\"\n"
	    " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\"\n"
	    " xmlns:chart=\"urn:oasis:names:tc:opendocument:xmlns:chart:1.0\"\n"
	    " xmlns:formx=\"urn:openoffice:names:experimental:ooxml-odf-interop:xmlns:form:1.0\"\n"
	    " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"\n"
	    " xmlns:tableooo=\"http://openoffice.org/2009/table\"\n"
	    " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\"\n"
	    " xmlns:rpt=\"http://openoffice.org/2005/report\"\n"
	    " xmlns:dr3d=\"urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0\"\n"
	    " xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\"\n"
	    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\"\n"
	    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\"\n"
	    " xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
	    " xmlns:calcext=\"urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0\"\n"
	    " xmlns:oooc=\"http://openoffice.org/2004/calc\"\n"
	    " xmlns:config=\"urn:oasis:names:tc:opendocument:xmlns:config:1.0\"\n"
	    " xmlns:ooo=\"http://openoffice.org/2004/office\"\n"
	    " xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
	    " xmlns:drawooo=\"http://openoffice.org/2010/draw\"\n"
	    " xmlns:ooow=\"http://openoffice.org/2004/writer\"\n"
	    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\"\n"
	    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\"\n"
	    " office:version=\"1.3\"\n"
	    " office:mimetype=\"application/vnd.oasis.opendocument.text\">\n"))
		return 0;
	if (!HBUF_PUTSL(ob,
	    "<office:font-face-decls>\n"
	    "<style:font-face style:name=\"Liberation Mono\""
	    " svg:font-family=\"&apos;Liberation Mono&apos;\""
	    " style:font-family-generic=\"modern\""
	    " style:font-pitch=\"fixed\"/>\n"
	    "<style:font-face style:name=\"Liberation Serif\""
	    " svg:font-family=\"&apos;Liberation Serif&apos;\""
	    " style:font-family-generic=\"roman\""
	    " style:font-pitch=\"variable\"/>\n"
	    "<style:font-face style:name=\"Liberation Sans\""
	    " svg:font-family=\"&apos;Liberation Sans&apos;\""
	    " style:font-family-generic=\"swiss\""
	    " style:font-pitch=\"variable\"/>\n"
	    "</office:font-face-decls>\n"))
		return 0;
	if (!odt_styles_flush(ob, st))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</office:document>\n");
}

/*
 * Allocate a meta-data value on the queue "mq".
 * Return zero on failure, non-zero on success.
 */
static int
rndr_meta(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	struct lowdown_metaq *mq,
	const struct lowdown_node *n, struct odt *st)
{
	struct lowdown_meta	*m;
	ssize_t			 val;
	const char		*ep;

	m = calloc(1, sizeof(struct lowdown_meta));
	if (m == NULL)
		return 0;
	TAILQ_INSERT_TAIL(mq, m, entries);

	m->key = strndup(n->rndr_meta.key.data,
		n->rndr_meta.key.size);
	if (m->key == NULL)
		return 0;
	m->value = strndup(content->data, content->size);
	if (m->value == NULL)
		return 0;

	if (strcmp(m->key, "shiftheadinglevelby") == 0) {
		val = (ssize_t)strtonum
			(m->value, -100, 100, &ep);
		if (ep == NULL)
			st->headers_offs = val + 1;
	} else if (strcmp(m->key, "baseheaderlevel") == 0) {
		val = (ssize_t)strtonum
			(m->value, 1, 100, &ep);
		if (ep == NULL)
			st->headers_offs = val;
	}

	return 1;
}

static int
rndr_doc_header(struct lowdown_buf *ob)
{

	return HBUF_PUTSL(ob, "<office:body>\n<office:text>\n");
}

static int
rndr(struct lowdown_buf *ob,
	struct lowdown_metaq *mq, void *ref, 
	const struct lowdown_node *n)
{
	const struct lowdown_node	*child;
	struct lowdown_buf		*tmp;
	int32_t				 ent;
	struct odt			*st = ref;
	struct odt_sty			*sty = NULL;
	int				 ret = 1, rc = 1;

	if ((tmp = hbuf_new(64)) == NULL)
		return 0;

	switch (n->type) {
	case LOWDOWN_BLOCKQUOTE:
		if (st->list == (size_t)-1)
			st->offs++;
		break;
	case LOWDOWN_LIST:
		if (st->list != (size_t)-1)
			break;
		for (st->list = 0; st->list < st->stysz; st->list++) 
			if (st->stys[st->list].type == LOWDOWN_LIST &&
			    st->stys[st->list].offs == st->offs)
				break;
		if (st->list == st->stysz) {
			if ((sty = odt_style_add(st)) == NULL)
				return 0;
			sty->type = LOWDOWN_LIST;
			sty->offs = st->offs;
			sty->autosty = 1;
			snprintf(sty->name, sizeof(sty->name),
				"L%zu", st->stysz);
		}
		sty = &st->stys[st->list];
		break;
	default:
		break;
	}

	TAILQ_FOREACH(child, &n->children, entries)
		if (!rndr(tmp, mq, st, child))
			goto out;

#if 0
	if (n->chng == LOWDOWN_CHNG_INSERT && 
	    !HBUF_PUTSL(ob, "<ins>"))
		goto out;
	if (n->chng == LOWDOWN_CHNG_DELETE && 
	   !HBUF_PUTSL(ob, "<del>"))
		goto out;
#endif

	switch (n->type) {
	case LOWDOWN_ROOT:
		rc = rndr_root(ob, tmp, st);
		break;
	case LOWDOWN_BLOCKCODE:
		rc = rndr_blockcode(ob, &n->rndr_blockcode, st);
		break;
	case LOWDOWN_DEFINITION_TITLE:
		rc = rndr_definition_title(ob, tmp);
		break;
	case LOWDOWN_DEFINITION_DATA:
		rc = rndr_definition_data(ob, tmp);
		break;
	case LOWDOWN_DOC_HEADER:
		rc = rndr_doc_header(ob);
		break;
	case LOWDOWN_META:
		if (n->chng != LOWDOWN_CHNG_DELETE)
			rc = rndr_meta(ob, tmp, mq, n, st);
		break;
	case LOWDOWN_DOC_FOOTER:
		rc = rndr_doc_footer(ob, st);
		break;
	case LOWDOWN_HEADER:
		rc = rndr_header(ob, tmp, &n->rndr_header, st);
		break;
	case LOWDOWN_HRULE:
		rc = rndr_hrule(ob);
		break;
	case LOWDOWN_LIST:
		rc = rndr_list(ob, tmp, &n->rndr_list, sty);
		break;
	case LOWDOWN_LISTITEM:
		rc = rndr_listitem(ob, tmp, n, st);
		break;
	case LOWDOWN_PARAGRAPH:
		rc = rndr_paragraph(ob, tmp, st);
		break;
	case LOWDOWN_TABLE_BLOCK:
		rc = rndr_table(ob, tmp);
		break;
	case LOWDOWN_TABLE_ROW:
		rc = rndr_tablerow(ob, tmp);
		break;
	case LOWDOWN_TABLE_CELL:
		rc = rndr_tablecell(ob, tmp, &n->rndr_table_cell);
		break;
	case LOWDOWN_FOOTNOTES_BLOCK:
		rc = rndr_footnotes(ob, tmp);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rc = rndr_footnote_def(ob, tmp, &n->rndr_footnote_def);
		break;
	case LOWDOWN_BLOCKHTML:
		rc = rndr_html(ob, &n->rndr_blockhtml.text, st);
		break;
	case LOWDOWN_LINK_AUTO:
		rc = rndr_autolink(ob, &n->rndr_autolink, st);
		break;
	case LOWDOWN_CODESPAN:
		rc = rndr_codespan(ob, &n->rndr_codespan, st);
		break;
	case LOWDOWN_TRIPLE_EMPHASIS:
	case LOWDOWN_DOUBLE_EMPHASIS:
	case LOWDOWN_EMPHASIS:
	case LOWDOWN_STRIKETHROUGH:
	case LOWDOWN_HIGHLIGHT:
	case LOWDOWN_SUPERSCRIPT:
		rc = rndr_span(ob, tmp, n, st);
		break;
	case LOWDOWN_IMAGE:
		rc = rndr_image(ob, &n->rndr_image, st);
		break;
	case LOWDOWN_LINEBREAK:
		rc = rndr_linebreak(ob);
		break;
	case LOWDOWN_LINK:
		rc = rndr_link(ob, tmp, &n->rndr_link, st);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		rc = rndr_footnote_ref(ob, &n->rndr_footnote_ref);
		break;
	case LOWDOWN_MATH_BLOCK:
		rc = rndr_math(ob, &n->rndr_math, st);
		break;
	case LOWDOWN_RAW_HTML:
		rc = rndr_html(ob, &n->rndr_raw_html.text, st);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rc = rndr_normal_text(ob, &n->rndr_normal_text, st);
		break;
	case LOWDOWN_ENTITY:
		ent = entity_find_iso(&n->rndr_entity.text);
		rc = ent > 0 ?
			hbuf_printf(ob, "&#%" PRId32 ";", ent) :
			hbuf_putb(ob, &n->rndr_entity.text);
		break;
	default:
		rc = hbuf_putb(ob, tmp);
		break;
	}
	if (!rc)
		goto out;

#if 0
	if (n->chng == LOWDOWN_CHNG_INSERT && 
	    !HBUF_PUTSL(ob, "</ins>"))
		goto out;
	if (n->chng == LOWDOWN_CHNG_DELETE &&
	    !HBUF_PUTSL(ob, "</del>"))
		goto out;
#endif

	switch (n->type) {
	case LOWDOWN_BLOCKQUOTE:
		if (st->list == (size_t)-1)
			st->offs--;
		break;
	case LOWDOWN_LIST:
		if (sty != NULL)
			st->list = (size_t)-1;
		break;
	default:
		break;
	}

	ret = 1;
out:
	hbuf_free(tmp);
	return ret;
}

int
lowdown_odt_rndr(struct lowdown_buf *ob,
	void *arg, const struct lowdown_node *n)
{
	struct odt		*st = arg;
	struct lowdown_metaq	 metaq;
	int			 rc;

	TAILQ_INIT(&metaq);
	st->headers_offs = 1;
	st->stys = NULL;
	st->stysz = 0;
	st->list = (size_t)-1;

	rc = rndr(ob, &metaq, st, n);

	free(st->stys);
	lowdown_metaq_free(&metaq);
	return rc;
}

void *
lowdown_odt_new(const struct lowdown_opts *opts)
{
	struct odt	*p;

	if ((p = calloc(1, sizeof(struct odt))) == NULL)
		return NULL;

	p->flags = opts == NULL ? 0 : opts->oflags;
	return p;
}

void
lowdown_odt_free(void *arg)
{

	free(arg);
}