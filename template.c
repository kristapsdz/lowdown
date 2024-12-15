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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

TAILQ_HEAD(opq, op);
TAILQ_HEAD(op_resq, op_res);
TAILQ_HEAD(op_argq, op_arg);

enum op_type {
	OP_FOR,
	OP_IFDEF,
	OP_ELSE,
	OP_STR,
	OP_EXPR,
	OP_ROOT,
};

static	const char *const op_types[OP_ROOT + 1] = {
	"for",
	"ifdef",
	"else",
	"str",
	"expr",
	"root",
};

/*
 * Carry the result of an evaluation as an allocated string.
 */
struct op_res {
	char		*res;
	TAILQ_ENTRY(op_res) entries;
};

/*
 * An argument to a transformation.
 */
struct op_arg {
	const char	*arg;
	size_t		 argsz;
	TAILQ_ENTRY(op_arg) entries;
};

/*
 * Operation for an opaque string.
 */
struct op_str {
	const char	*str; /* content */
	size_t		 sz; /* length of content */
};

/*
 * Operation for evaluating an expression.
 */
struct op_expr {
	const char	*expr; /* expression */
	size_t		 sz; /* length of expression */
};

/*
 * Operation for an ifdef conditional.
 */
struct op_ifdef {
	const char	*expr; /* expression */
	size_t		 sz; /* length of expression */
	const struct op	*chain; /* "else" or NULL */
};

/*
 * Operation for a for loop.
 */
struct op_for {
	const char	*expr;
	size_t		 sz;
};

/*
 * Operations are laid out in a tree under an OP_ROOT.  Each block
 * (OP_IFDEF, OP_ELSE, OP_FOR) introduces a sub-tree.
 */
struct op {
	union {
		struct op_for	 op_for;
		struct op_ifdef	 op_ifdef;
		struct op_str	 op_str;
		struct op_expr	 op_expr;
	};
	enum op_type		 op_type; /* type */
	struct opq		 children; /* if block, the children */
	struct op		*parent; /* parent (NULL for OP_ROOT) */
	TAILQ_ENTRY(op)		 _siblings; /* siblings in block */
	TAILQ_ENTRY(op)		 _all; /* queue of all operations */
};

struct op_out {
	int				 debug;
	size_t				 depth;
	struct lowdown_buf		*ob;
	const struct lowdown_buf	*content;
	const struct lowdown_metaq	*mq;
};

/* Forward declaration. */

static int op_exec(struct op_out *, const struct op *, const char *);
static struct op_resq *op_eval(struct op_out *, const char *, size_t,
    const char *, const struct op_resq *);
static int op_debug(struct op_out *, const char *, ...)
    __attribute__((format (printf, 2, 3)));

/*
 * Allocate the generic members of "struct op".  The caller should be
 * initialising the type-specific members, e.g., "op_ifdef" for
 * OP_IFDEF.  Returns the pointer or NULL on allocation failure.
 */
static struct op *
op_alloc(struct opq *q, enum op_type type, struct op *cop)
{
	struct op	*op;

	if ((op = calloc(1, sizeof(struct op))) == NULL)
		return NULL;
	TAILQ_INIT(&op->children);
	op->op_type = type;
	op->parent = cop;
	TAILQ_INSERT_TAIL(q, op, _all);
	if (cop != NULL)
		TAILQ_INSERT_TAIL(&cop->children, op, _siblings);
	return op;
}

/*
 * Queue an expression-printing operation.  Returns non-zero on success,
 * zero on failure (allocation failure).
 */
static int
op_queue_expr(struct opq *q, struct op *cop, const char *expr,
    size_t sz)
{
	struct op	*op;

	if ((op = op_alloc(q, OP_EXPR, cop)) == NULL)
		return 0;
	op->op_expr.expr = expr;
	op->op_expr.sz = sz;
	return 1;
}

/*
 * Queue an opaque string-printing operation.  Returns non-zero on
 * success, zero on failure (allocation failure).
 */
static int
op_queue_str(struct opq *q, struct op *cop, const char *str, size_t sz)
{
	struct op	*op;

	if ((op = op_alloc(q, OP_STR, cop)) == NULL)
		return 0;
	op->op_str.str = str;
	op->op_str.sz = sz;
	return 1;
}

/*
 * Queue the start of a conditional block.  Returns non-zero on success,
 * zero on failure (allocation failure).
 */
