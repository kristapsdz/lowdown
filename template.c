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
#include <err.h> /* DEBUG */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

TAILQ_HEAD(opq, op);

/*
 * Operation for outputting an opaque string.
 */
struct op_str {
	const char	*str; /* content */
	size_t		 sz; /* length of content */
};

struct op_expr {
	const char	*expr; /* expression */
	size_t		 sz; /* length of expression */
};

struct op_ifdef {
	const char	*expr;
	size_t		 sz;
	const struct op	*chain;
};

enum op_type {
	OP_IFDEF,
	OP_ELSE,
	OP_STR,
	OP_EXPR,
	OP_ROOT,
};

struct op {
	union {
		struct op_ifdef	 op_ifdef;
		struct op_str	 op_str;
		struct op_expr	 op_expr;
	};
	enum op_type		 op_type;
	struct opq		 children;
	struct op		*parent;
	TAILQ_ENTRY(op)		 _siblings;
	TAILQ_ENTRY(op)		 _all;
};

/* Forward declaration. */

static int op_exec(const struct op *, struct lowdown_buf *,
    const struct lowdown_metaq *);

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
 * Respond to an end-conditional.  If not currently in a conditional
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
	/* FIXME: space before "(". */

	if (sz > 7 && strncmp(str, "ifdef(", 6) == 0 &&
	    str[sz - 1] == ')')
		return op_queue_ifdef(q, cop, str + 6, sz - 7);
	if (sz == 4 && strncmp(str, "else", 4) == 0)
		return op_queue_else(q, cop);
	if (sz == 5 && strncmp(str, "endif", 5) == 0)
		return op_queue_endif(cop);

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

static char *
op_eval(const char *expr, size_t sz, const struct lowdown_metaq *mq)
{
	const struct lowdown_meta	*m;

	TAILQ_FOREACH(m, mq, entries)
		if (strlen(m->key) == sz &&
		    strncmp(m->key, expr, sz) == 0)
			return strdup(m->value);

	return strdup("");
}

/*
 * Copy the result running an expression into the output.  Returns zero
 * on failure (memory allocation), non-zero on success.
 */
static int
op_exec_expr(const struct op *op, struct lowdown_buf *ob,
    const struct lowdown_metaq *mq)
{
	char	*cp;
	int	 rc;

	assert(op->op_type == OP_EXPR);
	cp = op_eval(op->op_expr.expr, op->op_expr.sz, mq);
	if (cp == NULL)
		return 0;
	rc = hbuf_puts(ob, cp);
	free(cp);
	return rc;
}

/*
 * Execute the body of a conditional or its "else", if found, depending
 * on how the expression evaluates.
 */
static int
op_exec_ifdef(const struct op *op, struct lowdown_buf *ob,
    const struct lowdown_metaq *mq)
{
	char	*cp;
	int	 rc;

	assert(op->op_type == OP_IFDEF);
	cp = op_eval(op->op_ifdef.expr, op->op_ifdef.sz, mq);
	if (cp == NULL)
		return 0;
	rc = cp[0] != '\0';
	free(cp);
	return rc ? op_exec(op, ob, mq) :
		op->op_ifdef.chain == NULL ? 1 :
		op_exec(op->op_ifdef.chain, ob, mq);
}

static int
op_exec(const struct op *cop, struct lowdown_buf *ob,
    const struct lowdown_metaq *mq)
{
	const struct op	*op;

	TAILQ_FOREACH(op, &cop->children, _siblings)
		switch (op->op_type) {
		case OP_STR:
			if (!op_exec_str(op, ob))
				return 0;
			break;
		case OP_EXPR:
			if (!op_exec_expr(op, ob, mq))
				return 0;
			break;
		case OP_IFDEF:
			if (!op_exec_ifdef(op, ob, mq))
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

		savecp = cp;

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

		if (!op_queue(&q, &cop, cp, sz))
			goto out;

		cp = nextcp + 1;
	}

	/* Mop up any remaining tokens. */

	if (*cp != '\0' && !op_queue_str(&q, cop, cp, strlen(cp)))
		goto out;

	/* Execute the generation operation tree. */

	rc = op_exec(root, ob, mq);
out:
	while ((op = TAILQ_FIRST(&q)) != NULL) {
		TAILQ_REMOVE(&q, op, _all);
		free(op);
	}
	return rc;
}
