/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"
#include "nroff.h"

static ssize_t
nroff_manpage_synopsis_prog_fl(struct nroff *, struct bnodeq *, size_t,
    const struct lowdown_buf *, char **, int);

static ssize_t
nroff_manpage_synopsis_prog_ar(struct nroff *, struct bnodeq *, size_t,
    const struct lowdown_buf *, char **);

/*
 * Parse a sequence [xxx -yyy zzz].  Returns <0 on failure (memory), 0
 * if parsing fails, and the position otherwise.  Set "out" to be the
 * allocated content or NULL if there's no content.
 */
static ssize_t
nroff_manpage_synopsis_prog_op(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf, char **out)
{
	ssize_t		 ssz;
	char		*cp, *ncp, *nncp;
	int		 rc;

	*out = NULL;

	/* Skip past open bracket and opening space... */

	for (++pos; pos < buf->size; pos++)
		if (!isspace((unsigned char)buf->data[pos]))
			break;

	/* Continue parsing expressions til the closing bracket. */

	while (pos < buf->size && buf->data[pos] != ']') {
		if (hbuf_strncasecmpat(buf, "\\(en", pos) ||
		    buf->data[pos] == '-') {
			ssz = nroff_manpage_synopsis_prog_fl
			    (st, nq, pos, buf, &cp, 0);
			if (ssz <= 0)
				return ssz;
			rc = st->type == LOWDOWN_MDOC ?
			    asprintf(&ncp, "Fl %s",
				cp == NULL ? "" : cp) :
			    asprintf(&ncp, "\\fB%s\\fR",
				cp == NULL ? "" : cp);
		} else if (buf->data[pos] == '[') {
			ssz = nroff_manpage_synopsis_prog_op
			    (st, nq, pos, buf, &cp);
			if (ssz <= 0)
				return ssz;
			rc = st->type == LOWDOWN_MDOC ?
			    asprintf(&ncp, "Oo %s Oc",
				cp == NULL ? "" : cp) :
			    asprintf(&ncp, "[%s]", 
				cp == NULL ? "" : cp);
		} else {
			ssz = nroff_manpage_synopsis_prog_ar
			    (st, nq, pos, buf, &cp);
			if (ssz <= 0)
				return ssz;
			rc = st->type == LOWDOWN_MDOC ?
			    asprintf(&ncp, "Ar %s",
				cp == NULL ? "" : cp) :
			    asprintf(&ncp, "\\fI%s\\fR",
				cp == NULL ? "" : cp);
		}

		free(cp);
		if (rc == -1) {
			free(*out);
			return -1;
		}

		/* Either set "out" or concatenate to it. */

		if (*out != NULL) {
			rc = asprintf(&nncp, "%s %s", *out, ncp);
			free(*out);
			free(ncp);
			if (rc == -1)
				return -1;
			*out = nncp;
		} else
			*out = ncp;

		/* Skip to the next non-space. */

		pos = ssz;
		while (pos < buf->size &&
		    isspace((unsigned char)buf->data[pos]))
			pos++;
	}

	/* Either have a matching bracket or bad data. */

	if (pos < buf->size && buf->data[pos] == ']')
		return ++pos;

	free(*out);
	*out = NULL;
	return 0;
}

/*
 * Parse an opaque argument.  There are some special cases like
 * "file..." and such, and also the vertical bar.  Returns <0 on failure
 * (memory), 0 if parsing fails, and the position otherwise.  Set "out"
 * to be the allocated content or NULL if there's no content.
 */