static int
op_queue_ifdef(struct opq *q, struct op **cop, const char *expr,
    size_t sz)
{
	struct op	*op;

	if ((op = op_alloc(q, OP_IFDEF, *cop)) == NULL)
		return 0;
	op->op_ifdef.expr = expr;
	op->op_ifdef.sz = sz;
	*cop = op;
	return 1;
}

/*
 * Queue the start of a for-loop block.  Returns non-zero on success,
 * zero on failure (allocation failure).
 */
static int
op_queue_for(struct opq *q, struct op **cop, const char *expr,
    size_t sz)
{
	struct op	*op;

	if ((op = op_alloc(q, OP_FOR, *cop)) == NULL)
		return 0;
	op->op_for.expr = expr;
	op->op_for.sz = sz;
	*cop = op;
	return 1;
}

/*
 * Queue the start of a conditional "else" block.  If not currently in a
 * conditional block, the "else" block will never execute.  Returns
 * non-zero on success, zero on failure (allocation failure).
 */
static int
op_queue_else(struct opq *q, struct op **cop)
{
	struct op	*op, *ifop = NULL;

	if ((*cop)->op_type == OP_IFDEF) {
		ifop = *cop;
		*cop = (*cop)->parent;
	}

	if ((op = op_alloc(q, OP_ELSE, *cop)) == NULL)
		return 0;

	if (ifop != NULL) {
		assert(ifop->op_ifdef.chain == NULL);
		ifop->op_ifdef.chain = op;
	}

	*cop = op;
	return 1;
}

/*
 * Respond to ending a for-loop.  If not currently in a loop block, does
 * nothing.  Returns non-zero always.
 */
static int
op_queue_endfor(struct op **cop)
{
	if ((*cop)->op_type == OP_FOR)
		*cop = (*cop)->parent;
	return 1;
}

/*
 * Respond to ending a conditional.  If not currently in a conditional
 * block ("ifdef" or "else"), does nothing.  Returns non-zero always.
 */
static int
op_queue_endif(struct op **cop)
{
	if ((*cop)->op_type == OP_IFDEF || (*cop)->op_type == OP_ELSE)
		*cop = (*cop)->parent;
	return 1;
}

/*
 * Evaluate a statement as an expresion, conditional, etc.  The current
 * operation may be changed if entering or exiting a block.  Returns
 * zero on failure (memory allocation), non-zero on success.
 */
static int
op_queue(struct opq *q, struct op **cop, const char *str, size_t sz)
{
	/* TODO: space before "(". */

	if (sz > 6 && strncasecmp(str, "ifdef(", 6) == 0 &&
	    str[sz - 1] == ')')
		return op_queue_ifdef(q, cop, str + 6, sz - 7);
	if (sz > 4 && strncasecmp(str, "for(", 4) == 0 &&
	    str[sz - 1] == ')')
		return op_queue_for(q, cop, str + 4, sz - 5);
	if (sz == 4 && strncasecmp(str, "else", 4) == 0)
		return op_queue_else(q, cop);
	if (sz == 5 && strncasecmp(str, "endif", 5) == 0)
		return op_queue_endif(cop);
	if (sz == 6 && strncasecmp(str, "endfor", 6) == 0)
		return op_queue_endfor(cop);

	return op_queue_expr(q, *cop, str, sz);
}

/*
 * If "debug" has been set, print out a message after a number of spaces
 * proportionate to the debug depth.
 * Return zero on failure (memory allocation), non-zero otherwise.
 */
static int
op_debug(struct op_out *out, const char *fmt, ...)
{
	size_t	 i;
	char	 buf[256];
	int	 rc;
	va_list	 ap;

	if (!out->debug)
		return 1;

	for (i = 0; i < out->depth; i++)
		if (!HBUF_PUTSL(out->ob, "  "))
			return 0;

	va_start(ap, fmt);
	rc = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (rc == -1)
		return 0;
	return hbuf_puts(out->ob, buf) && HBUF_PUTSL(out->ob, "\n");
}

static void
op_resq_free(struct op_resq *q)
{
	struct op_res	*res;

	if (q == NULL)
		return;

	while ((res = TAILQ_FIRST(q)) != NULL) {
		TAILQ_REMOVE(q, res, entries);
		free(res->res);
		free(res);
	}
	free(q);
}

