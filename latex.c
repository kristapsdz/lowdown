/*	$Id$ */
/*
 * Copyright (c) 2020--2021 Kristaps Dzonsons <kristaps@bsd.lv>
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

struct latex {
	unsigned int	oflags; /* same as in lowdown_opts */
	ssize_t		headers_offs; /* header offset */
};

/*
 * Return zero on failure, non-zero on success.
 */
static int
rndr_escape_text(struct lowdown_buf *ob, const char *data, size_t sz)
{
	size_t	 i;

	for (i = 0; i < sz; i++)
		switch (data[i]) {
		case '&':
		case '%':
		case '$':
		case '#':
		case '_':
		case '{':
		case '}':
			if (!hbuf_putc(ob, '\\'))
				return 0;
			if (!hbuf_putc(ob, data[i]))
				return 0;
			break;
		case '~':
			if (!HBUF_PUTSL(ob, "\\textasciitilde{}"))
				return 0;
			break;
		case '^':
			if (!HBUF_PUTSL(ob, "\\textasciicircum{}"))
				return 0;
			break;
		case '\\':
			if (!HBUF_PUTSL(ob, "\\textbackslash{}"))
				return 0;
			break;
		default:
			if (!hbuf_putc(ob, data[i]))
				return 0;
			break;
		}

	return 1;
}

/*
 * Return zero on failure, non-zero on success.
 */
static int
rndr_escape(struct lowdown_buf *ob, const struct lowdown_buf *dat)
{
	
	return rndr_escape_text(ob, dat->data, dat->size);
}

static int
rndr_autolink(struct lowdown_buf *ob,
	const struct rndr_autolink *param)
{

	if (param->link.size == 0)
		return 1;
	if (!HBUF_PUTSL(ob, "\\url{"))
		return 0;
	if (param->type == HALINK_EMAIL && !HBUF_PUTSL(ob, "mailto:"))
		return 0;
	if (!rndr_escape(ob, &param->link))
		return 0;
	return HBUF_PUTSL(ob, "}");
}

static int
rndr_entity(struct lowdown_buf *ob,
	const struct rndr_entity *param)
{
	const char	*tex;
	unsigned char	 texflags;

	tex = entity_find_tex(&param->text, &texflags);
	if (tex == NULL)
		return rndr_escape(ob, &param->text);

	if (texflags & TEX_ENT_ASCII)
		return hbuf_puts(ob, tex);
	if (texflags & TEX_ENT_MATH)
		return hbuf_printf(ob, "$\\mathrm{\\%s}$", tex);
	return hbuf_printf(ob, "\\%s", tex);
}

static int
rndr_blockcode(struct lowdown_buf *ob,
	const struct rndr_blockcode *param)
{

	if (ob->size && !HBUF_PUTSL(ob, "\n"))
		return 0;

#if 0
	HBUF_PUTSL(ob, "\\begin{lstlisting}");
	if (lang->size) {
		HBUF_PUTSL(ob, "[language=");
		rndr_escape(ob, lang);
		HBUF_PUTSL(ob, "]\n\n");
	} else
		HBUF_PUTSL(ob, "\n");
#else
	HBUF_PUTSL(ob, "\\begin{verbatim}\n");
#endif
	if (!hbuf_putb(ob, &param->text))
		return 0;
#if 0
	HBUF_PUTSL(ob, "\\end{lstlisting}\n");
#else
	return HBUF_PUTSL(ob, "\\end{verbatim}\n");
#endif
}

static int
rndr_definition_title(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "\\item ["))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "] ");
}

static int
rndr_definition(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "\\begin{description}\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "\\end{description}\n");
}

static int
rndr_blockquote(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (ob->size && !HBUF_PUTSL(ob, "\n"))
		return 0;
	if (!HBUF_PUTSL(ob, "\\begin{quotation}\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "\\end{quotation}\n");
}

static int
rndr_codespan(struct lowdown_buf *ob,
	const struct rndr_codespan *param)
{
#if 0
	HBUF_PUTSL(ob, "\\lstinline{");
	hbuf_putb(ob, text);
#else
	if (!HBUF_PUTSL(ob, "\\texttt{"))
		return 0;
	if (!rndr_escape(ob, &param->text))
		return 0;
#endif
	return HBUF_PUTSL(ob, "}");
}

static int
rndr_triple_emphasis(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "\\textbf{\\emph{"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "}}");
}

static int
rndr_double_emphasis(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "\\textbf{"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "}");
}

static int
rndr_emphasis(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "\\emph{"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "}");
}