static ssize_t
nroff_manpage_synopsis_prog_ar(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf, char **out)
{
	size_t	 	 i, start;
	char		*cp = NULL;

	*out = NULL;

	/* Skip past opening space... */

	for ( ; pos < buf->size; pos++)
		if (!isspace((unsigned char)buf->data[pos]))
			break;

	/*
	 * Intercept special cases: `file...` and equivalents go
	 * directly back with no content for mdoc(7).  FIXME: this
	 * catches the fancy ellipsis for the case of `file...`, but not
	 * for the general case of e.g. `arg...`.
	 */

	if (hbuf_strncasecmpat(buf, "file \\[u2026]", pos)) {
		pos += 13;
		if (st->type == LOWDOWN_MAN &&
		    (cp = strdup("file ...")) == NULL)
			return -1;
	} else if (hbuf_strncasecmpat(buf, "file ...", pos)) {
		pos += 8;
		if (st->type == LOWDOWN_MAN &&
		    (cp = strdup("file ...")) == NULL)
			return -1;
	} else if (hbuf_strncasecmpat(buf, "file\\[u2026]", pos)) {
		pos += 12;
		if (st->type == LOWDOWN_MAN &&
		    (cp = strdup("file...")) == NULL)
			return -1;
	} else if (hbuf_strncasecmpat(buf, "file...", pos)) {
		pos += 7;
		if (st->type == LOWDOWN_MAN &&
		    (cp = strdup("file...")) == NULL)
			return -1;
	} else if (hbuf_strncasecmpat(buf, "\\[u2026]", pos)) {
		pos += 8;
		if ((cp = strdup("...")) == NULL)
			return -1;
	} else {
		for (start = pos; pos < buf->size; pos++) {
			if ('\\' == buf->data[pos] &&
			    pos + 1 < buf->size &&
			    buf->data[pos + 1] == '[') {
				for (++pos; pos < buf->size; pos++)
					if (buf->data[pos] == ']')
						break;
				if (pos == buf->size)
					break;
				continue;
			}
			if (isspace((unsigned char)buf->data[pos]) ||
			    buf->data[pos] == '[' ||
			    buf->data[pos] == ']' ||
			    buf->data[pos] == '|')
				break;
		}

		/*
		 * Scan ahead: is there an "OR" bar?  This is a common
		 * idiom like [foo | bar].  Consume up to and including
		 * the bar.
		 */

		if (pos < buf->size &&
		    (isspace((unsigned char)buf->data[pos]) ||
		     buf->data[pos] == '|')) {
			for (i = pos; i < buf->size; i++)
				if (!isspace((unsigned char)buf->data[i]))
					break;
			if (i < buf->size && buf->data[i] == '|')
				pos = ++i;
		}
		if ((cp = hbuf_stringn(buf, start, pos)) == NULL)
			return -1;
	}

	*out = cp;
	return pos;
}

/*
 * Parse a flag.  Flags are longform if they start with \(en or --
 * instead of -.  There are some special cases like the vertical bar.
 * Returns <0 on failure (memory), 0 if parsing fails, and the position
 * otherwise.
 */
static ssize_t
nroff_manpage_synopsis_prog_fl(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf, char **out, int first)
{
	size_t	 start, i;
	ssize_t	 ssz;
	char	*cp = NULL, *ncp;
	int	 ns = 0, rc, longform = 0;

	*out = NULL;

	/* Whether starting with "-" or "--". */

	if (hbuf_strncasecmpat(buf, "\\(en", pos)) {
		longform = 1;
		pos += 4;
	} else if (hbuf_strncasecmpat(buf, "--", pos)) {
		longform = 1;
		pos += 2;
	} else
		pos += 1;

	/*
	 * If this is man(7), print all the "-".  In mdoc(7), the
	 * surrounding `Fl` will output one. 
	 */

	if (st->type == LOWDOWN_MAN)
		longform++;

	/* Read until space or Oo/Oc close or open. */

	for (start = pos; pos < buf->size; pos++)
		if (isspace((unsigned char)buf->data[pos]) ||
		    buf->data[pos] == '=' ||
		    buf->data[pos] == '|' ||
		    buf->data[pos] == '[' ||
		    buf->data[pos] == ']')
			break;

	/*
	 * Scan ahead: is there an "OR" bar?  This is a common idiom
	 * like [-v | --verbose].  Consume up to and including the bar.
	 */

	if (pos < buf->size &&
	    (isspace((unsigned char)buf->data[pos]) ||
	     buf->data[pos] == '|')) {
		for (i = pos; i < buf->size; i++)
			if (!isspace((unsigned char)buf->data[i]))
				break;
		if (i < buf->size && buf->data[i] == '|')
			pos = ++i;
	}

	if (longform == 2 &&
	    asprintf(&cp, "\\-\\-%s", hbuf_stringn(buf, start, pos)) == -1)
		cp = NULL;
	else if (longform == 1 &&
	    asprintf(&cp, "\\-%s", hbuf_stringn(buf, start, pos)) == -1)
		cp = NULL;
	else if (longform == 0)
		cp = hbuf_stringn(buf, start, pos);

	if (cp == NULL)
		return -1;

	*out = cp;

	/*
	 * Scan ahead: is there something after that's part of the flag?
	 * For example, `-f flag` or, in the no-space case, `-f=bar`?
	 * Or even worse, `-f[=foo]`?
	 */

	ns = pos < buf->size && !isspace((unsigned char)buf->data[pos]);
	while (pos < buf->size && isspace((unsigned char)buf->data[pos]))
		pos++;

	/*
	 * If invoked at the beginning of subexpressions, don't continue
	 * trying to consume arguments.  This is a common idiom where a
	 * command is broken into multiple expressions with a different
	 * leading flag (e.g., cpio).  But only do so if there's space
	 * between the flag and argument.
	 */

	if (first && !ns)
		return pos;

	/* Continue with embedded options... */

	if (pos < buf->size && buf->data[pos] == '[') {
		ssz = nroff_manpage_synopsis_prog_op(st, nq, pos,
		    buf, &ncp);
		if (ssz <= 0)
			return ssz;
		rc = st->type == LOWDOWN_MDOC ?
		    asprintf(&cp, "%s%sOo %s Oc", *out,
			ns ?  " Ns " : " ", ncp == NULL ? "" : ncp) :
		    asprintf(&cp, "%s%s[%s]", *out,
			ns ? "" : " ", ncp == NULL ? "" : ncp);
		free(ncp);
		free(*out);
		if (rc == -1)
			return -1;
		*out = cp;
		return ssz;
	}

	/* Now just a trailing argument. */

	if (pos < buf->size && buf->data[pos] != '[' &&
	    buf->data[pos] != ']' && buf->data[pos] != '-' &&
	    !hbuf_strncasecmpat(buf, "\\(en", pos)) {
		ssz = nroff_manpage_synopsis_prog_ar(st, nq, pos,
		    buf, &ncp);
		if (ssz <= 0)
			return ssz;
		rc = st->type == LOWDOWN_MDOC ?
		    asprintf(&cp, "%s%sAr %s", *out,
			ns ?  " Ns " : " ", ncp) :
		    asprintf(&cp, "%s%s\\fI%s\\fR", *out,
			ns ?  "" : " ", ncp);
		free(ncp);
		free(*out);
		if (rc == -1)
			return -1;
		*out = cp;
		return ssz;
	}

	return pos;
}

