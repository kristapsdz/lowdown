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
 * Maximum length of any style.  This should account for fixed prefix
 * text (e.g., "Frame" at longest) then an incrementing size_t.
 */
#define STYLE_NAME_LEN	 32

/*
 * Default size for a blockquote (paragraph indent).
 */
static const float TAB_LEN = 1.25;

/*
 * Default size for a list indent.  Lists are first indented by the
 * number of tabs (starting at zero), then giving a full list indent,
 * then each sub-list gets half again this.
 */
static const float LIST_LEN = 1.27;

/*
 * A style in <office:styles> or <office-automatic-styles>.  The
 * difference between these two, according to section 3.15 of the v1.3,
 * is that automatic styles are ad hoc and regular styles are linked to
 * a central style that may be changed.  Span styles are in-line, blocks
 * can have offsets.
 */
struct	odt_sty {
	char			 name[STYLE_NAME_LEN]; /* name */
	size_t			 offs; /* offset ("tabs") from zero */
	size_t			 parent; /* parent or (size_t)-1*/
	enum lowdown_rndrt	 type; /* specific type of style */
	int			 foot; /* in a footnote */
	int			 fmt; /* general type of style */
#define	ODT_STY_TEXT		 1 /* text (inline) */
#define	ODT_STY_PARA		 2 /* paragraph */
#define ODT_STY_UL		 3 /* unordered list */
#define ODT_STY_OL		 4 /* ordered list */
#define ODT_STY_H1		 5 /* h1 heading */
#define ODT_STY_H2		 6 /* h2 heading */
#define ODT_STY_H3		 7 /* h3 heading */
#define	ODT_STY_TBL		 8 /* table */
#define ODT_STY_TBL_PARA	 9 /* table contents */
#define	ODT_STY_LIT		 10 /* literal */
	int			 autosty; /* automatic-style? */
};

/*
 * A change.  I'm not sure we'll need anything but "ins", so this could
 * just be an array of int, but whatever.
 */
struct	odt_chng {
	int		 	 ins; /* inserted vs deleted */
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
	size_t			 sty_T; /* "T" styles */
	size_t			 sty_Table; /* "Table" styles */
	size_t			 sty_L; /* "L" styles */
	size_t			 sty_P; /* "P" styles */
	size_t			 offs; /* offs or (size_t)-1 in list */
	size_t			 list; /* root list style or (size_t)-1 */
	int			 foot; /* in footnote or not */
	const struct lowdown_node *foots; /* footnotes */
	struct odt_chng		*chngs; /* changes in content */
	size_t			 chngsz; /* number of changes */
};

static int rndr(struct lowdown_buf *,
	struct lowdown_metaq *, void *, const struct lowdown_node *);

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
odt_style_add_text(struct odt *st, enum lowdown_rndrt type)
{
	size_t		 i;
	struct odt_sty	*s;

	for (i = 0; i < st->stysz; i++)
		if (st->stys[i].type == type) {
			assert(st->stys[i].fmt == ODT_STY_TEXT);
			return st->stys[i].name;
		}

	if ((s = odt_style_add(st)) == NULL)
		return NULL;

	s->fmt = ODT_STY_TEXT;
	s->type = type;

	/* Codespans and links are fixed, the rest are automatic. */

	switch (type) {
	case LOWDOWN_CODESPAN:
		strlcpy(s->name, "Source_20_Text", sizeof(s->name));
		break;
	case LOWDOWN_LINK:
		strlcpy(s->name, "Internet_20_link", sizeof(s->name));
		break;
	default:
		s->autosty = 1;
		snprintf(s->name, sizeof(s->name), "T%zu", st->sty_T++);
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

	if (sty->type == LOWDOWN_LIST &&
	    !HBUF_PUTSL(ob, "<text:list-style"))
		return 0;
	if (sty->type != LOWDOWN_LIST &&
	    !HBUF_PUTSL(ob, "<style:style"))
		return 0;

	switch (sty->fmt) {
	case ODT_STY_TEXT:
		if (!HBUF_PUTSL(ob, " style:family=\"text\""))
			return 0;
		break;
	case ODT_STY_TBL_PARA:
	case ODT_STY_PARA:
	case ODT_STY_LIT:
	case ODT_STY_H1:
	case ODT_STY_H2:
	case ODT_STY_H3:
		if (!HBUF_PUTSL(ob, " style:family=\"paragraph\""))
			return 0;
		break;
	case ODT_STY_TBL:
		if (!HBUF_PUTSL(ob, " style:family=\"table\""))
			return 0;
		break;
	}

	if (!hbuf_printf(ob, " style:name=\"%s\"", sty->name))
		return 0;

	/*
	 * Paragraphs in lists need to link to the list, then set some
	 * other crap found in libreoffice output.
	 */

	switch (sty->fmt) {
	case ODT_STY_LIT:
		if (!HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Preformatted_20_Text\""))
			return 0;
		break;
	case ODT_STY_PARA:
		if (!sty->foot && !HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Standard\""))
			return 0;
		if (sty->foot && !HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Footnote\""))
			return 0;
		if (sty->parent != (size_t)-1 && !hbuf_printf(ob,
		    " style:list-style-name=\"%s\"", 
		    st->stys[sty->parent].name))
			return 0;
		break;
	case ODT_STY_TBL_PARA:
		if (sty->foot && !HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Footnote\""))
			return 0;
		if (!sty->foot && !HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Table_20_Contents\""))
			return 0;
		break;
	case ODT_STY_H1:
		if (!HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Heading_20_1\""))
			return 0;
		break;
	case ODT_STY_H2:
		if (!HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Heading_20_2\""))
			return 0;
		break;
	case ODT_STY_H3:
		if (!HBUF_PUTSL(ob,
		    " style:parent-style-name=\"Heading_20_3\""))
			return 0;
		break;
	default:
		break;
	}

	switch (sty->type) {
	case LOWDOWN_LINK:
		if (!HBUF_PUTSL(ob,
		    " style:display-name=\"Internet Link\""))
			return 0;
		break;
	case LOWDOWN_CODESPAN:
		if (!HBUF_PUTSL(ob,
		    " style:display-name=\"Source Text\""))
			return 0;
		break;
	case LOWDOWN_HRULE:
		if (!HBUF_PUTSL(ob,
		    " style:display-name=\"Horizontal Line\""
		    " style:next-style-name=\"Text_20_body\""
		    " style:class=\"html\""))
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
	case LOWDOWN_TABLE_BLOCK:
		if (!hbuf_printf(ob,
		    "<style:table-properties"
		    " fo:margin-left=\"%.3fcm\""
		    " fo:margin-right=\"0cm\""
		    " table:align=\"margins\"/>\n",
		    sty->offs * TAB_LEN))
			return 0;
		break;
	case LOWDOWN_HRULE:
		if (!HBUF_PUTSL(ob,
		    "<style:paragraph-properties"
		    " fo:margin-top=\"0cm\""
		    " fo:margin-bottom=\"0.499cm\""
		    " style:contextual-spacing=\"false\""
		    " style:border-line-width-bottom=\"0.002cm 0.004cm 0.002cm\""
		    " fo:padding=\"0cm\""
		    " fo:border-left=\"none\""
		    " fo:border-right=\"none\""
		    " fo:border-top=\"none\""
		    " fo:border-bottom=\"0.14pt double #808080\""
		    " text:number-lines=\"false\""
		    " text:line-number=\"0\""
		    " style:join-border=\"false\"/>\n"
   		    "<style:text-properties"
		    " fo:font-size=\"6pt\""
		    " style:font-size-asian=\"6pt\""
		    " style:font-size-complex=\"6pt\"/>\n"))
			return 0;
		break;
	case LOWDOWN_HEADER:
		break;
	case LOWDOWN_PARAGRAPH:
		if (sty->offs == 0)
			break;
		if (!hbuf_printf(ob,
		    "<style:paragraph-properties"
		    " fo:margin-left=\"%.3fcm\""
		    " fo:margin-right=\"0cm\""
		    " fo:text-indent=\"0cm\""
		    " style:auto-text-indent=\"false\"/>\n",
		    sty->offs * TAB_LEN))
			return 0;
		break;
	case LOWDOWN_LIST:
		for (i = 0; i < 10; i++) {
			if (sty->fmt == ODT_STY_OL && !hbuf_printf(ob,
   			    "<text:list-level-style-number"
			    " text:level=\"%zu\""
			    " text:style-name=\"Numbering_20_Symbols\""
			    " style:num-suffix=\".\""
			    " style:num-format=\"1\">\n"
			    "<style:list-level-properties"
			    " text:list-level-position-and-space-mode="
			     "\"label-alignment\">\n"
			    "<style:list-level-label-alignment"
			    " text:label-followed-by=\"listtab\""
			    " text:list-tab-stop-position=\"%.3fcm\""
			    " fo:text-indent=\"-0.635cm\""
			    " fo:margin-left=\"%.3fcm\"/>\n"
			    "</style:list-level-properties>\n"
			    "</text:list-level-style-number>\n",
			    i + 1, 
			    (TAB_LEN * sty->offs) + LIST_LEN +
			    ((LIST_LEN / 2.0) * i),
			    (TAB_LEN * sty->offs) + LIST_LEN +
			    ((LIST_LEN / 2.0) * i)))
				return 0;
			if (sty->fmt == ODT_STY_UL && !hbuf_printf(ob,
			    "<text:list-level-style-bullet"
			    " text:level=\"%zu\""
			    " text:style-name=\"Bullet_20_Symbols\""
			    " text:bullet-char=\"&#x2022;\">\n"
			    "<style:list-level-properties"
			    " text:list-level-position-and-space-mode="
			     "\"label-alignment\">\n"
			    "<style:list-level-label-alignment"
			    " text:label-followed-by=\"listtab\""
			    " text:list-tab-stop-position=\"%.3fcm\""
			    " fo:text-indent=\"-0.635cm\""
			    " fo:margin-left=\"%.3fcm\"/>\n"
			    "</style:list-level-properties>\n"
			    "</text:list-level-style-bullet>\n",
			    i + 1, 
			    (TAB_LEN * sty->offs) + LIST_LEN +
			    ((LIST_LEN / 2.0) * i),
			    (TAB_LEN * sty->offs) + LIST_LEN +
			    ((LIST_LEN / 2.0) * i)))
				return 0;
		}
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
		    " style:font-family-asian="
		     "\"&apos;Liberation Mono&apos;\""
		    " style:font-family-generic-asian=\"modern\""
		    " style:font-pitch-asian=\"fixed\""
		    " style:font-name-complex=\"Liberation Mono\""
		    " style:font-family-complex="
		     "\"&apos;Liberation Mono&apos;\""
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

	if (sty->type == LOWDOWN_LIST &&
	    !HBUF_PUTSL(ob, "</text:list-style>\n"))
		return 0;
	if (sty->type != LOWDOWN_LIST &&
	    !HBUF_PUTSL(ob, "</style:style>\n"))
		return 0;

	return 1;
}

