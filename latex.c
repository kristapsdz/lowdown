/*	$Id$ */
/*
 * Copyright (c) 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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
};

static void
rndr_escape_text(hbuf *ob, const char *data, size_t sz)
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
			hbuf_putc(ob, '\\');
			hbuf_putc(ob, data[i]);
			break;
		case '~':
			HBUF_PUTSL(ob, "\\textasciitilde{}");
			break;
		case '^':
			HBUF_PUTSL(ob, "\\textasciicircum{}");
			break;
		case '\\':
			HBUF_PUTSL(ob, "\\textbackslash{}");
			break;
		default:
			hbuf_putc(ob, data[i]);
			break;
		}
}

static void
rndr_escape(hbuf *ob, const hbuf *dat)
{
	
	rndr_escape_text(ob, dat->data, dat->size);
}

static void
rndr_autolink(hbuf *ob, const hbuf *link, enum halink_type type)
{

	if (link->size == 0)
		return;
	HBUF_PUTSL(ob, "\\url{");
	if (type == HALINK_EMAIL)
		HBUF_PUTSL(ob, "mailto:");
	rndr_escape(ob, link);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_blockcode(hbuf *ob, const hbuf *text, const hbuf *lang)
{
	if (ob->size) 
		HBUF_PUTSL(ob, "\n");

#if 0
	HBUF_PUTSL(ob, "\\begin{lstlisting}");
	if (lang->size) {
		HBUF_PUTSL(ob, "[language=");
		rndr_escape(ob, lang);
		HBUF_PUTSL(ob, "]\n");
	} else
#else
	HBUF_PUTSL(ob, "\\begin{verbatim}");
#endif
		HBUF_PUTSL(ob, "\n");
	hbuf_putb(ob, text);
#if 0
	HBUF_PUTSL(ob, "\\end{lstlisting}\n");
#else
	HBUF_PUTSL(ob, "\\end{verbatim}\n");
#endif
}

static void
rndr_definition_title(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "\\item [");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "] ");
}

static void
rndr_definition(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "\\begin{description}\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "\\end{description}\n");
}

static void
rndr_blockquote(hbuf *ob, const hbuf *content)
{
	if (ob->size)
		HBUF_PUTSL(ob, "\n");
	HBUF_PUTSL(ob, "\\begin{quotation}\n");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "\\end{quotation}\n");
}

static void
rndr_codespan(hbuf *ob, const hbuf *text)
{

#if 0
	HBUF_PUTSL(ob, "\\lstinline{");
#else
	HBUF_PUTSL(ob, "\\texttt{");
#endif
	rndr_escape(ob, text);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_triple_emphasis(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "\\textbf{\\emph{");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "}}");
}

static void
rndr_double_emphasis(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "\\textbf{");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_emphasis(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "\\emph{");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_highlight(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "\\underline{");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_linebreak(hbuf *ob)
{

	HBUF_PUTSL(ob, "\\linebreak\n");
}

static void
rndr_header(hbuf *ob, const hbuf *content, size_t level)
{

	if (ob->size)
		HBUF_PUTSL(ob, "\n");

	switch (level) {
	case 0:
	case 1:
		HBUF_PUTSL(ob, "\\section{");
		break;
	case 2:
		HBUF_PUTSL(ob, "\\subsection{");
		break;
	case 3:
		HBUF_PUTSL(ob, "\\subsubsection{");
		break;
	case 4:
		HBUF_PUTSL(ob, "\\paragraph{");
		break;
	default:
		HBUF_PUTSL(ob, "\\subparagraph{");
		break;

	}

	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_link(hbuf *ob, const hbuf *content, const hbuf *link)
{

	HBUF_PUTSL(ob, "\\href{");
	rndr_escape(ob, link);
	HBUF_PUTSL(ob, "}{");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_list(hbuf *ob, const hbuf *content, const struct rndr_list *p)
{

	if (ob->size)
		hbuf_putc(ob, '\n');

	/* TODO: HLIST_FL_ORDERED and p->start */

	if (p->flags & HLIST_FL_ORDERED)
		HBUF_PUTSL(ob, "\\begin{enumerate}\n");
	else
		HBUF_PUTSL(ob, "\\begin{itemize}\n");

	hbuf_putb(ob, content);

	if (p->flags & HLIST_FL_ORDERED)
		HBUF_PUTSL(ob, "\\end{enumerate}\n");
	else
		HBUF_PUTSL(ob, "\\end{itemize}\n");
}