static int
rndr_highlight(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "\\underline{"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "}");
}

static int
rndr_linebreak(struct lowdown_buf *ob)
{

	return HBUF_PUTSL(ob, "\\linebreak\n");
}

static int
rndr_header(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_header *param,
	const struct latex *st)
{
	const char	*type;
	ssize_t		 level;

	if (ob->size && !HBUF_PUTSL(ob, "\n"))
		return 0;

	level = (ssize_t)param->level + st->headers_offs;
	if (level < 1)
		level = 1;

	switch (level) {
	case 1:
		type = "\\section";
		break;
	case 2:
		type = "\\subsection";
		break;
	case 3:
		type = "\\subsubsection";
		break;
	case 4:
		type = "\\paragraph";
		break;
	default:
		type = "\\subparagraph";
		break;
	}

	if (!hbuf_puts(ob, type))
		return 0;
	if (!(st->oflags & LOWDOWN_LATEX_NUMBERED) &&
  	    !HBUF_PUTSL(ob, "*"))
		return 0;
	if (!HBUF_PUTSL(ob, "{"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "}\n");
}

static int
rndr_link(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_link *param)
{

	if (!HBUF_PUTSL(ob, "\\href{"))
		return 0;
	if (!rndr_escape(ob, &param->link))
		return 0;
	if (!HBUF_PUTSL(ob, "}{"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "}");
}

static int
rndr_list(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_list *param)
{
	const char	*type;

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;

	/* TODO: HLIST_FL_ORDERED and param->start */

	type = (param->flags & HLIST_FL_ORDERED) ?
		"enumerate" : "itemize";

	if (!hbuf_printf(ob, "\\begin{%s}\n", type))
		return 0;
	if (!(param->flags & HLIST_FL_BLOCK) &&
	    !HBUF_PUTSL(ob, "\\itemsep -0.2em\n"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return hbuf_printf(ob, "\\end{%s}\n", type);
}

static int
rndr_listitem(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	const struct rndr_listitem *param)
{
	size_t	 size;

	/* Only emit <li> if we're not a <dl> list. */

	if (!(param->flags & HLIST_FL_DEF)) {
		if (!HBUF_PUTSL(ob, "\\item"))
			return 0;
		if ((param->flags & HLIST_FL_CHECKED) &&
		    !HBUF_PUTSL(ob, "[$\\rlap{$\\checkmark$}\\square$]"))
			return 0;
		if ((param->flags & HLIST_FL_UNCHECKED) &&
		    !HBUF_PUTSL(ob, "[$\\square$]"))
			return 0;
		if (!HBUF_PUTSL(ob, " "))
			return 0;
	}

	/* Cut off any trailing space. */

	if ((size = content->size) > 0) {
		while (size && content->data[size - 1] == '\n')
			size--;
		if (!hbuf_put(ob, content->data, size))
			return 0;
	}

	return HBUF_PUTSL(ob, "\n");
}

static int
rndr_paragraph(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{
	size_t	i = 0;

	if (content->size == 0)
		return 1;
	while (i < content->size &&
	       isspace((unsigned char)content->data[i])) 
		i++;
	if (i == content->size)
		return 1;

	if (!HBUF_PUTSL(ob, "\n"))
		return 0;
	if (!hbuf_put(ob, content->data + i, content->size - i))
		return 0;
	return HBUF_PUTSL(ob, "\n");
}

static int
rndr_raw_block(struct lowdown_buf *ob,
	const struct rndr_blockhtml *param,
	const struct latex *st)
{
	size_t	org = 0, sz = param->text.size;

	if (st->oflags & LOWDOWN_LATEX_SKIP_HTML)
		return 1;
	while (sz > 0 && param->text.data[sz - 1] == '\n')
		sz--;
	while (org < sz && param->text.data[org] == '\n')
		org++;
	if (org >= sz)
		return 1;

	if (ob->size && !HBUF_PUTSL(ob, "\n"))
		return 0;
	if (!HBUF_PUTSL(ob, "\\begin{verbatim}\n"))
		return 0;
	if (!hbuf_put(ob, param->text.data + org, sz - org))
		return 0;
	return HBUF_PUTSL(ob, "\\end{verbatim}\n");
}

static int
rndr_hrule(struct lowdown_buf *ob)
{

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	return HBUF_PUTSL(ob, "\\noindent\\hrulefill\n");
}

static int
rndr_image(struct lowdown_buf *ob,
	const struct rndr_image *param)
{
	const char	*cp;
	char		 dimbuf[32];
	unsigned int	 x, y;
	float		 pct;
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

	/* Extended attributes override dimensions. */

	if (!HBUF_PUTSL(ob, "\\includegraphics["))
		return 0;
	if (param->attr_width.size || param->attr_height.size) {
		if (param->attr_width.size) {
			memset(dimbuf, 0, sizeof(dimbuf));
			memcpy(dimbuf, param->attr_width.data, 
				param->attr_width.size);

			/* Try to parse as a percentage. */

			if (sscanf(dimbuf, "%e%%", &pct) == 1) {
				if (!hbuf_printf(ob, "width=%.2f"
				     "\\linewidth", pct / 100.0))
					return 0;
			} else {
				if (!hbuf_printf(ob, "width=%.*s", 
				    (int)param->attr_width.size, 
				    param->attr_width.data))
					return 0;
			}
		}
		if (param->attr_height.size) {
			if (param->attr_width.size && 
			    !HBUF_PUTSL(ob, ", "))
				return 0;
			if (!hbuf_printf(ob, "height=%.*s", 
			    (int)param->attr_height.size, 
			    param->attr_height.data))
				return 0;
		}
	} else if (rc > 0) {
		if (!hbuf_printf(ob, "width=%upx", x))
			return 0;
		if (rc > 1 && !hbuf_printf(ob, ", height=%upx", y))
			return 0;
	}

	if (!HBUF_PUTSL(ob, "]{"))
		return 0;
	cp = memrchr(param->link.data, '.', param->link.size);
	if (cp != NULL) {
		if (!HBUF_PUTSL(ob, "{"))
			return 0;
		if (!rndr_escape_text
		    (ob, param->link.data, cp - param->link.data))
			return 0;
		if (!HBUF_PUTSL(ob, "}"))
			return 0;
		if (!rndr_escape_text(ob, cp, 
		    param->link.size - (cp - param->link.data)))
			return 0;
	} else {
		if (!rndr_escape(ob, &param->link))
			return 0;
	}
	return HBUF_PUTSL(ob, "}");
}

static int
rndr_raw_html(struct lowdown_buf *ob,
	const struct rndr_raw_html *param,
	const struct latex *st)
{

	if (st->oflags & LOWDOWN_LATEX_SKIP_HTML)
		return 1;
	return rndr_escape(ob, &param->text);
}

static int
rndr_table(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	/* Open the table in rndr_table_header. */

	if (ob->size && !hbuf_putc(ob, '\n'))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	if (!HBUF_PUTSL(ob, "\\end{tabular}\n"))
		return 0;
	return HBUF_PUTSL(ob, "\\end{center}\n");
}

static int
rndr_table_header(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	const struct rndr_table_header *param)
{
	size_t	 i;

	if (!HBUF_PUTSL(ob, "\\begin{center}"))
		return 0;
	if (!HBUF_PUTSL(ob, "\\begin{tabular}{ "))
		return 0;

	/* FIXME: alignment */

	for (i = 0; i < param->columns; i++)
		if (!HBUF_PUTSL(ob, "c "))
			return 0;
	if (!HBUF_PUTSL(ob, "}\n"))
		return 0;
	return hbuf_putb(ob, content);
}

static int
rndr_tablecell(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	const struct rndr_table_cell *param)
{

	if (!hbuf_putb(ob, content))
		return 0;
	return (param->col < param->columns - 1) ?
		HBUF_PUTSL(ob, " & ") :
		HBUF_PUTSL(ob, "  \\\\\n");
}

static int
rndr_superscript(struct lowdown_buf *ob,
	const struct lowdown_buf *content)
{

	if (!HBUF_PUTSL(ob, "\\textsuperscript{"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	return HBUF_PUTSL(ob, "}");
}

static int
rndr_normal_text(struct lowdown_buf *ob,
	const struct rndr_normal_text *param)
{

	return rndr_escape(ob, &param->text);
}

static int
rndr_footnote_def(struct lowdown_buf *ob,
	const struct lowdown_buf *content, 
	const struct lowdown_node *n,
	const struct rndr_footnote_def *param)
{

	if (!hbuf_printf(ob, "\\footnotetext[%zu]{", param->num))
		return 0;
	if (n->chng == LOWDOWN_CHNG_INSERT &&
	    !HBUF_PUTSL(ob, "\\textcolor{blue}{"))
		return 0;
	if (n->chng == LOWDOWN_CHNG_DELETE &&
	    !HBUF_PUTSL(ob, "\\textcolor{red}{"))
		return 0;
	if (!hbuf_putb(ob, content))
		return 0;
	if ((n->chng == LOWDOWN_CHNG_INSERT ||
	     n->chng == LOWDOWN_CHNG_DELETE) &&
	    !HBUF_PUTSL(ob, "}"))
		return 0;
	return HBUF_PUTSL(ob, "}\n");
}

static int
rndr_footnote_ref(struct lowdown_buf *ob,
	const struct rndr_footnote_ref *param)
{

	return hbuf_printf(ob, "\\footnotemark[%zu]", param->num);
}

static int
rndr_math(struct lowdown_buf *ob,
	const struct rndr_math *param)
{

	if (param->blockmode && !HBUF_PUTSL(ob, "\\["))
		return 0;
	else if (!param->blockmode && !HBUF_PUTSL(ob, "\\("))
		return 0;
	if (!hbuf_putb(ob, &param->text))
		return 0;
	if (param->blockmode && !HBUF_PUTSL(ob, "\\]"))
		return 0;
	else if (!param->blockmode && !HBUF_PUTSL(ob, "\\)"))
		return 0;
	return 1;
}

static int
rndr_doc_footer(struct lowdown_buf *ob, const struct latex *st)
{

	if (st->oflags & LOWDOWN_STANDALONE)
		return HBUF_PUTSL(ob, "\\end{document}\n");
	return 1;
}

static int
rndr_doc_header(struct lowdown_buf *ob,
	const struct lowdown_metaq *mq, const struct latex *st)
{
	const struct lowdown_meta	*m;
	const char			*author = NULL, *title = NULL,
					*affil = NULL, *date = NULL,
					*rcsauthor = NULL, 
					*rcsdate = NULL;

	if (!(st->oflags & LOWDOWN_STANDALONE))
		return 1;

	if (!HBUF_PUTSL(ob, 
	    "\\documentclass[11pt,a4paper]{article}\n"
	    "\\usepackage{xcolor}\n"
	    "\\usepackage{graphicx}\n"
	    "\\usepackage[utf8]{inputenc}\n"
	    "\\usepackage[T1]{fontenc}\n"
	    "\\usepackage{textcomp}\n"
	    "\\usepackage{lmodern}\n"
	    "\\usepackage{hyperref}\n"
	    "\\usepackage{amsmath}\n"
	    "\\usepackage{amssymb}\n"
	    "\\begin{document}\n"))
		return 0;

	TAILQ_FOREACH(m, mq, entries)
		if (strcasecmp(m->key, "author") == 0)
			author = m->value;
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

	/* Overrides. */

	if (title == NULL)
		title = "Untitled article";
	if (rcsauthor != NULL)
		author = rcsauthor;
	if (rcsdate != NULL)
		date = rcsdate;

	if (!hbuf_printf(ob, "\\title{%s}\n", title))
		return 0;

	if (author != NULL) {
		if (!hbuf_printf(ob, "\\author{%s", author))
			return 0;
		if (affil != NULL && 
		    !hbuf_printf(ob, " \\\\ %s", affil))
			return 0;
		if (!HBUF_PUTSL(ob, "}\n"))
			return 0;
	}

	if (date != NULL && !hbuf_printf(ob, "\\date{%s}\n", date))
		return 0;

	return HBUF_PUTSL(ob, "\\maketitle\n");
}

static int
rndr_meta(struct lowdown_buf *ob,
	const struct lowdown_buf *content,
	struct lowdown_metaq *mq,
	const struct lowdown_node *n, struct latex *st)
{
	struct lowdown_meta	*m;
	ssize_t			 val;
	const char		*ep;

	if ((m = calloc(1, sizeof(struct lowdown_meta))) == NULL)
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
	struct lowdown_metaq *mq, void *arg, 
	const struct lowdown_node *n)
{
	struct lowdown_buf		*tmp;
	struct latex			*st = arg;
	const struct lowdown_node	*child;
	int				 ret = 0, rc = 1;

	if ((tmp = hbuf_new(64)) == NULL)
		return 0;

	TAILQ_FOREACH(child, &n->children, entries)
		if (!rndr(tmp, mq, st, child))
			goto out;

	/*
	 * These elements can be put in either a block or an inline
	 * context, so we're safe to just use them and forget.
	 */

	if (n->chng == LOWDOWN_CHNG_INSERT && 
	    n->type != LOWDOWN_FOOTNOTE_DEF &&
	    !HBUF_PUTSL(ob, "{\\color{blue} "))
		goto out;
	if (n->chng == LOWDOWN_CHNG_DELETE &&
	    n->type != LOWDOWN_FOOTNOTE_DEF &&
	    !HBUF_PUTSL(ob, "{\\color{red} "))
		goto out;

	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
		rc = rndr_blockcode(ob, &n->rndr_blockcode);
		break;
	case LOWDOWN_BLOCKQUOTE:
		rc = rndr_blockquote(ob, tmp);
		break;
	case LOWDOWN_DEFINITION:
		rc = rndr_definition(ob, tmp);
		break;
	case LOWDOWN_DEFINITION_TITLE:
		rc = rndr_definition_title(ob, tmp);
		break;
	case LOWDOWN_DOC_HEADER:
		rc = rndr_doc_header(ob, mq, st);
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
		rc = rndr_list(ob, tmp, &n->rndr_list);
		break;
	case LOWDOWN_LISTITEM:
		rc = rndr_listitem(ob, tmp, &n->rndr_listitem);
		break;
	case LOWDOWN_PARAGRAPH:
		rc = rndr_paragraph(ob, tmp);
		break;
	case LOWDOWN_TABLE_BLOCK:
		rc = rndr_table(ob, tmp);
		break;
	case LOWDOWN_TABLE_HEADER:
		rc = rndr_table_header(ob, tmp, &n->rndr_table_header);
		break;
	case LOWDOWN_TABLE_CELL:
		rc = rndr_tablecell(ob, tmp, &n->rndr_table_cell);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rc = rndr_footnote_def
			(ob, tmp, n, &n->rndr_footnote_def);
		break;
	case LOWDOWN_BLOCKHTML:
		rc = rndr_raw_block(ob, &n->rndr_blockhtml, st);
		break;
	case LOWDOWN_LINK_AUTO:
		rc = rndr_autolink(ob, &n->rndr_autolink);
		break;
	case LOWDOWN_CODESPAN:
		rc = rndr_codespan(ob, &n->rndr_codespan);
		break;
	case LOWDOWN_DOUBLE_EMPHASIS:
		rc = rndr_double_emphasis(ob, tmp);
		break;
	case LOWDOWN_EMPHASIS:
		rc = rndr_emphasis(ob, tmp);
		break;
	case LOWDOWN_HIGHLIGHT:
		rc = rndr_highlight(ob, tmp);
		break;
	case LOWDOWN_IMAGE:
		rc = rndr_image(ob, &n->rndr_image);
		break;
	case LOWDOWN_LINEBREAK:
		rc = rndr_linebreak(ob);
		break;
	case LOWDOWN_LINK:
		rc = rndr_link(ob, tmp, &n->rndr_link);
		break;
	case LOWDOWN_TRIPLE_EMPHASIS:
		rc = rndr_triple_emphasis(ob, tmp);
		break;
	case LOWDOWN_SUPERSCRIPT:
		rc = rndr_superscript(ob, tmp);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		rc = rndr_footnote_ref(ob, &n->rndr_footnote_ref);
		break;
	case LOWDOWN_MATH_BLOCK:
		rc = rndr_math(ob, &n->rndr_math);
		break;
	case LOWDOWN_RAW_HTML:
		rc = rndr_raw_html(ob, &n->rndr_raw_html, st);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rc = rndr_normal_text(ob, &n->rndr_normal_text);
		break;
	case LOWDOWN_ENTITY:
		rc = rndr_entity(ob, &n->rndr_entity);
		break;
	default:
		rc = hbuf_putb(ob, tmp);
		break;
	}
	if (!rc)
		goto out;

	if ((n->chng == LOWDOWN_CHNG_INSERT ||
	     n->chng == LOWDOWN_CHNG_DELETE) &&
	    n->type != LOWDOWN_FOOTNOTE_DEF &&
	    !HBUF_PUTSL(ob, "}"))
		goto out;

	ret = 1;
out:
	hbuf_free(tmp);
	return ret;
}

int
lowdown_latex_rndr(struct lowdown_buf *ob,
	void *arg, const struct lowdown_node *n)
{
	struct latex		*st = arg;
	struct lowdown_metaq	 metaq;
	int			 rc;

	TAILQ_INIT(&metaq);
	st->headers_offs = 1;

	rc = rndr(ob, &metaq, st, n);

	lowdown_metaq_free(&metaq);
	return rc;
}

void *
lowdown_latex_new(const struct lowdown_opts *opts)
{
	struct latex	*p;

	if ((p = calloc(1, sizeof(struct latex))) == NULL)
		return NULL;

	p->oflags = opts == NULL ? 0 : opts->oflags;
	return p;
}

void
lowdown_latex_free(void *arg)
{

	free(arg);
}