static struct op_resq *
op_resq_clone(const struct op_resq *q, int trim)
{
	struct op_resq		*nq;
	struct op_res		*nres;
	const struct op_res	*res;
	const char		*start, *cp;
	size_t			 sz;

	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		return NULL;

	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, q, entries) {
		sz = strlen(res->res);
		start = res->res;
		if (trim) {
			for (cp = start; *cp != '\0'; cp++, sz--)
				if (!isspace((unsigned char)*cp))
					break;
			if (*cp == '\0')
				continue;
			while (sz > 0 && isspace((unsigned char)cp[sz - 1]))
				sz--;
			if (sz == 0)
				continue;
		}

		assert(sz > 0);
		nres = calloc(1, sizeof(struct op_res));
		if (nres == NULL) {
			op_resq_free(nq);
			return NULL;
		}
		TAILQ_INSERT_TAIL(nq, nres, entries);
		nres->res = strndup(start, sz);
		if (nres->res == NULL) {
			op_resq_free(nq);
			return NULL;
		}
	}

	return nq;
}

static void
op_argq_free(struct op_argq *q)
{
	struct op_arg	*arg;

	while ((arg = TAILQ_FIRST(q)) != NULL) {
		TAILQ_REMOVE(q, arg, entries);
		free(arg);
	}
}

static int
op_argq_new(struct op_argq *q, const char *args, size_t argsz)
{
	size_t	 	 i, start, substack = 0;
	int		 inquot = 0;
	struct op_arg	*arg;

	for (start = i = 0; i < argsz; i++) {
		if (args[i] == '"') {
			inquot = !inquot;
			continue;
		} else if (args[i] == '(') {
			substack++;
			continue;
		} else if (args[i] == ')') {
			substack--;
			continue;
		} else if (args[i] != ',' || substack > 0 || inquot)
			continue;

		if ((arg = calloc(1, sizeof(struct op_arg))) == NULL)
			return 0;
		TAILQ_INSERT_TAIL(q, arg, entries);
		arg->arg = &args[start];
		arg->argsz = i - start;
		start = i + 1;
	}

	if (i >= start) {
		if ((arg = calloc(1, sizeof(struct op_arg))) == NULL)
			return 0;
		TAILQ_INSERT_TAIL(q, arg, entries);
		arg->arg = &args[start];
		arg->argsz = i - start;
		start = i + 1;
	}

	return 1;
}

/*
 * Split each input string along white-space boundaries.  This trims
 * white-space around all strings during the split.  The result is a
 * list of non-empty, non-only-whitespace strings.  Returns NULL on
 * allocation failure.
 */
static struct op_resq *
op_eval_function_split(struct op_out *out, const struct op_resq *input)
{
	struct op_resq	*nq = NULL;
	struct op_res	*nres, *nnres;
	char		*cp, *ncp;

	if ((nq = op_resq_clone(input, 1)) == NULL)
		goto err;

	TAILQ_FOREACH(nres, nq, entries) {
		for (cp = nres->res; *cp != '\0'; cp++)
			if (!isspace((unsigned char)*cp))
				break;

		/* If this is only whitespace, ignore it. */

		if (*cp == '\0')
			continue;

		/* Scan ahead until multiple whitespaces. */

		for ( ; *cp != '\0'; cp++)
			if (isspace((unsigned char)cp[0]) &&
			    isspace((unsigned char)cp[1]))
				break;

		/* Scan to next non-whitespace. */

		for (ncp = cp; *ncp != '\0'; ncp++)
			if (!isspace((unsigned char)*ncp))
				break;

		/* If no subsequent word, don't split. */

		if (*ncp == '\0')
			continue;

		/* Split at white-space. */

		*cp = '\0';
		if ((nnres = calloc(1, sizeof(struct op_res))) == NULL)
			goto err;
		TAILQ_INSERT_AFTER(nq, nres, nnres, entries);
		if ((nnres->res = strdup(ncp)) == NULL)
			goto err;
	}

	return nq;
err:
	op_resq_free(nq);
	return NULL;
}