static void
rndr_listitem(hbuf *ob, const hbuf *content,
	const struct lowdown_node *n)
{
	size_t	 size;
#if 0
	int	 blk = 0;
#endif

	/*
	 * If we're in block mode (which can be assigned post factum in
	 * the parser), make sure that we have an extra <p> around
	 * non-block content.
	 */

#if 0
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
#endif

	/* Only emit <li> if we're not a <dl> list. */

	if (!(n->rndr_listitem.flags & HLIST_FL_DEF))
		HBUF_PUTSL(ob, "\\item ");
#if 0
	if (blk)
		HBUF_PUTSL(ob, "<p>");
#endif

	/* Cut off any trailing space. */

	if ((size = content->size) > 0) {
		while (size && content->data[size - 1] == '\n')
			size--;
		hbuf_put(ob, content->data, size);
	}

	HBUF_PUTSL(ob, "\n");
#if 0
	if (blk)
		HBUF_PUTSL(ob, "</p>");
#endif
}

static void
rndr_paragraph(hbuf *ob, const hbuf *content)
{
	size_t	i = 0;

	if (content->size == 0)
		return;

	while (i < content->size &&
	       isspace((unsigned char)content->data[i])) 
		i++;

	if (i == content->size)
		return;

	HBUF_PUTSL(ob, "\n");
	hbuf_put(ob, content->data + i, content->size - i);
	HBUF_PUTSL(ob, "\n");
}

static void
rndr_raw_block(hbuf *ob, const hbuf *text, const struct latex *st)
{
	size_t	org = 0, sz = text->size;

	if (st->oflags & LOWDOWN_LATEX_SKIP_HTML)
		return;

	while (sz > 0 && text->data[sz - 1] == '\n')
		sz--;
	while (org < sz && text->data[org] == '\n')
		org++;
	if (org >= sz)
		return;

	if (ob->size)
		HBUF_PUTSL(ob, "\n");
	HBUF_PUTSL(ob, "\\begin{verbatim}\n");
	hbuf_put(ob, text->data + org, sz - org);
	HBUF_PUTSL(ob, "\\end{verbatim}\n");
}

static void
rndr_hrule(hbuf *ob)
{

	if (ob->size)
		hbuf_putc(ob, '\n');
	HBUF_PUTSL(ob, "\\noindent\\hrulefill\n");
}

static void
rndr_image(hbuf *ob, const hbuf *link, const hbuf *title, 
	const hbuf *dims, const hbuf *alt)
{
	const char	*cp;

	HBUF_PUTSL(ob, "\\includegraphics{");
	if ((cp = memrchr(link->data, '.', link->size)) != NULL) {
		HBUF_PUTSL(ob, "{");
		rndr_escape_text(ob, link->data, cp - link->data);
		HBUF_PUTSL(ob, "}");
		rndr_escape_text(ob, cp, link->size - (cp - link->data));
	} else
		rndr_escape(ob, link);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_raw_html(hbuf *ob, const hbuf *text, const struct latex *st)
{

	if (st->oflags & LOWDOWN_LATEX_SKIP_HTML)
		return;
	rndr_escape(ob, text);
}

static void
rndr_table(hbuf *ob, const hbuf *content)
{

	/* Open the table in rndr_table_header. */

	if (ob->size)
		hbuf_putc(ob, '\n');
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "\\end{tabular}\n");
	HBUF_PUTSL(ob, "\\end{center}\n");
}

static void
rndr_table_header(hbuf *ob, const hbuf *content, 
	const enum htbl_flags *fl, size_t columns)
{
	size_t	 i;

	HBUF_PUTSL(ob, "\\begin{center}");
	HBUF_PUTSL(ob, "\\begin{tabular}{ ");
	for (i = 0; i < columns; i++)
		HBUF_PUTSL(ob, "c ");
	HBUF_PUTSL(ob, "}\n");
	hbuf_putb(ob, content);
}

static void
rndr_tablecell(hbuf *ob, const hbuf *content, 
	enum htbl_flags flags, size_t col, size_t columns)
{

	hbuf_putb(ob, content);
	if (col < columns - 1)
		HBUF_PUTSL(ob, " & ");
	else
		HBUF_PUTSL(ob, "  \\\\\n");
}

static void
rndr_superscript(hbuf *ob, const hbuf *content)
{

	HBUF_PUTSL(ob, "\\textsuperscript{");
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "}");
}