/*
 * Parse a subexpression, which is basically anything that comes after
 * the name invocation.  Returns <0 on failure (memory), 0 if parsing
 * fails, and the position otherwise.
 */
static ssize_t
nroff_manpage_synopsis_prog_subexpr(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf, int first)
{
	ssize_t		 ssz = -1;
	struct bnode	*bn;
	char		*cp = NULL;

	/* Strip away leading space. */

	for ( ; pos < buf->size; pos++)
		if (!isspace((unsigned char)buf->data[pos]))
			break;
	if (pos == buf->size)
		return pos;

	/* Check illegal characters. */

	if (buf->data[pos] == ']')
		return 0;

	/*
	 * Subexpressions can either begin with a hyphen (or a long
	 * hyphen) for Fl, a square bracket for Oo/Oc, or anything else
	 * is routed to Ar.
	 */

	if (st->type == LOWDOWN_MDOC) {
		if (hbuf_strncasecmpat(buf, "\\(en", pos) ||
		    buf->data[pos] == '-') {
			if ((bn = bqueue_block(nq, ".Fl")) == NULL)
				goto out;
			ssz = nroff_manpage_synopsis_prog_fl
			    (st, nq, pos, buf, &cp, first);
		} else if (buf->data[pos] == '[') {
			if ((bn = bqueue_block(nq, ".Op")) == NULL)
				goto out;
			ssz = nroff_manpage_synopsis_prog_op
			    (st, nq, pos, buf, &cp);
		} else {
			if ((bn = bqueue_block(nq, ".Ar")) == NULL)
				goto out;
			ssz = nroff_manpage_synopsis_prog_ar
			    (st, nq, pos, buf, &cp);
		}
		if (ssz > 0)
			bn->nargs = cp;
		cp = NULL;
	} else {
		if (hbuf_strncasecmpat(buf, "\\(en", pos) ||
		    buf->data[pos] == '-') {
			ssz = nroff_manpage_synopsis_prog_fl
			    (st, nq, pos, buf, &cp, first);
			if (ssz <= 0)
				goto out;
			st->fonts[NFONT_BOLD]++;
			if (!bqueue_font(st, nq, 0) ||
			    (cp != NULL && bqueue_span(nq, cp) == NULL))
				goto out;
			cp = NULL;
			st->fonts[NFONT_BOLD]--;
			if (!bqueue_font(st, nq, 1) ||
			    bqueue_span(nq, "\n") == NULL)
				goto out;
		} else if (buf->data[pos] == '[') {
			ssz = nroff_manpage_synopsis_prog_op
			    (st, nq, pos, buf, &cp);
			if (ssz <= 0)
				goto out;
			if (bqueue_span(nq, "[") == NULL ||
			    (cp != NULL && bqueue_span(nq, cp) == NULL))
				goto out;
			cp = NULL;
			if (bqueue_span(nq, "]\n") == NULL)
				goto out;
		} else {
			ssz = nroff_manpage_synopsis_prog_ar
			    (st, nq, pos, buf, &cp);
			if (ssz <= 0)
				goto out;
			st->fonts[NFONT_ITALIC]++;
			if (!bqueue_font(st, nq, 0) ||
			    (cp != NULL && bqueue_span(nq, cp) == NULL))
				goto out;
			cp = NULL;
			st->fonts[NFONT_ITALIC]--;
			if (!bqueue_font(st, nq, 1) ||
			    bqueue_span(nq, "\n") == NULL)
				goto out;
		}
	}
out:
	free(cp);
	return ssz;
}