/*
 * Flush out the "fixed" styles we need for standalone mode.
 * XXX: it's possible to put a lot of this into a separate file,
 * somehow, but that's a matter for the future.  Return FALSE on
 * failure, TRUE on success.
 */
static int
odt_styles_flush_fixed(struct lowdown_buf *ob, const struct odt *st)
{
	size_t	 i;
	int	 xlink = 0, ulist = 0, olist = 0,
		 h1 = 0, h2 = 0, h3 = 0, hr = 0, tab = 0,
		 lit = 0;
	
	/*
	 * Many styles and auto-styles depend upon fixed parent styles,
	 * for example, a paragraph auto-style has a parent style that's
	 * fixed.  Determine which of these static styles we need by
	 * looking through what styles we're going to output.
	 */

	for (i = 0; i < st->stysz; i++)
		switch (st->stys[i].type) {
		case LOWDOWN_TABLE_BLOCK:
			tab = 1;
			break;
		case LOWDOWN_PARAGRAPH:
			if (st->stys[i].fmt == ODT_STY_LIT)
				lit = 1;
			break;
		case LOWDOWN_HRULE:
			hr = 1;
			break;
		case LOWDOWN_LINK:
			xlink = 1;
			break;
		case LOWDOWN_HEADER:
			if (st->stys[i].fmt == ODT_STY_H1)
				h1 = 1;
			else if (st->stys[i].fmt == ODT_STY_H2)
				h2 = 1;
			else if (st->stys[i].fmt == ODT_STY_H3)
				h3 = 1;
			break;
		case LOWDOWN_LIST:
			if (st->stys[i].fmt == ODT_STY_UL)
				ulist = 1;
			if (st->stys[i].fmt == ODT_STY_OL)
				olist = 1;
			break;
		default:
			break;
		}

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

	if (!HBUF_PUTSL(ob, "<office:styles>\n"))
		return 0;

	/* Emit boilerplate parent styles. */

  	if (!HBUF_PUTSL(ob,
  	    "<style:style"
	    " style:name=\"Standard\""
	    " style:family=\"paragraph\""
	    " style:class=\"text\"/>\n"))
		return 0;
	if (tab && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Frame\""
	    " style:family=\"graphic\">\n"
	    "<style:graphic-properties"
	    " text:anchor-type=\"as-char\""
	    " svg:x=\"0cm\""
	    " svg:y=\"0cm\""
	    " fo:margin-left=\"0cm\""
	    " fo:margin-right=\"0cm\""
	    " fo:margin-top=\"0.201cm\""
	    " fo:margin-bottom=\"0.201cm\""
	    " style:wrap=\"parallel\""
	    " style:number-wrapped-paragraphs=\"no-limit\""
	    " style:wrap-contour=\"false\""
	    " style:vertical-pos=\"top\""
	    " style:vertical-rel=\"paragraph-content\""
	    " style:horizontal-pos=\"center\""
	    " style:horizontal-rel=\"paragraph-content\""
	    " fo:padding=\"0cm\""
	    " fo:border=\"0pt solid #000000\"/>\n"
	    "</style:style>\n"))
	    	return 0;
	if (lit && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Preformatted_20_Text\""
	    " style:display-name=\"Preformatted Text\""
	    " style:family=\"paragraph\""
	    " style:parent-style-name=\"Standard\""
	    " style:class=\"html\">\n"
	    "<style:paragraph-properties"
	    " fo:margin-top=\"0cm\""
	    " fo:margin-bottom=\"0cm\""
	    " style:contextual-spacing=\"false\"/>\n"
	    "<style:text-properties"
	    " style:font-name=\"Liberation Mono\""
	    " fo:font-family=\"&apos;Liberation Mono&apos;\""
	    " style:font-family-generic=\"modern\""
	    " style:font-pitch=\"fixed\""
	    " fo:font-size=\"10pt\""
	    " style:font-name-asian=\"Liberation Mono\""
	    " style:font-family-asian=\"&apos;Liberation Mono&apos;\""
	    " style:font-family-generic-asian=\"modern\""
	    " style:font-pitch-asian=\"fixed\""
	    " style:font-size-asian=\"10pt\""
	    " style:font-name-complex=\"Liberation Mono\""
	    " style:font-family-complex=\"&apos;Liberation Mono&apos;\""
	    " style:font-family-generic-complex=\"modern\""
	    " style:font-pitch-complex=\"fixed\""
	    " style:font-size-complex=\"10pt\"/>\n"
	    "</style:style>\n"))
		return 0;
	if (tab && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Table_20_Contents\""
	    " style:display-name=\"Table Contents\""
	    " style:family=\"paragraph\""
	    " style:parent-style-name=\"Standard\""
	    " style:class=\"extra\">\n"
	    "<style:paragraph-properties"
	    " fo:orphans=\"0\""
	    " fo:widows=\"0\""
	    " text:number-lines=\"false\""
	    " text:line-number=\"0\"/>\n"
	    "</style:style>\n"))
		return 0;
	if ((h1 || h2 || h3 || hr) && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Text_20_body\""
	    " style:display-name=\"Text body\""
	    " style:family=\"paragraph\""
	    " style:parent-style-name=\"Standard\""
	    " style:class=\"text\">\n"
	    "<style:paragraph-properties"
	    " fo:margin-top=\"0cm\""
	    " fo:margin-bottom=\"0.247cm\""
	    " style:contextual-spacing=\"false\""
	    " fo:line-height=\"115%\"/>\n"
	    "</style:style>\n"))
		return 0;
	if ((h1 || h2 || h3) && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Heading\""
	    " style:family=\"paragraph\""
	    " style:parent-style-name=\"Standard\""
	    " style:class=\"text\">\n"
	    "<style:paragraph-properties"
	    " fo:margin-top=\"0.423cm\""
	    " fo:margin-bottom=\"0.212cm\""
	    " style:contextual-spacing=\"false\""
	    " fo:keep-with-next=\"always\"/>\n"
	    "<style:text-properties"
	    " style:font-name=\"Liberation Sans\""
	    " fo:font-family=\"&apos;Liberation Sans&apos;\""
	    " style:font-family-generic=\"swiss\""
	    " style:font-pitch=\"variable\""
	    " fo:font-size=\"14pt\""
	    " style:font-name-asian=\"Liberation Sans\""
	    " style:font-family-asian=\"&apos;Liberation Sans&apos;\""
	    " style:font-family-generic-asian=\"system\""
	    " style:font-pitch-asian=\"variable\""
	    " style:font-size-asian=\"14pt\""
	    " style:font-name-complex=\"Liberation Sans\""
	    " style:font-family-complex=\"&apos;Liberation Sans&apos;\""
	    " style:font-family-generic-complex=\"system\""
	    " style:font-pitch-complex=\"variable\""
	    " style:font-size-complex=\"14pt\"/>\n"
	    "</style:style>\n"))
		return 0;
	if (ulist && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Bullet_20_Symbols\""
	    " style:display-name=\"Bullet Symbols\""
	    " style:family=\"text\">\n"
	    "<style:text-properties"
	    " style:font-name=\"OpenSymbol\""
	    " fo:font-family=\"OpenSymbol\""
	    " style:font-charset=\"x-symbol\""
	    " style:font-name-asian=\"OpenSymbol\""
	    " style:font-family-asian=\"OpenSymbol\""
	    " style:font-charset-asian=\"x-symbol\""
	    " style:font-name-complex=\"OpenSymbol\""
	    " style:font-family-complex=\"OpenSymbol\""
	    " style:font-charset-complex=\"x-symbol\"/>\n"
   	    "</style:style>\n"))
		return 0;
	if (olist && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Numbering_20_Symbols\""
	    " style:display-name=\"Numbering Symbols\""
	    " style:family=\"text\"/>\n"))
		return 0;
	if (h1 && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Heading_20_1\""
	    " style:display-name=\"Heading 1\""
	    " style:family=\"paragraph\""
	    " style:parent-style-name=\"Heading\""
	    " style:next-style-name=\"Text_20_body\""
	    " style:default-outline-level=\"1\""
	    " style:class=\"text\">\n"
	    "<style:paragraph-properties"
	    " fo:margin-top=\"0.423cm\""
	    " fo:margin-bottom=\"0.212cm\""
	    " style:contextual-spacing=\"false\"/>\n"
	    "<style:text-properties"
	    " fo:font-size=\"130%\""
	    " fo:font-weight=\"bold\""
	    " style:font-size-asian=\"130%\""
	    " style:font-weight-asian=\"bold\""
	    " style:font-size-complex=\"130%\""
	    " style:font-weight-complex=\"bold\"/>\n"
	    "</style:style>\n"))
	    	return 0;
	if (h2 && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Heading_20_2\""
	    " style:display-name=\"Heading 2\""
	    " style:family=\"paragraph\""
	    " style:parent-style-name=\"Heading\""
	    " style:next-style-name=\"Text_20_body\""
	    " style:default-outline-level=\"2\""
	    " style:class=\"text\">\n"
	    "<style:paragraph-properties"
	    " fo:margin-top=\"0.353cm\""
	    " fo:margin-bottom=\"0.212cm\""
	    " style:contextual-spacing=\"false\"/>\n"
	    "<style:text-properties"
	    " fo:font-size=\"115%\""
	    " fo:font-weight=\"bold\""
	    " style:font-size-asian=\"115%\""
	    " style:font-weight-asian=\"bold\""
	    " style:font-size-complex=\"115%\""
	    " style:font-weight-complex=\"bold\"/>\n"
	    "</style:style>\n"))
	    	return 0;
	if (h3 && !HBUF_PUTSL(ob,
	    "<style:style"
	    " style:name=\"Heading_20_3\""
	    " style:display-name=\"Heading 3\""
	    " style:family=\"paragraph\""
	    " style:parent-style-name=\"Heading\""
	    " style:next-style-name=\"Text_20_body\""
	    " style:default-outline-level=\"3\""
	    " style:class=\"text\">\n"
	    "<style:paragraph-properties"
	    " fo:margin-top=\"0.247cm\""
	    " fo:margin-bottom=\"0.212cm\""
	    " style:contextual-spacing=\"false\"/>\n"
	    "<style:text-properties"
	    " fo:font-size=\"101%\""
	    " fo:font-weight=\"bold\""
	    " style:font-size-asian=\"101%\""
	    " style:font-weight-asian=\"bold\""
	    " style:font-size-complex=\"101%\""
	    " style:font-weight-complex=\"bold\"/>\n"
	    "</style:style>\n"))
	    	return 0;
	if (tab && !HBUF_PUTSL(ob,
	    "<style:style style:name=\"fr1\""
	    " style:family=\"graphic\""
	    " style:parent-style-name=\"Frame\">\n"
	    "<style:graphic-properties"
	    " style:run-through=\"foreground\""
	    " style:wrap=\"parallel\""
	    " style:number-wrapped-paragraphs=\"no-limit\""
	    " style:vertical-pos=\"middle\""
	    " style:vertical-rel=\"baseline\""
	    " style:horizontal-pos=\"center\""
	    " style:horizontal-rel=\"paragraph\"/>\n"
	    " </style:style>\n"))
		return 0;


	/* Emit fixed styles. */

	for (i = 0; i < st->stysz; i++)
		if (!st->stys[i].autosty &&
		    !odt_sty_flush(ob, st, &st->stys[i]))
			return 0;

	if (!HBUF_PUTSL(ob,
	    "</office:styles>\n"))
		return 0;
	return 1;
}