static void
rndr_normal_text(hbuf *ob, const hbuf *content)
{

	rndr_escape(ob, content);
}

static void
rndr_footnote_def(hbuf *ob, const hbuf *content, size_t num)
{

	hbuf_printf(ob, "\\footnotetext[%zu]{", num);
	hbuf_putb(ob, content);
	HBUF_PUTSL(ob, "}\n");
}

static void
rndr_footnote_ref(hbuf *ob, size_t num)
{

	hbuf_printf(ob, "\\footnotemark[%zu]", num);
}

static void
rndr_math(hbuf *ob, const hbuf *text, int block)
{

	if (block)
		HBUF_PUTSL(ob, "\\[");
	else
		HBUF_PUTSL(ob, "\\(");

	hbuf_putb(ob, text);

	if (block)
		HBUF_PUTSL(ob, "\\]");
	else
		HBUF_PUTSL(ob, "\\)");
}

static void
rndr_doc_footer(hbuf *ob, const struct latex *st)
{

	if (st->oflags & LOWDOWN_STANDALONE)
		HBUF_PUTSL(ob, "\\end{document}\n");
}

static void
rndr_doc_header(hbuf *ob, const hbuf *content,
	const struct lowdown_metaq *mq, const struct latex *st)
{
	const struct lowdown_meta	*m;
	const char			*author = NULL, *title = NULL,
					*affil = NULL, *date = NULL;

	if (!(st->oflags & LOWDOWN_STANDALONE))
		return;
	HBUF_PUTSL(ob, 
	      "\\documentclass[11pt,a4paper]{article}\n"
	      "\\usepackage{xcolor}\n"
	      "\\usepackage{graphicx}\n"
	      "\\usepackage[utf8]{inputenc}\n"
	      "\\usepackage[T1]{fontenc}\n"
	      "\\usepackage{textcomp}\n"
	      "\\usepackage{lmodern}\n"
	      "\\usepackage{hyperref}\n"
	      "\\usepackage{listings}\n"
	      "\\usepackage[parfill]{parskip}\n"
	      "\\begin{document}\n");

	TAILQ_FOREACH(m, mq, entries)
		if (strcasecmp(m->key, "author") == 0)
			author = m->value;
		else if (strcasecmp(m->key, "affiliation") == 0)
			affil = m->value;
		else if (strcasecmp(m->key, "date") == 0)
			date = m->value;
		else if (strcasecmp(m->key, "title") == 0)
			title = m->value;

	if (title == NULL)
		title = "Untitled article";

	hbuf_printf(ob, "\\title{%s}\n", title);

	if (author != NULL) {
		hbuf_printf(ob, "\\author{%s", author);
		if (affil != NULL)
			hbuf_printf(ob, " // %s", affil);
		HBUF_PUTSL(ob, "}\n");
	}

	if (date != NULL)
		hbuf_printf(ob, "\\date{%s}\n", date);

	HBUF_PUTSL(ob, "\\maketitle\n");

	hbuf_putb(ob, content);
}

static void
rndr_meta(hbuf *ob, const hbuf *content,
	struct lowdown_metaq *mq, const struct lowdown_node *n)
{
	struct lowdown_meta	*m;

	if (mq == NULL)
		return;

	m = xcalloc(1, sizeof(struct lowdown_meta));
	TAILQ_INSERT_TAIL(mq, m, entries);
	m->key = xstrndup(n->rndr_meta.key.data,
		n->rndr_meta.key.size);
	m->value = xstrndup(content->data, content->size);
}