/*
 * Escape all characters in all list items for HTML URL attributes.
 * Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_escape_htmlurl(struct op_out *out,
    const struct op_resq *input)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf = NULL;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!lowdown_html_esc_href(buf, res->res, strlen(res->res)))
			goto err;
		if ((nres = calloc(1, sizeof(struct op_res))) == NULL)
			goto err;
		TAILQ_INSERT_TAIL(nq, nres, entries);
		nres->res = strndup(buf->data, buf->size);
		if (nres->res == NULL)
			goto err;
	}
	hbuf_free(buf);
	return nq;
err:
	hbuf_free(buf);
	op_resq_free(nq);
	return NULL;
}

/*
 * Escape all characters in all list items for HTML attributes.
 * Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_escape_htmlattr(struct op_out *out,
    const struct op_resq *input)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf = NULL;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!lowdown_html_esc_attr(buf, res->res, strlen(res->res)))
			goto err;
		if ((nres = calloc(1, sizeof(struct op_res))) == NULL)
			goto err;
		TAILQ_INSERT_TAIL(nq, nres, entries);
		nres->res = strndup(buf->data, buf->size);
		if (nres->res == NULL)
			goto err;
	}
	hbuf_free(buf);
	return nq;
err:
	hbuf_free(buf);
	op_resq_free(nq);
	return NULL;
}

/*
 * HTML-escape (for general content) all characters in all list items.
 * Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_escape_html(struct op_out *out,
    const struct op_resq *input)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf = NULL;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!lowdown_html_esc(buf, res->res, strlen(res->res),
		    1, 0, 0))
			goto err;
		if ((nres = calloc(1, sizeof(struct op_res))) == NULL)
			goto err;
		TAILQ_INSERT_TAIL(nq, nres, entries);
		nres->res = strndup(buf->data, buf->size);
		if (nres->res == NULL)
			goto err;
	}
	hbuf_free(buf);
	return nq;
err:
	hbuf_free(buf);
	op_resq_free(nq);
	return NULL;
}

/*
 * Escape all characters in all list items for general LaTeX content.
 * Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_escape_latex(struct op_out *out,
    const struct op_resq *input)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf = NULL;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!lowdown_latex_esc(buf, res->res, strlen(res->res)))
			goto err;
		if ((nres = calloc(1, sizeof(struct op_res))) == NULL)
			goto err;
		TAILQ_INSERT_TAIL(nq, nres, entries);
		nres->res = strndup(buf->data, buf->size);
		if (nres->res == NULL)
			goto err;
	}
	hbuf_free(buf);
	return nq;
err:
	hbuf_free(buf);
	op_resq_free(nq);
	return NULL;
}

/*
 * Escape all characters in all list items for Gemini, either over
 * multiple lines or with newlines removed for a single line.
 * Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_escape_gemini(struct op_out *out,
    const struct op_resq *input, int oneline)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf = NULL;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!lowdown_gemini_esc(buf, res->res, strlen(res->res),
		    oneline))
			goto err;
		if ((nres = calloc(1, sizeof(struct op_res))) == NULL)
			goto err;
		TAILQ_INSERT_TAIL(nq, nres, entries);
		nres->res = strndup(buf->data, buf->size);
		if (nres->res == NULL)
			goto err;
	}
	hbuf_free(buf);
	return nq;
err:
	hbuf_free(buf);
	op_resq_free(nq);
	return NULL;
}

/*
 * Escape all characters in all list items for roff (ms/man), either
 * over multiple lines or with newlines removed for a single line.
 * Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_escape_roff(struct op_out *out,
    const struct op_resq *input, int oneline)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf = NULL;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!lowdown_nroff_esc(buf, res->res, strlen(res->res),
		    oneline, 0))
			goto err;
		if ((nres = calloc(1, sizeof(struct op_res))) == NULL)
			goto err;
		TAILQ_INSERT_TAIL(nq, nres, entries);
		nres->res = strndup(buf->data, buf->size);
		if (nres->res == NULL)
			goto err;
	}
	hbuf_free(buf);
	return nq;
err:
	hbuf_free(buf);
	op_resq_free(nq);
	return NULL;
}

/*
 * Lowercase or uppercase all characters in all list items.  Returns
 * NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_case(struct op_out *out, const struct op_resq *input,
    int lower)
{
	struct op_resq	*nq;
	struct op_res	*nres;
	char		*cp;

	if ((nq = op_resq_clone(input, 0)) == NULL)
		return NULL;
	TAILQ_FOREACH(nres, nq, entries)
		for (cp = nres->res; *cp != '\0'; cp++)
			*cp = lower ?
				tolower((unsigned char)*cp) :
				toupper((unsigned char)*cp);
	return nq;
}

/*
 * Join all list items into a singleton, with two white-spaces
 * delimiting the new string.  If the input list is empty, produces an
 * empty output.  Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_join(struct op_out *out, const struct op_resq *input)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	size_t			 sz = 0;
	void			*p;

	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	/* Empty list -> empty singleton. */

	if (TAILQ_EMPTY(input))
		return nq;

	if ((nres = calloc(1, sizeof(struct op_res))) == NULL)
		goto err;
	TAILQ_INSERT_TAIL(nq, nres, entries);

	TAILQ_FOREACH(res, input, entries) {
		if (sz == 0) {
			if ((nres->res = strdup(res->res)) == NULL)
				goto err;
			sz = strlen(nres->res) + 1;
		} else {
			sz += strlen(res->res) + 2;
			if ((p = realloc(nres->res, sz)) == NULL)
				goto err;
			nres->res = p;
			strlcat(nres->res, "  ", sz);
			strlcat(nres->res, res->res, sz);
		}
	}
	assert(sz > 0);
	return nq;