/*
 * Parse a single #include statement.  Returns the position in the
 * buffer if >0, 0 if the parse failed, or -1 on failure.
 */
static ssize_t
nroff_manpage_synopsis_func_incl(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf)
{
	struct bnode	*bn;
	char		*cp;
	size_t		 start;

	pos += 9;
	while (pos < buf->size &&
	    isspace((unsigned char)buf->data[pos]))
		pos++;
	if (pos < buf->size && buf->data[pos] == '<')
		pos++;

	for (start = pos; pos < buf->size; pos++)
		if (buf->data[pos] == '>' ||
		    isspace((unsigned char)buf->data[pos]))
			break;

	if ((cp = hbuf_stringn(buf, start, pos)) == NULL)
		return -1;
	if (st->type == LOWDOWN_MDOC) {
		if ((bn = bqueue_block(nq, ".In")) == NULL) {
			free(cp);
			return -1;
		}
		bn->nargs = cp;
	} else {
		if (bqueue_block(nq, ".sp") == NULL) {
			free(cp);
			return -1;
		}
		st->fonts[NFONT_BOLD]++;
		if (!bqueue_font(st, nq, 0) ||
		    bqueue_span(nq, "#include <") == NULL ||
		    bqueue_span(nq, cp) == NULL ||
		    bqueue_span(nq, ">") == NULL) {
			free(cp);
			return -1;
		}
		st->fonts[NFONT_BOLD]--;
		if (!bqueue_font(st, nq, 1) ||
		    bqueue_span(nq, "\n") == NULL)
			return -1;
	}

	if (pos < buf->size && buf->data[pos] == '>')
		pos++;

	while (pos < buf->size &&
	    isspace((unsigned char)buf->data[pos]))
		pos++;

	return pos;
}

/*
 * Parse a single function declaration.  Returns the position in the
 * buffer if >0, 0 if the parse failed, or -1 on failure.
 */
static ssize_t
nroff_manpage_synopsis_func_decl(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf)
{
	struct bnode	*bn;
	size_t		 i, paren, arg, nest;
	char		*cp, *ncp;
	int		 first = 1;

	/* Skip over leading space. */

	while (pos < buf->size &&
	    isspace((unsigned char)buf->data[pos]))
		pos++;

	/*
	 * Get the start of the function part.  If it doesn't exist,
	 * assume that this is a variable type.
	 */

	for (paren = pos; paren < buf->size; paren++)
		if (buf->data[paren] == '(')
			break;

	if (paren == buf->size || paren == pos) {
		if ((bn = bqueue_block(nq, ".Vt")) == NULL ||
		    (bn->nargs = hbuf_stringn(buf, pos, buf->size)) == NULL)
			return -1;
		return buf->size;
	}

	/* Backtrack to the function name start. */

	for (i = paren; i > pos; i--)
		if (isspace((unsigned char)buf->data[i]))
			break;

	/*
	 * The type is what came before the function name from the start
	 * of the expression.  If doesn't need to exist.
	 */

	if (st->type == LOWDOWN_MAN)
		if (bqueue_block(nq, ".sp") == NULL)
			return -1;

	if (i > pos) {
		if ((cp = hbuf_stringn(buf, pos, i)) == NULL)
			return -1;
		if (st->type == LOWDOWN_MDOC) {
			if ((bn = bqueue_block(nq, ".Ft")) == NULL) {
				free(cp);
				return -1;
			}
			bn->nargs = cp;
		} else {
			st->fonts[NFONT_ITALIC]++;
			if (!bqueue_font(st, nq, 0) ||
			    bqueue_span(nq, cp) == NULL) {
				free(cp);
				return -1;
			}
			st->fonts[NFONT_ITALIC]--;
			if (!bqueue_font(st, nq, 1) ||
			    bqueue_span(nq, "\n") == NULL ||
			    bqueue_block(nq, ".br") == NULL ||
			    bqueue_block(nq, ".in +4") == NULL ||
			    bqueue_block(nq, ".ti -4") == NULL)
				return -1;
		}
		pos = i + 1;
	} 

	/* Output the function name start. */

    	if ((cp = hbuf_stringn(buf, pos, paren)) == NULL)
		return -1;
	if (st->type == LOWDOWN_MDOC) {
		if ((bn = bqueue_block(nq, ".Fo")) == NULL) {
			free(cp);
			return -1;
		}
		bn->nargs = cp;
	} else {
		st->fonts[NFONT_BOLD]++;
		if (!bqueue_font(st, nq, 0) ||
		    bqueue_span(nq, cp) == NULL) {
			free(cp);
			return -1;
		}
		st->fonts[NFONT_BOLD]--;
		if (!bqueue_font(st, nq, 1))
			return -1;
	}

	/* If the function is truncated, just close it out. */

	if (paren == buf->size) {
		if (st->type == LOWDOWN_MDOC) {
			if (bqueue_block(nq, ".Fc") == NULL)
				return -1;
		} else {
			if (bqueue_span(nq, "\n") == NULL)
				return -1;
		}
		return paren;
	}

	/* Read each comma-separated argument. */

	nest = 0;
	for (pos = paren + 1, arg = pos; pos < buf->size; pos++) {
		if (buf->data[pos] == ')' && nest > 0) {
			nest--;
			continue;
		}
		if (buf->data[pos] == '(')
			nest++;
		if (buf->data[pos] != ',' && buf->data[pos] != ')')
			continue;

		/*
		 * The end of the sequence is a closing parenthesis
		 * followed by optional space, then an optional
		 * semicolon.  Preserve our position, because this
		 * ending part shouldn't be in the trailing `Fa`.
		 */

		i = pos;

		if (buf->data[i] == ')') {
			assert(nest == 0);
			for (++i; i < buf->size; i++)
				if (!isspace
				    ((unsigned char)buf->data[i]))
					break;
			if (i < buf->size && buf->data[i] == ';')
				i++;
			for ( ; i < buf->size; i++)
				if (!isspace
				    ((unsigned char)buf->data[i]))
					break;
			if (i < buf->size)
				return 0;
		}
		
		/*
		 * From the last saved position, copy the text from then
		 * til the comma and strip away any whitespace.  Elide
		 * empty arguments.
		 */

	        if ((cp = hbuf_stringn_trim(buf, arg, pos)) == NULL)
			return -1;
		if (*cp != '\0' && st->type == LOWDOWN_MDOC) {
			bn = bqueue_block(nq, ".Fa");
			if (bn == NULL) {
				free(cp);
				return -1;
			}
			if (asprintf(&bn->nargs, "\"%s\"", cp) == -1) {
				free(cp);
				bn->nargs = NULL;
				return -1;
			}
		} else if (*cp != '\0' && st->type == LOWDOWN_MAN) {
			if (asprintf(&ncp, "%s\\fI%s\\fR",
			    first ? "(" : ", ", cp) == -1) {
				free(cp);
				return -1;
			} else if (bqueue_span(nq, ncp) == NULL) {
				free(cp);
				free(ncp);
				return -1;
			}
		}
		free(cp);
		first = 0;

		/*
		 * Set the position to the end of the sequence, and arg
		 * as the start of the next one.
		 */

		pos = i;
		arg = pos + 1;
	}

	if (st->type == LOWDOWN_MDOC)
		return bqueue_block(nq, ".Fc") == NULL ? -1 : pos;

	if (bqueue_span(nq, ");\n") == NULL ||
	    bqueue_block(nq, ".in -4") == NULL)
		return -1;

	return pos;
}