void
lowdown_latex_rndr(hbuf *ob, struct lowdown_metaq *metaq, 
	void *arg, const struct lowdown_node *root)
{
	hbuf				*tmp;
	struct latex			*st = arg;
	const struct lowdown_node	*n;
	const char			*tex;
	unsigned char			 texflags;

	tmp = hbuf_new(64);

	TAILQ_FOREACH(n, &root->children, entries)
		lowdown_latex_rndr(tmp, metaq, st, n);

	/*
	 * These elements can be put in either a block or an inline
	 * context, so we're safe to just use them and forget.
	 */

	if (root->chng == LOWDOWN_CHNG_INSERT)
		HBUF_PUTSL(ob, "{\\color{blue} ");
	if (root->chng == LOWDOWN_CHNG_DELETE)
		HBUF_PUTSL(ob, "{\\color{red} ");

	switch (root->type) {
	case LOWDOWN_BLOCKCODE:
		rndr_blockcode(ob, 
			&root->rndr_blockcode.text, 
			&root->rndr_blockcode.lang);
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
	case LOWDOWN_DOC_HEADER:
		rndr_doc_header(ob, tmp, metaq, st);
		break;
	case LOWDOWN_META:
		rndr_meta(ob, tmp, metaq, root);
		break;
	case LOWDOWN_DOC_FOOTER:
		rndr_doc_footer(ob, st);
		break;
	case LOWDOWN_HEADER:
		rndr_header(ob, tmp, root->rndr_header.level);
		break;
	case LOWDOWN_HRULE:
		rndr_hrule(ob);
		break;
	case LOWDOWN_LIST:
		rndr_list(ob, tmp, &root->rndr_list);
		break;
	case LOWDOWN_LISTITEM:
		rndr_listitem(ob, tmp, root);
		break;
	case LOWDOWN_PARAGRAPH:
		rndr_paragraph(ob, tmp);
		break;
	case LOWDOWN_TABLE_BLOCK:
		rndr_table(ob, tmp);
		break;
	case LOWDOWN_TABLE_HEADER:
		rndr_table_header(ob, tmp, 
			root->rndr_table_header.flags,
			root->rndr_table_header.columns);
		break;
	case LOWDOWN_TABLE_CELL:
		rndr_tablecell(ob, tmp, 
			root->rndr_table_cell.flags, 
			root->rndr_table_cell.col,
			root->rndr_table_cell.columns);
		break;
	case LOWDOWN_FOOTNOTE_DEF:
		rndr_footnote_def(ob, tmp, 
			root->rndr_footnote_def.num);
		break;
	case LOWDOWN_BLOCKHTML:
		rndr_raw_block(ob, &root->rndr_blockhtml.text, st);
		break;
	case LOWDOWN_LINK_AUTO:
		rndr_autolink(ob, 
			&root->rndr_autolink.link,
			root->rndr_autolink.type);
		break;
	case LOWDOWN_CODESPAN:
		rndr_codespan(ob, &root->rndr_codespan.text);
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
			&root->rndr_image.alt);
		break;
	case LOWDOWN_LINEBREAK:
		rndr_linebreak(ob);
		break;
	case LOWDOWN_LINK:
		rndr_link(ob, tmp, &root->rndr_link.link);
		break;
	case LOWDOWN_TRIPLE_EMPHASIS:
		rndr_triple_emphasis(ob, tmp);
		break;
	case LOWDOWN_SUPERSCRIPT:
		rndr_superscript(ob, tmp);
		break;
	case LOWDOWN_FOOTNOTE_REF:
		rndr_footnote_ref(ob, 
			root->rndr_footnote_ref.num);
		break;
	case LOWDOWN_MATH_BLOCK:
		rndr_math(ob, &root->rndr_math.text,
			root->rndr_math.blockmode);
		break;
	case LOWDOWN_RAW_HTML:
		rndr_raw_html(ob, &root->rndr_raw_html.text, st);
		break;
	case LOWDOWN_NORMAL_TEXT:
		rndr_normal_text(ob, &root->rndr_normal_text.text);
		break;
	case LOWDOWN_ENTITY:
		tex = entity_find_tex
			(&root->rndr_entity.text, &texflags);
		if (tex == NULL)
			rndr_escape(ob, &root->rndr_entity.text);
		else if (texflags & TEX_ENT_ASCII)
			hbuf_puts(ob, tex);
		else if (texflags & TEX_ENT_MATH)
			hbuf_printf(ob, "$\\mathrm{\\%s}$", tex);
		else
			hbuf_printf(ob, "\\%s", tex);
		break;
	default:
		hbuf_put(ob, tmp->data, tmp->size);
		break;
	}

	if (root->chng == LOWDOWN_CHNG_INSERT ||
	    root->chng == LOWDOWN_CHNG_DELETE)
		HBUF_PUTSL(ob, "}");

	hbuf_free(tmp);
}

void *
lowdown_latex_new(const struct lowdown_opts *opts)
{
	struct latex	*p;

	p = xcalloc(1, sizeof(struct latex));
	p->oflags = opts == NULL ? 0 : opts->oflags;
	return p;
}

void
lowdown_latex_free(void *arg)
{

	free(arg);
}