/*
 * Flush out the elements for scripts and styles.  Return FALSE on
 * failure, TRUE on success.
 */
static int
odt_styles_flush(struct lowdown_buf *ob, const struct odt *st)
{
	size_t	 i;

	if ((st->flags & LOWDOWN_STANDALONE) &&
	    !odt_styles_flush_fixed(ob, st))
		return 0;

	if (!HBUF_PUTSL(ob, "<office:automatic-styles>\n"))
		return 0;
	for (i = 0; i < st->stysz; i++)
		if (st->stys[i].autosty &&
		    !odt_sty_flush(ob, st, &st->stys[i]))
			return 0;

	/*
	 * I'm not sure why the page layout goes into the automatic
	 * styles and not the fixed styles, but if placed in fixed
	 * styles, this isn't processed.
	 */

	if (!HBUF_PUTSL(ob,
	    "<style:page-layout style:name=\"pm1\">\n"
	    "<style:page-layout-properties"
	    " fo:page-width=\"21.001cm\""
	    " fo:page-height=\"29.7cm\""
	    " style:num-format=\"1\""
	    " style:print-orientation=\"portrait\""
	    " fo:margin-top=\"2cm\""
	    " fo:margin-bottom=\"2cm\""
	    " fo:margin-left=\"2cm\""
	    " fo:margin-right=\"2cm\""
	    " style:writing-mode=\"lr-tb\""
	    " style:footnote-max-height=\"0cm\">\n"
	    "</style:page-layout-properties>\n"
	    "</style:page-layout>\n"))
		return 0;

	if (!HBUF_PUTSL(ob, "</office:automatic-styles>\n"))
		return 0;

	/*
	 * Since this references an automatic style (pm1), emit this
	 * regardless of whether we're in standalone or not.
	 */

	return HBUF_PUTSL(ob,
		"<office:master-styles>\n"
		"<style:master-page "
		" style:name=\"Standard\""
		" style:page-layout-name=\"pm1\"/>\n"
		"</office:master-styles>\n");
}