/*
 * Parse an expression in a function SYNOPSIS, that is, an include
 * statement or a function declaration.  Return -1 on failure (memory),
 * 0 if the parse failed (not a valid expression), or the current
 * position if >0 on success.
 */
static ssize_t
nroff_manpage_synopsis_func_expr(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf)
{
	ssize_t		 ssz;

	for ( ; pos < buf->size; pos++)
		if (!isspace((unsigned char)buf->data[pos]))
			break;

	/*
	 * Following the name and any whitespace are the subexpressions
	 * that define the arguments.  Consume all of them.
	 */

	while (pos < buf->size) {
		if (hbuf_strncasecmpat(buf, "#include ", pos))
			ssz = nroff_manpage_synopsis_func_incl(st, nq, pos, buf);
		else
			ssz = nroff_manpage_synopsis_func_decl(st, nq, pos, buf);
		if (ssz <= 0)
			return ssz;
		pos = ssz;
	}
	return pos;
}

/*
 * Parse a full synopsis expression. Returns <0 on failure (memory), 0
 * if parsing fails, and the position otherwise.
 */
static ssize_t
nroff_manpage_synopsis_prog_expr(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf)
{
	struct bnode	*bn;
	size_t		 sta;
	ssize_t		 ssz;

	/*
	 * Each synopsis expression consists of a name at the beginning
	 * of the line, possibly following whitespace.
	 */

	for ( ; pos < buf->size; pos++)
		if (!isspace((unsigned char)buf->data[pos]))
			break;
	for (sta = pos; pos < buf->size; pos++)
		if (isspace((unsigned char)buf->data[pos]))
			break;
	if (pos == buf->size)
		return pos;

	/* mdoc(7) has Nm followed by stuff; man(7) has SY. */
	/* FIXME: man(7) use IP if traditional. */

	bn = st->type == LOWDOWN_MDOC ?
	    bqueue_block(nq, ".Nm") : bqueue_block(nq, ".SY");
	if (bn == NULL ||
	    (bn->nargs = hbuf_stringn(buf, sta, pos)) == NULL)
		return -1;

	/*
	 * Following the name and any whitespace are the subexpressions
	 * that define the arguments.  Consume all of them.
	 */

	while (pos < buf->size) {
		ssz = nroff_manpage_synopsis_prog_subexpr(st, nq, pos, buf, 1);
		if (ssz <= 0)
			return ssz;
		pos = ssz;
	}

	/* FIXME: don't use if traditional. */

	if (st->type == LOWDOWN_MAN && bqueue_block(nq, ".YS") == NULL)
		return -1;

	return pos;
}