err:
	op_resq_free(nq);
	return NULL;
}

static struct op_resq *
op_eval_function(struct op_out *out, const char *expr, size_t exprsz,
    const char *args, size_t argsz, const struct op_resq *input)
{
	struct op_resq	*nq;

	if (!op_debug(out, "%s: %.*s", __func__, (int)exprsz, expr))
		return NULL;
	out->depth++;

	if (exprsz == 9 && strncasecmp(expr, "uppercase", 9) == 0)
		nq = op_eval_function_case(out, input, 0);
	else if (exprsz == 9 && strncasecmp(expr, "lowercase", 9) == 0)
		nq = op_eval_function_case(out, input, 1);
	else if (exprsz == 5 && strncasecmp(expr, "split", 5) == 0)
		nq = op_eval_function_split(out, input);
	else if (exprsz == 4 && strncasecmp(expr, "join", 4) == 0)
		nq = op_eval_function_join(out, input);
	else if (exprsz == 4 && strncasecmp(expr, "trim", 4) == 0)
		nq = op_resq_clone(input, 1);
	else if (exprsz == 12 && strncasecmp(expr, "escapegemini", 12) == 0)
		nq = op_eval_function_escape_gemini(out, input, 0);
	else if (exprsz == 16 && strncasecmp(expr, "escapegeminiline", 16) == 0)
		nq = op_eval_function_escape_gemini(out, input, 1);
	else if (exprsz == 10 && strncasecmp(expr, "escapehtml", 10) == 0)
		nq = op_eval_function_escape_html(out, input);
	else if (exprsz == 14 && strncasecmp(expr, "escapehtmlattr", 14) == 0)
		nq = op_eval_function_escape_htmlattr(out, input);
	else if (exprsz == 13 && strncasecmp(expr, "escapehtmlurl", 13) == 0)
		nq = op_eval_function_escape_htmlurl(out, input);
	else if (exprsz == 11 && strncasecmp(expr, "escapelatex", 11) == 0)
		nq = op_eval_function_escape_latex(out, input);
	else if (exprsz == 10 && strncasecmp(expr, "escaperoff", 10) == 0)
		nq = op_eval_function_escape_roff(out, input, 0);
	else if (exprsz == 14 && strncasecmp(expr, "escaperoffline", 14) == 0)
		nq = op_eval_function_escape_roff(out, input, 1);
	else {
		if (!op_debug(out, "transform not recognised"))
			return NULL;
		if ((nq = malloc(sizeof(struct op_resq))) != NULL)
			TAILQ_INIT(nq);
	}

	out->depth--;
	return nq;
}