/*
 * Use our metadata to grab change identifiers.  Return FALSE on
 * failure, TRUE on success.
 */
static int
odt_changes_flush(struct lowdown_buf *ob,
	const struct lowdown_metaq *mq,
	const struct odt *st)
{
	const struct lowdown_meta	*m;
	const char			*author = NULL, *date = NULL,
	      				*rcsauthor = NULL, *rcsdate = NULL;
	char				 buf[64];
	size_t	 			 i;
	time_t				 t = time(NULL);

	if (st->chngsz == 0)
		return 1;

	TAILQ_FOREACH(m, mq, entries)
		if (strcasecmp(m->key, "author") == 0)
			author = m->value;
		else if (strcasecmp(m->key, "date") == 0)
			date = m->value;
		else if (strcasecmp(m->key, "rcsauthor") == 0)
			rcsauthor = rcsauthor2str(m->value);
		else if (strcasecmp(m->key, "rcsdate") == 0)
			rcsdate = rcsdate2str(m->value);

	/* Overrides. */

	if (rcsdate != NULL)
		date = rcsdate;
	if (rcsauthor != NULL)
		author = rcsauthor;

	/* We require at least a date. */

	if (date == NULL) {
		if (strftime(buf, sizeof(buf),
		    "%Y-%m-%dT%H:%M:%S", localtime(&t)) == 0)
			date = "1970-01-01";
		else
			date = buf;
	}

	if (!HBUF_PUTSL(ob,
	    "<text:tracked-changes"
	    " text:track-changes=\"false\">\n"))
		return 0;
	for (i = 0; i < st->chngsz; i++) {
		if (!hbuf_printf(ob,
		    "<text:changed-region"
		    " xml:id=\"ct%zu\""
		    " text:id=\"ct%zu\">\n"
		    "<text:%s>\n"
		    "<office:change-info>\n", i, i,
		    st->chngs[i].ins ? "insertion" : "deletion"))
			return 0;
		if (author != NULL) {
			if (!HBUF_PUTSL(ob, "<dc:creator>"))
				return 0;
			if (!hesc_html(ob, author,
			    strlen(author), 1, 0, 1))
				return 0;
			if (!HBUF_PUTSL(ob, "</dc:creator>\n"))
				return 0;
		}
		if (!HBUF_PUTSL(ob, "<dc:date>"))
			return 0;
		if (!hesc_html(ob, date, strlen(date), 1, 0, 1))
			return 0;
		if (!HBUF_PUTSL(ob, "</dc:date>\n"))
			return 0;
		if (!hbuf_printf(ob,
		    "</office:change-info>\n"
		    "</text:%s>\n"
		    "</text:changed-region>\n",
		    st->chngs[i].ins ? "insertion" : "deletion"))
			return 0;
	}

	return HBUF_PUTSL(ob, "</text:tracked-changes>\n");
}

/*
 * Flush out the <office:meta> element, if applicable.  Return FALSE on
 * failure, TRUE on success.
 */