/*
 * Route into nroff_manpage_synopsis_func_expr() with parsed buffer.
 * Returns 0 on failure (memory), 1 on success.  On success, the
 * paragraph content may have been replaced.
 */
static int
nroff_manpage_synopsis_func(struct nroff *st, const struct lowdown_node *n,
    struct bnodeq *obq, struct bnodeq *nbq)
{
	int	 		 rc = -1;
	struct lowdown_buf	*buf = NULL;
	struct bnodeq		 nq;
	ssize_t			 ret;

	TAILQ_INIT(&nq);

	if ((buf = hbuf_new(32)) != NULL &&
	    bqueue_flush(st, buf, nbq, 2)) {
		ret = nroff_manpage_synopsis_func_expr(st, &nq, 0, buf);
		if (ret > 0) {
			bqueue_free(nbq);
			TAILQ_CONCAT(obq, &nq, entries);
			rc = 1;
		} else if (ret == 0)
			rc = 0;
	}

	hbuf_free(buf);
	bqueue_free(&nq);
	return rc;
}

/*
 * Route into nroff_manpage_synopsis_prog_expr() with parsed buffer.
 * Returns 0 on failure (memory), 1 on success.  On success, the
 * paragraph content may have been replaced.
 */
static int
nroff_manpage_synopsis_prog(struct nroff *st, const struct lowdown_node *n,
    struct bnodeq *obq, struct bnodeq *nbq)
{
	int	 		 rc = -1;
	struct lowdown_buf	*buf = NULL;
	struct bnodeq		 nq;
	ssize_t			 ret;

	TAILQ_INIT(&nq);

	if ((buf = hbuf_new(32)) != NULL &&
	    bqueue_flush(st, buf, nbq, 2)) {
		ret = nroff_manpage_synopsis_prog_expr(st, &nq, 0, buf);
		if (ret > 0) {
			bqueue_free(nbq);
			TAILQ_CONCAT(obq, &nq, entries);
			rc = 1;
		} else if (ret == 0)
			rc = 0;
	}

	hbuf_free(buf);
	bqueue_free(&nq);
	return rc;
}

/*
 * SEE ALSO sections in mdoc(7) conventionally contain only lists of
 * other manpages.  They can, however, also contain actual content.  Try
 * to catch the list of manpages and format them with `Xr` here.  Return
 * -1 on total failure (memory), 0 if the paragraph was not a list of
 *  manpages, and 1 if it was.
 */
static int
rndr_manpage_see_also(struct nroff *st, const struct lowdown_node *n,
    struct bnodeq *obq, struct bnodeq *nbq)
{
	int	 		 rc = 0;
	struct lowdown_buf	*buf = NULL;
	struct bnodeq		 nq;
	size_t			 pos, end, start;
	const char		*openp;
	struct bnode		*bn;

	TAILQ_INIT(&nq);

	if ((buf = hbuf_new(32)) == NULL ||
	    !bqueue_flush(st, buf, nbq, 2))
		goto out;

	for (pos = 0; pos < buf->size; pos++)
		if (!isspace((unsigned char)buf->data[pos]))
			break;

	/*
	 * Tokenise along commas or the eoln, then break apart the
	 * contents to see if it matches foo(section).  If a single
	 * token doesn't, then bail out and assume this is a regular
	 * paragraph.  Otherwise, format the link in an Xr.  Skip over
	 * empty tokens.
	 */

	for (start = pos; pos <= buf->size; ) {
		if (buf->data[pos] == ',' || pos == buf->size) {
			if (pos == start) {
				/* Skip empty tokens. */
				for (++pos; pos < buf->size; pos++)
					if (!isspace((unsigned char)
					    buf->data[pos]))
						break;
				start = pos;
				continue;
			}
			/* Trim trailing spaces. */
			for (end = pos--; pos > start; pos--)
				if (!isspace((unsigned char)
				    buf->data[pos]))
					break;
			/* Make sure there are parentheses. */
			if (buf->data[pos] != ')')
				goto out;
			openp = memchr(&buf->data[start], '(',
			    pos - start);
			if (openp == NULL)
				goto out;
			bn = st->type == LOWDOWN_MDOC ?
			    bqueue_block(obq, ".Xr") :
			    bqueue_block(obq, ".MR");
			if (bn == NULL) {
				rc = -1;
				goto out;
			}
			if (asprintf(&bn->nargs, "%.*s %.*s%s",
			    (int)(openp - &buf->data[start]),
			    &buf->data[start],
			    (int)(&buf->data[pos] - openp - 1),
			    openp + 1,
			    end < buf->size ? " ," : "") == -1) {
				bn->nargs = NULL;
				rc = -1;
				goto out;
			}
			if (end == buf->size)
				break;
			/* Trim leading spaces. */
			for (pos = end + 1; pos < buf->size; pos++)
				if (!isspace((unsigned char)
				    buf->data[pos]))
					break;
			start = pos;
		} else
			pos++;
	}
	TAILQ_CONCAT(obq, &nq, entries);
	bqueue_free(nbq);
	rc = 1;
out:
	hbuf_free(buf);
	bqueue_free(&nq);
	return rc;
}