/*
 * The initial expression in an expression chain must resolve to a
 * variable of some sort.  This consists of metadata or "special"
 * variables.  Evaluate this variable to either a non-empty singleton or
 * an empty list.  Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_initial(struct op_out *out, const char *expr, size_t exprsz,
    const char *args, size_t argsz, const char *this)
{
	struct op_resq			*q, *resq;
	struct op_res			*res;
	struct op_argq			 argq;
	struct op_arg			*arg;
	const struct lowdown_meta	*m;
	const char			*v = NULL;
	size_t				 vsz;
	int				 rc;

	if (!op_debug(out, "%s: %.*s", __func__, (int)exprsz, expr))
		return NULL;
	out->depth++;

	if ((q = malloc(sizeof(struct op_resq))) == NULL)
		return NULL;

	TAILQ_INIT(&argq);
	TAILQ_INIT(q);

	if (exprsz == 4 && strncasecmp(expr, "this", 4) == 0) {
		/* Anaphoric keyword in current loop or NULL. */
		v = this;
		vsz = this == NULL ? 0 : strlen(this);
	} else if (exprsz == 4 && strncasecmp(expr, "body", 4) == 0) {
		/* Body of HTML document. */
		v = out->content->data;
		vsz = out->content->size;
	} else if (exprsz == 3 && strncasecmp(expr, "not", 3) == 0) {
		/* "NOT" of argument.  The rest are ignored. */
		resq = op_eval(out, args, argsz, this, NULL);
		if (resq == NULL)
			goto err;
		rc = TAILQ_EMPTY(resq);
		op_resq_free(resq);
		if (rc == 1) {
			v = "true";
			vsz = 4;
		}
	} else if (exprsz == 2 && strncasecmp(expr, "or", 2) == 0) {
		/* "OR" of all arguments. Short-circuit on TRUE. */
		if (!op_argq_new(&argq, args, argsz))
			goto err;
		rc = 0;
		TAILQ_FOREACH(arg, &argq, entries) {
			resq = op_eval(out, arg->arg, arg->argsz, this,
				NULL);
			if (resq == NULL)
				goto err;
			rc = !TAILQ_EMPTY(resq);
			op_resq_free(resq);
			if (rc == 1)
				break;
		}
		if (rc == 1) {
			v = "true";
			vsz = 4;
		}
	} else if (exprsz == 3 && strncasecmp(expr, "and", 3) == 0) {
		/* "AND" of all arguments.  Short-circuit on FALSE. */
		if (!op_argq_new(&argq, args, argsz))
			goto err;
		rc = TAILQ_EMPTY(&argq) ? 0 : 1;
		TAILQ_FOREACH(arg, &argq, entries) {
			resq = op_eval(out, arg->arg, arg->argsz, this,
				NULL);
			if (resq == NULL)
				goto err;
			rc = !TAILQ_EMPTY(resq);
			op_resq_free(resq);
			if (rc == 0)
				break;
		}
		if (rc != 0) {
			v = "true";
			vsz = 4;
		}
	} else {
		/*
		 * If "meta", interpret argument as being a metadata
		 * key, allowing the use of the overridden names e.g.
		 * body.
		 */
		if (exprsz == 4 && strncasecmp(expr, "meta", 4) == 0 &&
		    args != NULL) {
			if (!op_debug(out, "arg: %.*s",
			    (int)argsz, args))
				goto err;
			expr = args;
			exprsz = argsz;
		}

		TAILQ_FOREACH(m, out->mq, entries)
			if (strlen(m->key) == exprsz &&
			    strncasecmp(m->key, expr, exprsz) == 0) {
				v = m->value;
				vsz = strlen(m->value);
				break;
			}
	}

	op_argq_free(&argq);
	out->depth--;

	/* Invariant: non-empty or empty list. */

	if (v == NULL || v[0] == '\0')
		return q;
	if ((res = calloc(1, sizeof(struct op_res))) == NULL)
		goto err;
	TAILQ_INSERT_TAIL(q, res, entries);
	if ((res->res = strndup(v, vsz)) == NULL)
		goto err;
	return q;
err:
	op_resq_free(q);
	op_argq_free(&argq);
	return NULL;
}