static int
odt_metaq_flush(struct lowdown_buf *ob,
	const struct lowdown_metaq *mq, 
	const struct odt *st)
{
	const struct lowdown_meta	*m;
	const char			*author = NULL, *title = NULL,
					*date = NULL, *rcsauthor = NULL, 
					*rcsdate = NULL;

	if (mq == NULL || TAILQ_EMPTY(mq))
		return 1;

	TAILQ_FOREACH(m, mq, entries)
		if (strcasecmp(m->key, "author") == 0)
			author = m->value;
		else if (strcasecmp(m->key, "date") == 0)
			date = m->value;
		else if (strcasecmp(m->key, "rcsauthor") == 0)
			rcsauthor = rcsauthor2str(m->value);
		else if (strcasecmp(m->key, "rcsdate") == 0)
			rcsdate = rcsdate2str(m->value);
		else if (strcasecmp(m->key, "title") == 0)
			title = m->value;

	/* Overrides. */

	if (title == NULL)
		title = "Untitled article";
	if (rcsdate != NULL)
		date = rcsdate;
	if (rcsauthor != NULL)
		author = rcsauthor;

	if (!HBUF_PUTSL(ob, "<office:meta>\n"))
		return 0;

	if (!HBUF_PUTSL(ob, "<dc:title>"))
		return 0;
	if (!hesc_html(ob, title, strlen(title), 1, 0, 1))
		return 0;
	if (!HBUF_PUTSL(ob, "</dc:title>\n"))
		return 0;

	if (author != NULL) {
		if (!HBUF_PUTSL(ob, "<dc:creator>"))
			return 0;
		if (!hesc_html(ob, author, strlen(author), 1, 0, 1))
			return 0;
		if (!HBUF_PUTSL(ob, "</dc:creator>\n"))
			return 0;
		if (!HBUF_PUTSL(ob, "<meta:initial-creator>"))
			return 0;
		if (!hesc_html(ob, author, strlen(author), 1, 0, 1))
			return 0;
		if (!HBUF_PUTSL(ob, "</meta:initial-creator>\n"))
			return 0;
	}

	if (date != NULL) {
		if (!HBUF_PUTSL(ob, "<dc:date>"))
			return 0;
		if (!hesc_html(ob, date, strlen(date), 1, 0, 1))
			return 0;
		if (!HBUF_PUTSL(ob, "</dc:date>\n"))
			return 0;
		if (!HBUF_PUTSL(ob, "<meta:creation-date>"))
			return 0;
		if (!hesc_html(ob, date, strlen(date), 1, 0, 1))
			return 0;
		if (!HBUF_PUTSL(ob, "</meta:creation-date>\n"))
			return 0;
	}

	return HBUF_PUTSL(ob, "</office:meta>\n");
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
 * Escape an href link.  Return FALSE on failure, TRUE on success.
 */
static int
escape_href(struct lowdown_buf *ob, const struct lowdown_buf *in,
	const struct odt *st)
{

	return hesc_href(ob, in->data, in->size);
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_autolink(struct lowdown_buf *ob, 
	const struct rndr_autolink *parm,
	struct odt *st)
{
	const char	*sty;

	if (parm->link.size == 0)
		return 1;

	if ((sty = odt_style_add_text(st, LOWDOWN_LINK)) == NULL)
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

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_blockcode(struct lowdown_buf *ob, 
	const struct rndr_blockcode *parm,
	struct odt *st)
{
	size_t		 i, j, sz, ssz;
	struct odt_sty	*s;

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;

	for (i = 0; i < st->stysz; i++)
		if (st->stys[i].type == LOWDOWN_PARAGRAPH &&
		    st->stys[i].fmt == ODT_STY_LIT &&
		    st->stys[i].parent == st->list &&
		    st->stys[i].offs == st->offs)
			break;

	if (i == st->stysz) {
		if ((s = odt_style_add(st)) == NULL)
			return 0;
		s->autosty = 1;
		s->type = LOWDOWN_PARAGRAPH;
		s->fmt = ODT_STY_LIT;
		s->parent = st->list;
		s->offs = st->offs;
		snprintf(s->name, sizeof(s->name),
			"P%zu", st->sty_P++);
	} else
		s = &st->stys[i];

	for (i = 0; i < parm->text.size; ) {
		if (!hbuf_printf(ob,
		    "<text:p text:style-name=\"%s\">", s->name))
			return 0;
		
		/* 
		 * Iterate through each line, printing it in its own
		 * <text:p>.  If we encounter more than one space in a
		 * row, then use a <text:s text:c> spanner to print the
		 * literal spaces.
		 */

		for (sz = 0, j = i; i < parm->text.size; i++, sz++) {
			if (parm->text.data[i] == ' ' &&
			    i < parm->text.size - 1 &&
			    parm->text.data[i + 1] == ' ') {
				if (!hesc_html(ob,
				    &parm->text.data[j], sz, 1, 1, 1))
					return 0;
				sz = 0;
				for (ssz = 0; i < parm->text.size;
				     i++, ssz++)
					if (parm->text.data[i] != ' ')
						break;
				j = i;
				if (!hbuf_printf(ob,
				    "<text:s text:c=\"%zu\"/>", ssz))
					return 0;
			}
			if (i < parm->text.size &&
			    parm->text.data[i] == '\n')
				break;
		}
		if (!hesc_html(ob, &parm->text.data[j], sz, 1, 1, 1))
			return 0;
		if (!HBUF_PUTSL(ob, "</text:p>\n"))
			return 0;
		if (i < parm->text.size)
			i++;
	}

	return 1;
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_codespan(struct lowdown_buf *ob,
	const struct rndr_codespan *param, 
	struct odt *st)
{
	const char	*sty;

	if ((sty = odt_style_add_text(st, LOWDOWN_CODESPAN)) == NULL)
		return 0;
	if (!hbuf_printf(ob,
	    "<text:span text:style-name=\"%s\">", sty))
		return 0;
	if (!escape_htmlb(ob, &param->text, st))
		return 0;
	return HBUF_PUTSL(ob, "</text:span>");
}

/*
 * This covers all manner of span types: italic, bold, etc.  Return
 * FALSE on failure, TRUE on success.
 */
static int
rndr_span(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
       	const struct lowdown_node *n, struct odt *st)
{
	const char	*sty;

	if ((sty = odt_style_add_text(st, n->type)) == NULL)
		return 0;
	if (!hbuf_printf(ob,
	    "<text:span text:style-name=\"%s\">", sty))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</text:span>");
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_linebreak(struct lowdown_buf *ob)
{

	return HBUF_PUTSL(ob, "<text:line-break/>\n");
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_header(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_header *param, 
	struct odt *st)
{
	struct odt_sty	*sty;
	ssize_t		 level;
	size_t		 i;
	int		 fl;

	level = (ssize_t)param->level + st->headers_offs;
	if (level < 1)
		level = 1;
	else if (level > 3)
		level = 3;

	if (level == 1)
		fl = ODT_STY_H1;
	else if (level == 2)
		fl = ODT_STY_H2;
	else
		fl = ODT_STY_H3;

	for (i = 0; i < st->stysz; i++)
		if (st->stys[i].type == LOWDOWN_HEADER &&
		    st->stys[i].fmt == fl)
			break;
	if (i == st->stysz) {
		if ((sty = odt_style_add(st)) == NULL)
			return 0;
		sty->autosty = 1;
		sty->fmt = fl;
		sty->type = LOWDOWN_HEADER;
		snprintf(sty->name, sizeof(sty->name),
			"P%zu", st->sty_P++);
	} else
		sty = &st->stys[i];

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	if (!hbuf_printf(ob,
	     "<text:h text:style-name=\"%s\""
	     " text:outline-level=\"%zu\">", sty->name, level))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</text:h>\n");
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_link(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_link *param,
	struct odt *st)
{
	const char	*sty;

	if ((sty = odt_style_add_text(st, LOWDOWN_LINK)) == NULL)
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

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_list(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_list *param,
	const char *name)
{

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	if (!HBUF_PUTSL(ob, "<text:list"))
		return 0;
	if (name != NULL && !hbuf_printf(ob,
	    " text:style-name=\"%s\"", name))
		return 0;
	if (!HBUF_PUTSL(ob, ">\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</text:list>\n");
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_listitem(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct lowdown_node *n,
	struct odt *st)
{
	size_t	 	 i, size;
	struct odt_sty	*sty;

	if (!(n->rndr_listitem.flags & HLIST_FL_DEF)) {
		assert(st->list != (size_t)-1);
		if (!HBUF_PUTSL(ob, "<text:list-item>"))
			return 0;
	}

	/*
	 * Non-definition, non-block lists have an initial paragraph
	 * that must link to the root list of the current tree.
	 */

	if (!(n->rndr_listitem.flags & HLIST_FL_DEF) &&
	    !(n->rndr_listitem.flags & HLIST_FL_BLOCK)) {
		assert(st->list != (size_t)-1);
		for (i = 0; i < st->stysz; i++)
			if (st->stys[i].type == LOWDOWN_PARAGRAPH &&
			    st->stys[i].fmt == ODT_STY_PARA &&
			    st->stys[i].foot == st->foot &&
			    st->stys[i].parent == st->list)
				break;
		if (i == st->stysz) {
			if ((sty = odt_style_add(st)) == NULL)
				return 0;
			sty->autosty = 1;
			sty->parent = st->list;
			sty->foot = st->foot;
			sty->fmt = ODT_STY_PARA;
			sty->type = LOWDOWN_PARAGRAPH;
			snprintf(sty->name, sizeof(sty->name),
				"P%zu", st->sty_P++);
		} else
			sty = &st->stys[i];

		if (!hbuf_printf(ob,
		    "<text:p text:style-name=\"%s\">", sty->name))
			return 0;
	}

	if (n->rndr_listitem.flags & HLIST_FL_UNCHECKED) {
		if (!HBUF_PUTSL(ob, "☐ "))
			return 0;
	}
	if (n->rndr_listitem.flags & HLIST_FL_CHECKED) {
		if (!HBUF_PUTSL(ob, "☑ "))
			return 0;
	}

	/* Cut off any trailing space. */

	if ((size = content->size) > 0) {
		while (size && content->data[size - 1] == '\n')
			size--;
		if (!hbuf_put(ob, content->data, size))
			return 0;
	}

	if (!(n->rndr_listitem.flags & HLIST_FL_DEF) &&
	    !(n->rndr_listitem.flags & HLIST_FL_BLOCK))
		if (!HBUF_PUTSL(ob, "</text:p>"))
			return 0;

	if (!(n->rndr_listitem.flags & HLIST_FL_DEF))
		if (!HBUF_PUTSL(ob, "</text:list-item>\n"))
			return 0;

	return 1;
}

/*
 * Return FALSE on failure, TRUE on success.
 */
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
	 * blockquotes, or link to the root list, if applicable.  The
	 * foot bits are because footer paragraphs inherit the footnote
	 * font.
	 */

	for (j = 0; j < st->stysz; j++)
		if (st->stys[j].type == LOWDOWN_PARAGRAPH &&
		    st->stys[j].parent == st->list &&
		    st->stys[j].foot == st->foot &&
		    st->stys[j].fmt == ODT_STY_PARA &&
		    st->stys[j].offs == st->offs)
			break;

	if (j == st->stysz) {
		if ((sty = odt_style_add(st)) == NULL)
			return 0;
		sty->autosty = 1;
		sty->foot = st->foot;
		sty->fmt = ODT_STY_PARA;
		sty->type = LOWDOWN_PARAGRAPH;
		sty->parent = st->list;
		sty->offs = st->offs;
		snprintf(sty->name, sizeof(sty->name),
			"P%zu", st->sty_P++);
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

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_html(struct lowdown_buf *ob,
	const struct lowdown_buf *param,
	const struct odt *st)
{

	if (st->flags & LOWDOWN_ODT_SKIP_HTML)
		return 1;
	return escape_htmlb(ob, param, st);
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_hrule(struct lowdown_buf *ob, struct odt *st)
{
	size_t	 	 i;
	struct odt_sty	*s;

	for (i = 0; i < st->stysz; i++)
		if (st->stys[i].type == LOWDOWN_HRULE &&
		    st->stys[i].foot == st->foot) {
			assert(st->stys[i].fmt == ODT_STY_PARA);
			break;
		}

	if (i == st->stysz) {
		if ((s = odt_style_add(st)) == NULL)
			return 0;
		s->type = LOWDOWN_HRULE;
		s->fmt = ODT_STY_PARA;
		s->foot = st->foot;
		strlcpy(s->name, "Horizontal_20_Line",
			sizeof(s->name));
	} else
		s = &st->stys[i];

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	return hbuf_printf(ob,
		"<text:p text:style-name=\"%s\"/>\n", s->name);
}

/*
 * TODO: not implemented yet.  Return FALSE on failure, TRUE on success.
 */
static int
rndr_image(struct lowdown_buf *ob,
	const struct rndr_image *param, 
	const struct odt *st)
{
	return 1;
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_table(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_table *param,
	struct odt *st)
{
	size_t		 i, pid;
	struct odt_sty	*s;

	/*
	 * First find the outer paragraph.  If we're in the footer, this
	 * must be linked to the footer; and if in a list, to the list.
	 * We don't do offset here: that's part of the table itself.
	 */

	for (pid = 0; pid < st->stysz; pid++)
		if (st->stys[pid].type == LOWDOWN_PARAGRAPH &&
		    st->stys[pid].fmt == ODT_STY_PARA &&
		    st->stys[pid].offs == 0 &&
		    st->stys[pid].foot == st->foot &&
		    st->stys[pid].parent == st->list)
			break;
	if (pid == st->stysz) {
		if ((s = odt_style_add(st)) == NULL)
			return 0;
		s->autosty = 1;
		s->parent = st->list;
		s->foot = st->foot;
		s->fmt = ODT_STY_PARA;
		s->type = LOWDOWN_PARAGRAPH;
		snprintf(s->name, sizeof(s->name),
			"P%zu", st->sty_P++);
	}

	/*
	 * Now the table itself.  Tables are only unique insofar as they
	 * have different offsets and possible are in lists.
	 */

	for (i = 0; i < st->stysz; i++)
		if (st->stys[i].type == LOWDOWN_TABLE_BLOCK &&
		    st->stys[i].parent == st->list &&
		    st->stys[i].foot == st->foot &&
		    st->stys[i].offs == st->offs)
			break;

	if (i == st->stysz) {
		if ((s = odt_style_add(st)) == NULL)
			return 0;
		s->autosty = 1;
		s->type = LOWDOWN_TABLE_BLOCK;
		s->fmt = ODT_STY_TBL;
		s->foot = st->foot;
		s->parent = st->list;
		s->offs = st->offs;
		snprintf(s->name, sizeof(s->name),
			"Table%zu", st->sty_Table++);
	} else
		s = &st->stys[i];

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;

	if (!hbuf_printf(ob,
	    "<text:p text:style-name=\"%s\">\n",
	    st->stys[pid].name))
		return 0;

	if (!hbuf_printf(ob,
	    "<draw:frame draw:style-name=\"fr1\""
	    " draw:name=\"Frame\""
	    " draw:z-index=\"0\">\n"
	    "<draw:text-box"
	    " fo:min-height=\"0.499cm\""
	    " fo:min-width=\"0.34cm\">\n"
	    "<table:table"
	    " table:style-name=\"%s\""
	    " table:name=\"%s\">\n"
	    "<table:table-column"
	    " table:number-columns-repeated=\"%zu\"/>\n",
	    s->name, s->name, param->columns))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	if (!HBUF_PUTSL(ob, "</table:table>\n"))
		return 0;
	if (!hbuf_printf(ob,
	    "</draw:text-box>\n</draw:frame>\n</text:p>\n"))
		return 0;
	return 1;
}

/*
 * Return FALSE on failure, TRUE on success.
 */
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

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_tablecell(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_table_cell *param,
	struct odt *st)
{
	size_t		 i;
	struct odt_sty	*s;

	/*
	 * Reference if we're in a footnote, as the paragraph will want
	 * to inherit the Footnote smaller font.
	 */

	for (i = 0; i < st->stysz; i++)
		if (st->stys[i].type == LOWDOWN_PARAGRAPH &&
		    st->stys[i].foot == st->foot &&
		    st->stys[i].fmt == ODT_STY_TBL_PARA)
			break;

	if (i == st->stysz) {
		if ((s = odt_style_add(st)) == NULL)
			return 0;
		s->autosty = 1;
		s->type = LOWDOWN_PARAGRAPH;
		s->foot = st->foot;
		s->fmt = ODT_STY_TBL_PARA;
		snprintf(s->name, sizeof(s->name),
			"P%zu", st->sty_P++);
	} else
		s = &st->stys[i];

	if (!hbuf_printf(ob,
	    "<table:table-cell office:value-type=\"string\">"
	    "<text:p text:style-name=\"%s\">", s->name))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "</text:p></table:table-cell>\n");
}

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_footnote_ref(struct lowdown_buf *ob,
	const struct rndr_footnote_ref *param,
	struct odt *st)
{
	const struct lowdown_node	*n;
	struct odt			 tmp;

	/* Don't allow nested footnotes. */

	if (st->foot)
		return 1;

	/* Look up footnote definition and exit if not found. */

	if (st->foots == NULL)
		return 1;
	TAILQ_FOREACH(n, &st->foots->children, entries)
		if (n->type != LOWDOWN_FOOTNOTE_DEF)
			continue;
		else if (n->rndr_footnote_def.num == param->num)
			break;
	if (n == NULL)
		return 1;

	/* Save state values. */

	tmp = *st;
	st->offs = 0;
	st->list = (size_t)-1;
	st->foot = 1;

	if (!hbuf_printf(ob,
	    "<text:note text:id=\"ftn%zu\""
	    " text:note-class=\"footnote\">"
	    "<text:note-citation>%zu</text:note-citation>"
	    "<text:note-body>\n", param->num, param->num))
		return 0;
	if (!rndr(ob, NULL, st, n))
		return 0;
	if (!HBUF_PUTSL(ob,
	    "</text:note-body></text:note>\n"))
		return 0;

	/* Restore state values. */

	st->offs = tmp.offs;
	st->list = tmp.list;
	st->foot = 0;
	return 1;
}

/*
 * Return FALSE on failure, TRUE on success.
 */
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

/*
 * Return FALSE on failure, TRUE on success.
 */
static int
rndr_root(struct lowdown_buf *ob, const struct lowdown_metaq *mq,
	const struct lowdown_buf *content, const struct odt *st)
{

	if ((st->flags & LOWDOWN_STANDALONE) && !HBUF_PUTSL(ob,
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<office:document\n"
	    " xmlns:calcext=\"urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0\"\n"
	    " xmlns:chart=\"urn:oasis:names:tc:opendocument:xmlns:chart:1.0\"\n"
	    " xmlns:config=\"urn:oasis:names:tc:opendocument:xmlns:config:1.0\"\n"
	    " xmlns:css3t=\"http://www.w3.org/TR/css3-text/\"\n"
	    " xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
	    " xmlns:dom=\"http://www.w3.org/2001/xml-events\"\n"
	    " xmlns:dr3d=\"urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0\"\n"
	    " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\"\n"
	    " xmlns:drawooo=\"http://openoffice.org/2010/draw\"\n"
	    " xmlns:field=\"urn:openoffice:names:experimental:ooo-ms-interop:xmlns:field:1.0\"\n"
	    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\"\n"
	    " xmlns:form=\"urn:oasis:names:tc:opendocument:xmlns:form:1.0\"\n"
	    " xmlns:formx=\"urn:openoffice:names:experimental:ooxml-odf-interop:xmlns:form:1.0\"\n"
	    " xmlns:grddl=\"http://www.w3.org/2003/g/data-view#\"\n"
	    " xmlns:loext=\"urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0\"\n"
	    " xmlns:math=\"http://www.w3.org/1998/Math/MathML\"\n"
	    " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\"\n"
	    " xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\"\n"
	    " xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\"\n"
	    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\"\n"
	    " xmlns:officeooo=\"http://openoffice.org/2009/office\"\n"
	    " xmlns:ooo=\"http://openoffice.org/2004/office\"\n"
	    " xmlns:oooc=\"http://openoffice.org/2004/calc\"\n"
	    " xmlns:ooow=\"http://openoffice.org/2004/writer\"\n"
	    " xmlns:rpt=\"http://openoffice.org/2005/report\"\n"
	    " xmlns:script=\"urn:oasis:names:tc:opendocument:xmlns:script:1.0\"\n"
	    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\"\n"
	    " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"\n"
	    " xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\"\n"
	    " xmlns:tableooo=\"http://openoffice.org/2009/table\"\n"
	    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\"\n"
	    " xmlns:xforms=\"http://www.w3.org/2002/xforms\"\n"
	    " xmlns:xhtml=\"http://www.w3.org/1999/xhtml\"\n"
	    " xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
	    " xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"\n"
	    " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
	    " office:mimetype=\"application/vnd.oasis.opendocument.text\"\n"
	    " office:version=\"1.3\">\n"))
		return 0;

	if ((st->flags & LOWDOWN_STANDALONE) &&
	    !odt_metaq_flush(ob, mq, st))
		return 0;

	/* 
	 * These fonts are only referenced in the non-automatic styles,
	 * so don't emit them for non-standalone mode.
	 */

	if ((st->flags & LOWDOWN_STANDALONE) && !HBUF_PUTSL(ob,
	    "<office:font-face-decls>\n"
  	    "<style:font-face style:name=\"OpenSymbol\""
	    " svg:font-family=\"OpenSymbol\""
	    " style:font-charset=\"x-symbol\"/>\n"
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

	if (!HBUF_PUTSL(ob, "<office:body>\n<office:text>\n"))
		return 0;
	if (!odt_changes_flush(ob, mq, st))
		return 0;

	if (!hbuf_putb(ob, content))
		return 0;

	if (!HBUF_PUTSL(ob, "</office:text>\n</office:body>\n"))
		return 0;

	if ((st->flags & LOWDOWN_STANDALONE) && !HBUF_PUTSL(ob,
	    "</office:document>\n"))
		return 0;
	return 1;
}

/*
 * Allocate a meta-data value on the queue "mq".  Return FALSE on
 * failure, TRUE on success.
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
rndr(struct lowdown_buf *ob,
	struct lowdown_metaq *mq, void *ref, 
	const struct lowdown_node *n)
{
	const struct lowdown_node	*child;
	struct lowdown_buf		*tmp;
	int32_t				 ent;
	struct odt			*st = ref;
	struct odt_sty			*sty = NULL;
	size_t				 curid = (size_t)-1, curoffs,
					 chngid = (size_t)-1;
	int				 ret = 1, rc = 1;
	void				*pp;

	if ((tmp = hbuf_new(64)) == NULL)
		return 0;

	/*
	 * Manage our position in the output.  If we're in a blockquote
	 * and not a list, then increment our indent.  If we're in a
	 * list, we're not allowed to have indents between the list and
	 * content (OpenDocument limitations), so don't touch the
	 * indentation.
	 */

	/*
	 * TODO: keep a "real offset" if we have an embedded table and
	 * want to set the width to be the real offset minus page width.
	 * Without doing so, list-embedded tables run off the right
	 * margin for OpenDocument reasons.
	 */

	switch (n->type) {
	case LOWDOWN_BLOCKQUOTE:
		if (st->list == (size_t)-1)
			st->offs++;
		break;
	case LOWDOWN_LIST:
		if (st->list != (size_t)-1)
			break;
		for (st->list = 0; st->list < st->stysz; st->list++) {
			if (st->stys[st->list].type != LOWDOWN_LIST)
				continue;
			if (st->stys[st->list].offs != st->offs)
				continue;
			if ((n->rndr_list.flags & HLIST_FL_UNORDERED) &&
			    st->stys[st->list].fmt != ODT_STY_UL)
				continue;
			if ((n->rndr_list.flags & HLIST_FL_ORDERED) &&
			    st->stys[st->list].fmt != ODT_STY_OL)
				continue;
			break;
		}
		if (st->list == st->stysz) {
			if ((sty = odt_style_add(st)) == NULL)
				return 0;
			sty->type = LOWDOWN_LIST;
			if (n->rndr_list.flags & HLIST_FL_ORDERED)
				sty->fmt = ODT_STY_OL;
			if (n->rndr_list.flags & HLIST_FL_UNORDERED)
				sty->fmt = ODT_STY_UL;
			sty->offs = st->offs;
			sty->autosty = 1;
			snprintf(sty->name, sizeof(sty->name),
				"L%zu", st->sty_L++);
		}
		curoffs = st->offs;
		st->offs = 0;
		curid = st->list;
		break;
	default:
		break;
	}

	/* Skip footnotes. */

	TAILQ_FOREACH(child, &n->children, entries) {
		if (child->type == LOWDOWN_FOOTNOTES_BLOCK)
			continue;
		if (!rndr(tmp, mq, st, child))
			goto out;
	}

	if (n->chng == LOWDOWN_CHNG_INSERT ||
	    n->chng == LOWDOWN_CHNG_DELETE) {
		pp = reallocarray(st->chngs,
			st->chngsz + 1, sizeof(struct odt_chng));
		if (pp == NULL)
			goto out;
		st->chngs = pp;
		st->chngs[st->chngsz].ins =
			n->chng == LOWDOWN_CHNG_INSERT;
		chngid = st->chngsz++;
		if (!hbuf_printf(ob,
		    "<text:change-start"
		    " text:change-id=\"ct%zu\"/>", chngid))
			return 0;
	}

	switch (n->type) {
	case LOWDOWN_ROOT:
		rc = rndr_root(ob, mq, tmp, st);
		break;
	case LOWDOWN_BLOCKCODE:
		rc = rndr_blockcode(ob, &n->rndr_blockcode, st);
		break;
	case LOWDOWN_META:
		if (n->chng != LOWDOWN_CHNG_DELETE)
			rc = rndr_meta(ob, tmp, mq, n, st);
		break;
	case LOWDOWN_HEADER:
		rc = rndr_header(ob, tmp, &n->rndr_header, st);
		break;
	case LOWDOWN_HRULE:
		rc = rndr_hrule(ob, st);
		break;
	case LOWDOWN_LIST:
		rc = rndr_list(ob, tmp, &n->rndr_list,
			curid == (size_t)-1 ?
			NULL : st->stys[curid].name);
		break;
	case LOWDOWN_LISTITEM:
		rc = rndr_listitem(ob, tmp, n, st);
		break;
	/* TODO */
	case LOWDOWN_DEFINITION_DATA:
	/* TODO */
	case LOWDOWN_DEFINITION_TITLE:
	case LOWDOWN_PARAGRAPH:
		rc = rndr_paragraph(ob, tmp, st);
		break;
	case LOWDOWN_TABLE_BLOCK:
		rc = rndr_table(ob, tmp, &n->rndr_table, st);
		break;
	case LOWDOWN_TABLE_ROW:
		rc = rndr_tablerow(ob, tmp);
		break;
	case LOWDOWN_TABLE_CELL:
		rc = rndr_tablecell(ob, tmp, &n->rndr_table_cell, st);
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
		rc = rndr_footnote_ref(ob, &n->rndr_footnote_ref, st);
		break;
	case LOWDOWN_MATH_BLOCK:
		rc = rndr_math(ob, &n->rndr_math, st);
		break;
	case LOWDOWN_RAW_HTML:
		rc = rndr_html(ob, &n->rndr_raw_html.text, st);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rc = escape_htmlb(ob, &n->rndr_normal_text.text, st);
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

	if (n->chng == LOWDOWN_CHNG_INSERT ||
	    n->chng == LOWDOWN_CHNG_DELETE) {
		assert(chngid != (size_t)-1);
		if (!hbuf_printf(ob,
		    "<text:change-end"
		    " text:change-id=\"ct%zu\"/>", chngid))
			return 0;
	}

	switch (n->type) {
	case LOWDOWN_BLOCKQUOTE:
		if (st->list == (size_t)-1)
			st->offs--;
		break;
	case LOWDOWN_LIST:
		if (curid != (size_t)-1) {
			st->list = (size_t)-1;
			st->offs = curoffs;
		}
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
	st->foot = 0;
	st->foots = NULL;
	st->sty_T = st->sty_L = st->sty_P = st->sty_Table = 1;
	st->chngs = NULL;
	st->chngsz = 0;

	/* 
	 * Keep tabs of where the footnote block is, if any.  This is
	 * because we need to inline footnote definitions directly where
	 * we have the references.
	 */

	if (n->type == LOWDOWN_ROOT)
		TAILQ_FOREACH_REVERSE(st->foots,
		    &n->children, lowdown_nodeq, entries)
			if (st->foots->type == LOWDOWN_FOOTNOTES_BLOCK)
				break;

	rc = rndr(ob, &metaq, st, n);

	free(st->stys);
	free(st->chngs);
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