/*
 * The NAME section consists of one or more comma-separated names
 * followed by a dash (or fancy dash) then a description.  Split the
 * names into `Nm` (or bold) statements and, if found, the description
 * into an `Nd` (or undecorated).  The regressive case of a single word
 * with no hyphens is just the `Nm` (or nothing).  This returns -1 on
 * failure (memory), 1 on success, and 0 if the section failed to parse
 * as a NAME.  (This currently doesn't actually happen.)
 */
static int
rndr_manpage_name(struct nroff *st, const struct lowdown_node *n,
    struct bnodeq *obq, struct bnodeq *nbq)
{
	struct bnode		*bn;
	struct lowdown_buf	*buf = NULL;
	int			 rc = -1, has_next;
	size_t			 pos, start;
	char			*cp = NULL;

	/* Flush everything into a single, un-decorated line. */

	if ((buf = hbuf_new(32)) == NULL ||
	    !bqueue_flush(st, buf, nbq, 2))
		goto out;

	/* In man(7), break after the NAME section. */

	if (st->type == LOWDOWN_MAN && bqueue_span(obq, "\n") == NULL)
		goto out;

	/*
	 * Skip white-space then output an name for each token leading
	 * up to a comma, a hyphen (or pretty hyphen), or the end of
	 * line.
	 */

	for (pos = 0; pos < buf->size; pos++)
		if (!isspace((unsigned char)buf->data[pos]))
			break;

	for (start = pos; pos <= buf->size; ) {
		if (pos != buf->size &&
		    buf->data[pos] != ',' &&
		    buf->data[pos] != '-' &&
		    !hbuf_strncasecmpat(buf, "\\(en", pos) &&
		    !hbuf_strncasecmpat(buf, "\\(em", pos)) {
			pos++;
			continue;
		}

		has_next = pos < buf->size && buf->data[pos] == ',';

		cp = hbuf_stringn_trim(buf, start, pos);
		if (cp == NULL)
			goto out;

		/* For mdoc(7), use `Nm`; for man(7), bold. */

		if (*cp != '\0' && st->type == LOWDOWN_MDOC) {
			bn = bqueue_block(obq, ".Nm");
			if (bn == NULL)
				goto out;
			if (asprintf(&bn->nargs, "%s%s",
			    cp, has_next ? " ," : "") == -1) {
				bn->nargs = NULL;
				goto out;
			}
		} else if (*cp != '\0' && st->type == LOWDOWN_MAN) {
			st->fonts[NFONT_BOLD]++;
			if (!bqueue_font(st, obq, 0) ||
			    bqueue_span(obq, cp) == NULL) {
				st->fonts[NFONT_BOLD]--;
				goto out;
			}
			st->fonts[NFONT_BOLD]--;
			if (!bqueue_font(st, obq, 1))
				goto out;
			free(cp);
			if (has_next &&
			    bqueue_span(obq, ",\n") == NULL)
				goto out;
		} else
			free(cp);

		/* Stop if no more name tokens. */

		cp = NULL;
		if (pos == buf->size || buf->data[pos] != ',')
			break;

		/* Read after whitespace, then try again... */
		for (++pos; pos < buf->size; pos++)
			if (!isspace((unsigned char)buf->data[pos]))
				break;

		start = pos;
	}

	/*
	 * Read past delimiter (which is either a sequence of hyphens or
	 * a fancy en or em, length 4) between name and description.
	 */

	if (pos < buf->size && buf->data[pos] == '-') {
		while (pos < buf->size && buf->data[pos] == '-')
			pos++;
	} else if (pos < buf->size)
		pos += 4;

	/* In man(7), add the delimiter. */

	if (st->type == LOWDOWN_MAN &&
	    bqueue_span(obq, " \\(en ") == NULL)
		goto out;

	/* Conditionally output `Nd` or just text. */