static struct op_resq *
op_eval(struct op_out *out, const char *expr, size_t sz,
    const char *this, const struct op_resq *input)
{
	size_t	 	 nextsz = 0, exprsz, argsz = 0;
	const char	*next, *args;
	struct op_resq	*q, *nextq;

	if (sz == 0) {
		if ((q = malloc(sizeof(struct op_resq))) == NULL)
			return NULL;
		TAILQ_INIT(q);
		return q;
	}

	/* Find next expression in chain. */

	next = memchr(expr, '.', sz);
	if (next != NULL) {
		assert(next >= expr);
		exprsz = (size_t)(next - expr);
		next++;
		assert(next > expr);
		nextsz = sz - (size_t)(next - expr);
	} else
		exprsz = sz;

	if (exprsz > 0 && expr[exprsz - 1] == ')' &&
	    ((args = memchr(expr, '(', exprsz)) != NULL)) {
		argsz = &expr[exprsz - 1] - args - 1;
		exprsz = args - expr;
		args++;
	}

	/*
	 * If input is NULL, this is the first of the chain, so it
	 * should resolve to a variable.  Otherwise, it's a function.
	 */

	q = (input == NULL) ?
		op_eval_initial(out, expr, exprsz, args, argsz, this) :
		op_eval_function(out, expr, exprsz, args, argsz, input);

	/* Return or pass to the next, then free current. */

	if (next == NULL)
		return q;

	nextq = op_eval(out, next, nextsz, this, q);
	op_resq_free(q);
	return nextq;
}

/*
 * Copy the string into the output.  Returns zero on failure (memory
 * allocation), non-zero on success.
 */
static int
op_exec_str(struct op_out *out, const struct op *op)
{
	assert(op->op_type == OP_STR);

	if (!op_debug(out, "length: %zu", op->op_str.sz))
		return 0;
	if (out->debug)
		return 1;
	return hbuf_put(out->ob, op->op_str.str, op->op_str.sz);
}

/*
 * Copy the result running an expression into the output.  Returns zero
 * on failure (memory allocation), non-zero on success.
 */
static int
op_exec_expr(struct op_out *out, const struct op *op, const char *this)
{
	struct op_resq	*resq;
	struct op_res	*res;
	int		 rc = 0, first = 1;

	assert(op->op_type == OP_EXPR);
	resq = op_eval(out, op->op_expr.expr, op->op_expr.sz, this,
		NULL);
	if (resq == NULL)
		return 0;
	if (!out->debug)
		TAILQ_FOREACH(res, resq, entries) {
			if (!first && !HBUF_PUTSL(out->ob, "  "))
				goto out;
			if (!hbuf_puts(out->ob, res->res))
				goto out;
			first = 0;
		}
	rc = 1;
out:
	op_resq_free(resq);
	return rc;
}

/*
 * Execute the body of a conditional or its "else", if found, depending
 * on how the expression evaluates.
 */
static int
op_exec_for(struct op_out *out, const struct op *op, const char *this)
{
	struct op_resq	*resq;
	struct op_res	*res;
	size_t		 loops = 0;

	assert(op->op_type == OP_FOR);

	/* Empty arguments evaluate to an empty list. */

	if (op->op_for.sz == 0) {
		if (!op_debug(out, "no loop expression"))
			return 0;
		return 1;
	}

	resq = op_eval(out, op->op_for.expr, op->op_for.sz, this, NULL);
	if (resq == NULL)
		return 0;

	TAILQ_FOREACH(res, resq, entries) {
		if (!op_debug(out, "loop iteration: %zu", ++loops))
			return 0;
		if (!op_exec(out, op, res->res)) {
			op_resq_free(resq);
			return 0;
		}
	}

	if (loops == 0 && !op_debug(out, "no loop iterations"))
		return 0;

	op_resq_free(resq);
	return 1;
}

/*
 * Execute the body of a conditional or its "else", if found, depending
 * on how the expression evaluates.
 */
static int
op_exec_ifdef(struct op_out *out, const struct op *op, const char *this)
{
	struct op_resq	*resq;
	int	 	 rc;

	assert(op->op_type == OP_IFDEF);

	/* Empty arguments evaluate to an empty list. */

	if (op->op_ifdef.sz > 0) {
		resq = op_eval(out, op->op_ifdef.expr, op->op_ifdef.sz,
			this, NULL);
		if (resq == NULL)
			return 0;
		rc = !TAILQ_EMPTY(resq);
		op_resq_free(resq);
	} else
		rc = 0;

	if (!op_debug(out, "result: %s%s", rc ? "true" : "false",
	    rc || op->op_ifdef.chain == NULL ? "" :
	    " (taking else branch)"))
		return 0;

	return rc ? op_exec(out, op,this) :
		op->op_ifdef.chain == NULL ? 1 :
		op_exec(out, op->op_ifdef.chain, this);
}

