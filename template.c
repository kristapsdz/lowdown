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

TAILQ_HEAD(opq, op);
TAILQ_HEAD(op_resq, op_res);

struct op_res {
	char		*res;
	TAILQ_ENTRY(op_res) entries;
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

enum op_type {
	OP_FOR,
	OP_IFDEF,
	OP_ELSE,
	OP_STR,
	OP_EXPR,
	OP_ROOT,
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

/* Forward declaration. */

static int op_exec(const struct op *, struct lowdown_buf *,
    const struct lowdown_metaq *, const struct lowdown_buf *,
    const char *);

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

	if (sz > 7 && strncasecmp(str, "ifdef(", 6) == 0 &&
	    str[sz - 1] == ')')
		return op_queue_ifdef(q, cop, str + 6, sz - 7);
	if (sz > 5 && strncasecmp(str, "for(", 4) == 0 &&
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
 * Copy the string into the output.  Returns zero on failure (memory
 * allocation), non-zero on success.
 */
static int
op_exec_str(const struct op *op, struct lowdown_buf *ob)
{
	assert(op->op_type == OP_STR);
	return hbuf_put(ob, op->op_str.str, op->op_str.sz);
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

/*
 * Split each input string along white-space boundaries.  This trims
 * white-space around all strings during the split.  The result is a
 * list of non-empty, non-only-whitespace strings.  Returns NULL on
 * allocation failure.
 */
static struct op_resq *
op_eval_function_split(const struct lowdown_metaq *mq,
    const struct op_resq *input)
{
	struct op_resq	*nq;
	struct op_res	*nres, *nnres;
	char		*cp, *ncp;

	if ((nq = op_resq_clone(input, 1)) == NULL)
		return NULL;

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
		if ((nnres = calloc(1, sizeof(struct op_res))) == NULL) {
			op_resq_free(nq);
			return NULL;
		}
		TAILQ_INSERT_AFTER(nq, nres, nnres, entries);
		if ((nnres->res = strdup(ncp)) == NULL) {
			op_resq_free(nq);
			return NULL;
		}
	}

	return nq;
}

/*
 * HTML-escape (for URL attributes) all characters in all list items.
 * Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_escape_htmlurl(const struct lowdown_metaq *mq,
    const struct op_resq *input)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!hesc_href(buf, res->res, strlen(res->res)))
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
 * HTML-escape (for attributes) all characters in all list items.
 * Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_escape_htmlattr(const struct lowdown_metaq *mq,
    const struct op_resq *input)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!hesc_attr(buf, res->res, strlen(res->res)))
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
op_eval_function_escape_html(const struct lowdown_metaq *mq,
    const struct op_resq *input)
{
	struct op_resq		*nq = NULL;
	struct op_res		*nres;
	const struct op_res	*res;
	struct lowdown_buf	*buf;

	if ((buf = hbuf_new(32)) == NULL)
		goto err;
	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		goto err;
	TAILQ_INIT(nq);

	TAILQ_FOREACH(res, input, entries) {
		hbuf_truncate(buf);
		if (!hesc_html(buf, res->res, strlen(res->res),
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
 * Lowercase all characters in all list items.  Returns NULL on
 * allocation failure.
 */
static struct op_resq *
op_eval_function_lowercase(const struct lowdown_metaq *mq,
    const struct op_resq *input)
{
	struct op_resq	*nq;
	struct op_res	*nres;
	char		*cp;

	if ((nq = op_resq_clone(input, 0)) == NULL)
		return NULL;
	TAILQ_FOREACH(nres, nq, entries)
		for (cp = nres->res; *cp != '\0'; cp++)
			*cp = tolower((unsigned char)*cp);
	return nq;
}

/*
 * Uppercase all characters in all list items.  Returns NULL on
 * allocation failure.
 */
static struct op_resq *
op_eval_function_uppercase(const struct lowdown_metaq *mq,
    const struct op_resq *input)
{
	struct op_resq	*nq;
	struct op_res	*nres;
	char		*cp;

	if ((nq = op_resq_clone(input, 0)) == NULL)
		return NULL;
	TAILQ_FOREACH(nres, nq, entries)
		for (cp = nres->res; *cp != '\0'; cp++)
			*cp = toupper((unsigned char)*cp);
	return nq;
}

/*
 * Join all list items into a singleton, with two white-spaces
 * delimiting the new string.  If the input list is empty, produces an
 * empty output.  Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_function_join(const struct lowdown_metaq *mq,
    const struct op_resq *input)
{
	struct op_resq		*nq;
	struct op_res		*nres;
	const struct op_res	*res;
	size_t			 sz = 0;
	void			*p;

	if ((nq = malloc(sizeof(struct op_resq))) == NULL)
		return NULL;
	TAILQ_INIT(nq);

	/* Empty list -> empty singleton. */

	if (TAILQ_EMPTY(input))
		return nq;

	if ((nres = calloc(1, sizeof(struct op_res))) == NULL) {
		op_resq_free(nq);
		return NULL;
	}
	TAILQ_INSERT_TAIL(nq, nres, entries);
	nres->res = NULL;
	TAILQ_FOREACH(res, input, entries) {
		if (sz == 0) {
			if ((nres->res = strdup(res->res)) == NULL) {
				op_resq_free(nq);
				return NULL;
			}
			sz = strlen(nres->res) + 1;
		} else {
			sz += strlen(res->res) + 2;
			if ((p = realloc(nres->res, sz)) == NULL) {
				op_resq_free(nq);
				return NULL;
			}
			nres->res = p;
			strlcat(nres->res, "  ", sz);
			strlcat(nres->res, res->res, sz);
		}
	}
	assert(sz > 0);
	return nq;
}

static struct op_resq *
op_eval_function(const char *expr, size_t exprsz, const char *args,
    size_t argsz, const struct lowdown_metaq *mq,
    const struct op_resq *input)
{
	struct op_resq	*nq;

	if (exprsz == 9 && strncasecmp(expr, "uppercase", 9) == 0)
		nq = op_eval_function_uppercase(mq, input);
	else if (exprsz == 9 && strncasecmp(expr, "lowercase", 9) == 0)
		nq = op_eval_function_lowercase(mq, input);
	else if (exprsz == 5 && strncasecmp(expr, "split", 5) == 0)
		nq = op_eval_function_split(mq, input);
	else if (exprsz == 4 && strncasecmp(expr, "join", 4) == 0)
		nq = op_eval_function_join(mq, input);
	else if (exprsz == 4 && strncasecmp(expr, "trim", 4) == 0)
		nq = op_resq_clone(input, 1);
	else if (exprsz == 10 && strncasecmp(expr, "escapehtml", 10) == 0)
		nq = op_eval_function_escape_html(mq, input);
	else if (exprsz == 14 && strncasecmp(expr, "escapehtmlattr", 14) == 0)
		nq = op_eval_function_escape_htmlattr(mq, input);
	else if (exprsz == 13 && strncasecmp(expr, "escapehtmlurl", 13) == 0)
		nq = op_eval_function_escape_htmlurl(mq, input);
	else if ((nq = malloc(sizeof(struct op_resq))) != NULL)
		TAILQ_INIT(nq);

	return nq;
}

/*
 * The initial expression in an expression chain must resolve to a
 * variable of some sort.  This consists of metadata or "special"
 * variables.  Evaluate this variable to either a non-empty singleton or
 * an empty list.  Returns NULL on allocation failure.
 */
static struct op_resq *
op_eval_initial(const char *expr, size_t exprsz, const char *args,
    size_t argsz, const char *this, const struct lowdown_metaq *mq,
    const struct lowdown_buf *content)
{
	struct op_resq			*q;
	struct op_res			*res;
	const struct lowdown_meta	*m;
	const char			*v = NULL;
	size_t				 vsz;

	if ((q = malloc(sizeof(struct op_resq))) == NULL)
		return NULL;

	TAILQ_INIT(q);

	if (exprsz == 4 && strncasecmp(expr, "this", exprsz) == 0) {
		/* Anaphoric keyword in current loop or NULL. */
		v = this;
		vsz = this == NULL ? 0 : strlen(this);
	} else if (exprsz == 4 && strncasecmp(expr, "body", exprsz) == 0) {
		/* Body of HTML document. */
		v = content->data;
		vsz = content->size;
	} else {
		/*
		 * If "meta", interpret argument as being a metadata
		 * key, allowing the use of the overridden names e.g.
		 * body.
		 */
		if (exprsz == 4 &&
		    strncasecmp(expr, "meta", exprsz) == 0 &&
		    args != NULL) {
			expr = args;
			exprsz = argsz;
		}
		TAILQ_FOREACH(m, mq, entries)
			if (strlen(m->key) == exprsz &&
			    strncasecmp(m->key, expr, exprsz) == 0) {
				v = m->value;
				vsz = strlen(m->value);
				break;
			}
	}

	/* Invariant: non-empty or empty list. */

	if (v == NULL || v[0] == '\0')
		return q;

	if ((res = calloc(1, sizeof(struct op_res))) == NULL) {
		op_resq_free(q);
		return NULL;
	}
	TAILQ_INSERT_TAIL(q, res, entries);
	if ((res->res = strndup(v, vsz)) == NULL) {
		op_resq_free(q);
		return NULL;
	}
	return q;
}

static struct op_resq *
op_eval(const char *expr, size_t sz, const struct lowdown_metaq *mq,
    const char *this, const struct op_resq *input,
    const struct lowdown_buf *content)
{
	size_t	 	 nextsz = 0, exprsz, argsz = 0;
	const char	*next, *args;
	struct op_resq	*q, *nextq;

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
		op_eval_initial(expr, exprsz, args, argsz, this, mq,
			content) :
		op_eval_function(expr, exprsz, args, argsz, mq, input);

	/* Return or pass to the next, then free current. */

	if (next == NULL)
		return q;

	nextq = op_eval(next, nextsz, mq, this, q, content);
	op_resq_free(q);
	return nextq;
}

/*
 * Copy the result running an expression into the output.  Returns zero
 * on failure (memory allocation), non-zero on success.
 */
static int
op_exec_expr(const struct op *op, struct lowdown_buf *ob,
    const struct lowdown_metaq *mq, const char *this,
    const struct lowdown_buf *content)
{
	struct op_resq	*resq;
	struct op_res	*res;
	int		 rc = 0, first = 1;

	assert(op->op_type == OP_EXPR);
	resq = op_eval(op->op_expr.expr, op->op_expr.sz, mq, this,
		NULL, content);
	if (resq == NULL)
		return 0;

	TAILQ_FOREACH(res, resq, entries) {
		if (!first && !HBUF_PUTSL(ob, "  "))
			goto out;
		if (!hbuf_puts(ob, res->res))
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
op_exec_for(const struct op *op, struct lowdown_buf *ob,
    const struct lowdown_metaq *mq, const char *this,
    const struct lowdown_buf *content)
{
	struct op_resq	*resq;
	struct op_res	*res;

	assert(op->op_type == OP_FOR);
	resq = op_eval(op->op_for.expr, op->op_for.sz, mq, this, NULL,
		content);
	if (resq == NULL)
		return 0;

	TAILQ_FOREACH(res, resq, entries)
		if (!op_exec(op, ob, mq, content, res->res)) {
			op_resq_free(resq);
			return 0;
		}

	op_resq_free(resq);
	return 1;
}

/*
 * Execute the body of a conditional or its "else", if found, depending
 * on how the expression evaluates.
 */
static int
op_exec_ifdef(const struct op *op, struct lowdown_buf *ob,
    const struct lowdown_metaq *mq, const char *this,
    const struct lowdown_buf *content)
{
	struct op_resq	*resq;
	int	 	 rc;

	assert(op->op_type == OP_IFDEF);
	resq = op_eval(op->op_ifdef.expr, op->op_ifdef.sz, mq, this,
		NULL, content);
	if (resq == NULL)
		return 0;

	rc = !TAILQ_EMPTY(resq);
	op_resq_free(resq);

	return rc ? op_exec(op, ob, mq, content, this) :
		op->op_ifdef.chain == NULL ? 1 :
		op_exec(op->op_ifdef.chain, ob, mq, content, this);
}

static int
op_exec(const struct op *cop, struct lowdown_buf *ob,
    const struct lowdown_metaq *mq, const struct lowdown_buf *content,
    const char *this)
{
	const struct op	*op;

	TAILQ_FOREACH(op, &cop->children, _siblings)
		switch (op->op_type) {
		case OP_STR:
			if (!op_exec_str(op, ob))
				return 0;
			break;
		case OP_EXPR:
			if (!op_exec_expr(op, ob, mq, this, content))
				return 0;
			break;
		case OP_IFDEF:
			if (!op_exec_ifdef(op, ob, mq, this, content))
				return 0;
			break;
		case OP_FOR:
			if (!op_exec_for(op, ob, mq, this, content))
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

	return 1;
}

/*
 * Fill in the output-specific template string "templ" with a document
 * body of "content" into "ob".
 */
int
lowdown_template(const char *templ, const struct lowdown_buf *content,
    struct lowdown_buf *ob, const struct lowdown_metaq *mq)
{
	char		 delim;
	const char	*cp, *nextcp, *savecp;
	struct opq	 q;
	struct op	*op, *cop, *root;
	int		 rc = 0;
	size_t		 sz;

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

		/*
		 * If the next character is a '$', output that as a
		 * literal '$'.
		 */

		cp = nextcp + 1;
		if (*cp == '$') {
			if (!op_queue_str(&q, cop, "$", 1))
				goto out;
			cp++;
			continue;
		}

		/* Determine the closing delimiter. */

		if (*cp == '{') {
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
		 * From "cp" to "nextcp" is the statement to evaluate.
		 * Trim the string.
		 */

		assert(nextcp >= cp);
		for ( ; cp < nextcp; cp++)
			if (*cp != ' ' && *cp != '\t')
				break;

		assert(nextcp >= cp);
		sz = (size_t)(nextcp - cp);

		while (sz > 0 &&
		    (cp[sz - 1] == ' ' || cp[sz - 1] == '\t'))
			sz--;

		/* Look up and process the operation. */

		if (sz > 0 && !op_queue(&q, &cop, cp, sz))
			goto out;

		cp = nextcp + 1;
	}

	/* Mop up any remaining tokens. */

	if (*cp != '\0' && !op_queue_str(&q, cop, cp, strlen(cp)))
		goto out;

	/* Execute the generation operation tree. */

	rc = op_exec(root, ob, mq, content, NULL);
out:
	while ((op = TAILQ_FIRST(&q)) != NULL) {
		TAILQ_REMOVE(&q, op, _all);
		free(op);
	}
	return rc;
}