	if (pos < buf->size) {
		cp = hbuf_stringn_trim(buf, pos, buf->size);
		if (cp == NULL)
			goto out;
		if (*cp != '\0' && st->type == LOWDOWN_MDOC) {
			if ((bn = bqueue_block(obq, ".Nd")) == NULL)
				goto out;
			bn->nargs = cp;
		} else if (*cp != '\0' && st->type == LOWDOWN_MAN) {
			if (bqueue_span(obq, cp) == NULL)
				goto out;
			free(cp);
		} else
			free(cp);
		cp = NULL;
	}

	bqueue_free(nbq);
	rc = 1;
out:
	hbuf_free(buf);
	free(cp);
	return rc;
}

/*
 * Manpage paragraphs may have special handling depending on their prior
 * section, the manual section, phase of the moon, etc.  If these
 * conditions are met, "nbq" may be drained and new content generated
 * and appended to "obq".  This returns 0 if the paragraph content was
 * replaced and no paragraph macro should be output, <0 on fatal error,
 * and >0 if the paragraph should be processed as usual.
 */
int
nroff_manpage_paragraph(struct nroff *st, const struct lowdown_node *n,
    struct bnodeq *obq, struct bnodeq *nbq)
{
	const struct lowdown_node	*prev, *next;
	int				 rc;

	/* NAME for manpages is always a single paragraph. */

	if ((prev = TAILQ_PREV(n, lowdown_nodeq, entries)) != NULL &&
	    prev->type == LOWDOWN_HEADER &&
	    (next = TAILQ_NEXT(n, entries)) != NULL &&
	    next->type == LOWDOWN_HEADER &&
	    nroff_in_section(st, "NAME")) {
		if ((rc = rndr_manpage_name(st, n, obq, nbq)) < 0)
			return rc;
		return rc == 0 ? 1 : 0;
	}

	/* SEE ALSO manpage listings (can have multiple paragraphs). */

	if (nroff_in_section(st, "SEE ALSO")) {
		if ((rc = rndr_manpage_see_also(st, n, obq, nbq)) < 0)
			return rc;
		return rc == 0 ? 1 : 0;
	}

	/*
	 * Paragraphs within the SYNOPSIS are broken down in very
	 * specific ways according to the manual section.
	 */

	if (st->headers_sec != NULL &&
	    (st->headers_sec[0] == '1' ||
	     st->headers_sec[0] == '6' ||
	     st->headers_sec[0] == '8') &&
	    nroff_in_section(st, "SYNOPSIS")) {
		if ((rc = nroff_manpage_synopsis_prog(st, n, obq, nbq)) < 0)
			return rc;
		return rc == 0 ? 1 : 0;
	}

	if (st->headers_sec != NULL &&
	    (st->headers_sec[0] == '2' ||
	     st->headers_sec[0] == '3' ||
	     st->headers_sec[0] == '9') &&
	    nroff_in_section(st, "SYNOPSIS")) {
		if ((rc = nroff_manpage_synopsis_func(st, n, obq, nbq)) < 0)
			return rc;
		return rc == 0 ? 1 : 0;
	}

	return 1;
}

/*
 * Parse a full synopsis expression. Returns <0 on failure (memory), 0
 * if parsing fails, and the position otherwise.
 */
static ssize_t
nroff_manpage_codespan_prog_expr(struct nroff *st, struct bnodeq *nq,
    size_t pos, const struct lowdown_buf *buf)
{
	ssize_t		 ssz;

	for ( ; pos < buf->size; pos++)
		if (!isspace((unsigned char)buf->data[pos]))
			break;

	if (pos == buf->size)
		return pos;

	while (pos < buf->size) {
		ssz = nroff_manpage_synopsis_prog_subexpr(st, nq, pos, buf, 0);
		if (ssz <= 0)
			return ssz;
		pos = ssz;
	}

	return pos;
}

/*
 * Codespans trigger the "manpage" interpreter.  Assume that the span
 * contains a flag, argument, or option.  If these conditions are met,
 * generate new content and append to "obq".  This returns 0 if content
 * was added and the span should not be processed, <0 on fatal error,
 * and >0 if no content was processed and the span should be processed
 * by the caller.
 */
int
nroff_manpage_codespan(struct nroff *st, struct bnodeq *obq,
    const struct lowdown_buf *buf)
{
	int	 		 rc = 0;
	struct bnodeq		 nq;
	ssize_t			 ret;

	if (st->headers_sec != NULL &&
	    (st->headers_sec[0] == '1' ||
	     st->headers_sec[0] == '6' ||
	     st->headers_sec[0] == '8') &&
	    !nroff_in_section(st, "SYNOPSIS")) {
		TAILQ_INIT(&nq);
		ret = nroff_manpage_codespan_prog_expr(st, &nq, 0, buf);
		if (ret > 0)
			TAILQ_CONCAT(obq, &nq, entries);
		bqueue_free(&nq);
		rc = ret > 0 ? 0 : ret == 0 ? 1 : -1;
	}
	return rc;
}