static int
op_exec(struct op_out *out, const struct op *cop, const char *this)
{
	const struct op	*op;

	out->depth++;
	TAILQ_FOREACH(op, &cop->children, _siblings) {
		if (!op_debug(out, "%s: %s", __func__,
		    op_types[op->op_type]))
			return 0;
		out->depth++;
		switch (op->op_type) {
		case OP_STR:
			if (!op_exec_str(out, op))
				return 0;
			break;
		case OP_EXPR:
			if (!op_exec_expr(out, op, this))
				return 0;
			break;
		case OP_IFDEF:
			if (!op_exec_ifdef(out, op, this))
				return 0;
			break;
		case OP_FOR:
			if (!op_exec_for(out, op, this))
				return 0;
			break;
		case OP_ELSE:
			/*
			 * These are run if the condition in a prior
			 * "if" statement fails, otherwise the block is
			 * never run.
			 */
			break;
		case OP_ROOT:
			break;
		}
		out->depth--;
	}
	out->depth--;

	return 1;
}

/*
 * Fill in the output-specific template string "templ" with a document
 * body of "content" into "ob".
 */
int
lowdown_template(const char *templ, const struct lowdown_buf *content,
    struct lowdown_buf *ob, const struct lowdown_metaq *mq, int dbg)
{
	char		 delim;
	const char	*cp, *nextcp, *savecp;
	struct opq	 q;
	struct op	*op, *cop, *root;
	int		 rc = 0, igneoln;
	size_t		 sz;
	struct op_out	 out;

	TAILQ_INIT(&q);
	if ((root = op_alloc(&q, OP_ROOT, NULL)) == NULL)
		return 0;

	/* Consume all template bytes. */

	for (cop = root, cp = templ; *cp != '\0'; ) {
		/*
		 * Scan ahead to the next variable delimiter.  If none
		 * is found, stop processing.
		 */

		if ((nextcp = strchr(cp, '$')) == NULL)
			break;

		savecp = nextcp;

		/* Output all text up to the delimiter. */

		assert(nextcp >= cp);
		if (!op_queue_str(&q, cop, cp, (size_t)(nextcp - cp)))
			goto out;
		cp = nextcp + 1;

		/* Determine the closing delimiter. */

		if (cp[0] == '{') {
			delim = '}';
			cp++;
		} else
			delim = '$';

		/*
		 * If the closing delimiter was not found, revert to the
		 * beginning of the delimit sequence and bail out.
		 */

		if ((nextcp = strchr(cp, delim)) == NULL) {
			cp = savecp;
			break;
		}

		/*
		 * A double-hypen before the end delimiter means that
		 * input must be consumed up to and including the eoln
		 * following the instruction.
		 */

		igneoln = nextcp > (cp + 2) &&
			nextcp[-1] == '-' &&
			nextcp[-2] == '-';

		/*
		 * From "cp" to "nextcp" is the statement to evaluate.
		 * Trim the string.
		 */

		assert(nextcp >= cp);
		for ( ; cp < nextcp; cp++)
			if (*cp != ' ' && *cp != '\t')
				break;

		assert(nextcp >= cp);
		sz = (size_t)(nextcp - cp);

		/* Account for double-hyphens. */

		if (igneoln)
			sz -= 2;

		while (sz > 0 &&
		    (cp[sz - 1] == ' ' || cp[sz - 1] == '\t'))
			sz--;

		/* Fully empty statements output a literal '$'. */

		if (sz == 0) {
			if (!op_queue_str(&q, cop, "$", 1))
				goto out;
			cp++;
			continue;
		}

		/* Look up and process the operation. */

		if (sz > 0 && !op_queue(&q, &cop, cp, sz))
			goto out;

		cp = nextcp + 1;

		/* Special instruction that ignores til eoln/f. */

		if (igneoln) {
			while (*cp != '\0' && *cp != '\n')
				cp++;
			if (*cp == '\n')
				cp++;
		}
	}

	/* Mop up any remaining tokens. */

	if (*cp != '\0' && !op_queue_str(&q, cop, cp, strlen(cp)))
		goto out;

	/* Execute the generation operation tree. */

	out.debug = dbg;
	out.ob = ob;
	out.content = content;
	out.mq = mq;
	out.depth = 0;
	rc = op_exec(&out, root, NULL);
out:
	while ((op = TAILQ_FIRST(&q)) != NULL) {
		TAILQ_REMOVE(&q, op, _all);
		free(op);
	}
	return rc;
}
