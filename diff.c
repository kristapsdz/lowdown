/*	$Id$ */
/*
 * Copyright (c) 2017 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/queue.h>
#include <sys/types.h>

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

struct	xnode {
	char		 sig[MD5_DIGEST_STRING_LENGTH]; /* signature */
	size_t		 weight; /* priority queue weight */
	const struct lowdown_node *node; /* basis node */
	const struct lowdown_node *match;
	size_t		 optimality;
};

struct	xmap {
	struct xnode	*nodes;
	size_t		 maxid;
	size_t		 maxsize;
};

struct	pnode {
	const struct lowdown_node *node;
	TAILQ_ENTRY(pnode) entries;
};

TAILQ_HEAD(pnodeq, pnode);

static	const char *const names[LOWDOWN__MAX] = {
	"LOWDOWN_ROOT",			/* LOWDOWN_ROOT */
	"LOWDOWN_BLOCKCODE",            /* LOWDOWN_BLOCKCODE */
	"LOWDOWN_BLOCKQUOTE",           /* LOWDOWN_BLOCKQUOTE */
	"LOWDOWN_HEADER",               /* LOWDOWN_HEADER */
	"LOWDOWN_HRULE",                /* LOWDOWN_HRULE */
	"LOWDOWN_LIST",                 /* LOWDOWN_LIST */
	"LOWDOWN_LISTITEM",             /* LOWDOWN_LISTITEM */
	"LOWDOWN_PARAGRAPH",            /* LOWDOWN_PARAGRAPH */
	"LOWDOWN_TABLE_BLOCK",          /* LOWDOWN_TABLE_BLOCK */
	"LOWDOWN_TABLE_HEADER",         /* LOWDOWN_TABLE_HEADER */
	"LOWDOWN_TABLE_BODY",           /* LOWDOWN_TABLE_BODY */
	"LOWDOWN_TABLE_ROW",            /* LOWDOWN_TABLE_ROW */
	"LOWDOWN_TABLE_CELL",           /* LOWDOWN_TABLE_CELL */
	"LOWDOWN_FOOTNOTES_BLOCK",      /* LOWDOWN_FOOTNOTES_BLOCK */
	"LOWDOWN_FOOTNOTE_DEF",         /* LOWDOWN_FOOTNOTE_DEF */
	"LOWDOWN_BLOCKHTML",            /* LOWDOWN_BLOCKHTML */
	"LOWDOWN_LINK_AUTO",            /* LOWDOWN_LINK_AUTO */
	"LOWDOWN_CODESPAN",             /* LOWDOWN_CODESPAN */
	"LOWDOWN_DOUBLE_EMPHASIS",      /* LOWDOWN_DOUBLE_EMPHASIS */
	"LOWDOWN_EMPHASIS",             /* LOWDOWN_EMPHASIS */
	"LOWDOWN_HIGHLIGHT",            /* LOWDOWN_HIGHLIGHT */
	"LOWDOWN_IMAGE",                /* LOWDOWN_IMAGE */
	"LOWDOWN_LINEBREAK",            /* LOWDOWN_LINEBREAK */
	"LOWDOWN_LINK",                 /* LOWDOWN_LINK */
	"LOWDOWN_TRIPLE_EMPHASIS",      /* LOWDOWN_TRIPLE_EMPHASIS */
	"LOWDOWN_STRIKETHROUGH",        /* LOWDOWN_STRIKETHROUGH */
	"LOWDOWN_SUPERSCRIPT",          /* LOWDOWN_SUPERSCRIPT */
	"LOWDOWN_FOOTNOTE_REF",         /* LOWDOWN_FOOTNOTE_REF */
	"LOWDOWN_MATH_BLOCK",           /* LOWDOWN_MATH_BLOCK */
	"LOWDOWN_RAW_HTML",             /* LOWDOWN_RAW_HTML */
	"LOWDOWN_ENTITY",               /* LOWDOWN_ENTITY */
	"LOWDOWN_NORMAL_TEXT",          /* LOWDOWN_NORMAL_TEXT */
	"LOWDOWN_DOC_HEADER",           /* LOWDOWN_DOC_HEADER */
	"LOWDOWN_DOC_FOOTER",           /* LOWDOWN_DOC_FOOTER */
};

static void
MD5Updatebuf(MD5_CTX *ctx, const hbuf *v)
{

	MD5Update(ctx, v->data, v->size);
}

static void
MD5Updatev(MD5_CTX *ctx, const void *v, size_t sz)
{

	MD5Update(ctx, (const unsigned char *)v, sz);
}

/*
 * Assign signatures and weights.
 * This is defined by "Phase 2" in 5.2, Detailed description.
 * We use the MD5 algorithm for computing hashes; and for the time
 * being, use the linear case of weighing.
 * Returns the weight of the node rooted at "n".
 * If "parent" is not NULL, its hash is updated with the hash computed
 * for the current "n" and its children.
 */
static size_t
assign_sigs(MD5_CTX *parent, struct xmap *map, 
	const struct lowdown_node *n)
{
	const struct lowdown_node *nn;
	MD5_CTX		 ctx;
	struct xnode	*xn;

	/* Get our node slot. */

	if (n->id >= map->maxsize) {
		map->nodes = recallocarray
			(map->nodes, map->maxsize, 
			 map->maxsize + 64,
			 sizeof(struct xnode));
		if (NULL == map->nodes)
			err(EXIT_FAILURE, NULL);
		map->maxsize += 64;
	}

	xn = &map->nodes[n->id];
	assert(NULL == xn->node);
	xn->node = n;
	if (n->id > map->maxid)
		map->maxid = n->id;

	/* Recursive step. */

	MD5Init(&ctx);
	MD5Updatev(&ctx, &n->type, sizeof(enum lowdown_rndrt));

	TAILQ_FOREACH(nn, &n->children, entries)
		xn->weight += assign_sigs(&ctx, map, nn);

	/*
	 * Compute our weight.
	 * The weight is either the contained text length for leaf nodes
	 * or the accumulated sub-element weight for non-terminal nodes
	 * plus one.
	 */

	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
		assert(0 == xn->weight);
		xn->weight = n->rndr_blockcode.text.size;
		break;
	case LOWDOWN_BLOCKHTML:
		assert(0 == xn->weight);
		xn->weight = n->rndr_blockhtml.text.size;
		break;
	case LOWDOWN_LINK_AUTO:
		assert(0 == xn->weight);
		xn->weight = n->rndr_autolink.link.size;
		break;
	case LOWDOWN_CODESPAN:
		assert(0 == xn->weight);
		xn->weight = n->rndr_codespan.text.size;
		break;
	case LOWDOWN_IMAGE:
		assert(0 == xn->weight);
		xn->weight = n->rndr_image.link.size +
			n->rndr_image.title.size +
			n->rndr_image.dims.size +
			n->rndr_image.alt.size;
		break;
	case LOWDOWN_LINK:
		assert(0 == xn->weight);
		xn->weight = n->rndr_link.link.size +
			n->rndr_link.title.size;
		break;
	case LOWDOWN_RAW_HTML:
		assert(0 == xn->weight);
		xn->weight = n->rndr_raw_html.text.size;
		break;
	case LOWDOWN_NORMAL_TEXT:
		assert(0 == xn->weight);
		xn->weight = n->rndr_normal_text.text.size;
		break;
	case LOWDOWN_ENTITY:
		assert(0 == xn->weight);
		xn->weight = n->rndr_entity.text.size;
		break;
	default:
		xn->weight++;
		break;
	}

	/*
	 * Augment our signature from our attributes.
	 * This depends upon the node.
	 * Avoid using attributes that are "mutable" relative to the
	 * generated output, e.g., list display numbers.
	 */

	switch (n->type) {
	case LOWDOWN_LIST:
		MD5Updatev(&ctx, &n->rndr_list.flags, 
			sizeof(enum hlist_fl));
		break;
	case LOWDOWN_LISTITEM:
		MD5Updatev(&ctx, &n->rndr_listitem.flags, 
			sizeof(enum hlist_fl));
		MD5Updatev(&ctx, &n->rndr_listitem.num, 
			sizeof(size_t));
		break;
	case LOWDOWN_HEADER:
		MD5Updatev(&ctx, &n->rndr_header.level, 
			sizeof(size_t));
		break;
	case LOWDOWN_NORMAL_TEXT:
		MD5Updatebuf(&ctx, &n->rndr_normal_text.text);
		break;
	case LOWDOWN_ENTITY:
		MD5Updatebuf(&ctx, &n->rndr_entity.text);
		break;
	case LOWDOWN_LINK_AUTO:
		MD5Updatebuf(&ctx, &n->rndr_autolink.link);
		MD5Updatebuf(&ctx, &n->rndr_autolink.text);
		MD5Updatev(&ctx, &n->rndr_autolink.type, 
			sizeof(enum halink_type));
		break;
	case LOWDOWN_RAW_HTML:
		MD5Updatebuf(&ctx, &n->rndr_raw_html.text);
		break;
	case LOWDOWN_LINK:
		MD5Updatebuf(&ctx, &n->rndr_link.link);
		MD5Updatebuf(&ctx, &n->rndr_link.title);
		break;
	case LOWDOWN_BLOCKCODE:
		MD5Updatebuf(&ctx, &n->rndr_blockcode.text);
		MD5Updatebuf(&ctx, &n->rndr_blockcode.lang);
		break;
	case LOWDOWN_CODESPAN:
		MD5Updatebuf(&ctx, &n->rndr_codespan.text);
		break;
	case LOWDOWN_TABLE_HEADER:
		/* Don't use the column metrics: mutable. */
		break;
	case LOWDOWN_TABLE_CELL:
		MD5Updatev(&ctx, &n->rndr_table_cell.flags,
			sizeof(enum htbl_flags));
		/* Don't use the column number/count: mutable. */
		break;
	case LOWDOWN_FOOTNOTE_DEF:
	case LOWDOWN_FOOTNOTE_REF:
		/* Don't use footnote number: mutable. */
	case LOWDOWN_IMAGE:
		MD5Updatebuf(&ctx, &n->rndr_image.link);
		MD5Updatebuf(&ctx, &n->rndr_image.title);
		MD5Updatebuf(&ctx, &n->rndr_image.dims);
		MD5Updatebuf(&ctx, &n->rndr_image.alt);
		break;
	case LOWDOWN_MATH_BLOCK:
		MD5Updatev(&ctx, &n->rndr_math.displaymode, 
			sizeof(int));
		break;
	case LOWDOWN_BLOCKHTML:
		MD5Updatebuf(&ctx, &n->rndr_blockhtml.text);
		break;
	default:
		break;
	}

	MD5End(&ctx, xn->sig);

	if (NULL != parent)
		MD5Update(parent, xn->sig, 
			MD5_DIGEST_STRING_LENGTH - 1);

	return(xn->weight);
}

static void
diff_print_char(const hbuf *txt)
{
	size_t	 i, len;

	len = txt->size > 20 ? 20 : txt->size;

	for (i = 0; i < len; i++)
		if ('\n' == txt->data[i])
			fputs("\\n", stderr);
		else
			fputc(txt->data[i], stderr);
}

static void
diff_print(const struct lowdown_node *from,
	const struct xmap *xfrom, size_t tabs)
{
	const struct lowdown_node *nn;
	size_t	 i;

	for (i = 0; i < tabs; i++)
		fprintf(stderr, "  ");

	fprintf(stderr, "%zu: %s: %zu", from->id,
		names[from->type], 
		xfrom->nodes[from->id].weight);

	if (LOWDOWN_NORMAL_TEXT == from->type) {
		fprintf(stderr, ": ");
		diff_print_char(&from->rndr_normal_text.text);
	} 
	fprintf(stderr, "\n");

	TAILQ_FOREACH(nn, &from->children, entries)
		diff_print(nn, xfrom, tabs + 1);
}

/*
 * Enqueue "n" into a priority queue "pq".
 * Priority is given to weights; and if weights are equal, then
 * proximity to the parse root given by a pre-order identity.
 */
static void
pqueue(const struct lowdown_node *n, 
	struct xmap *map, struct pnodeq *pq)
{
	struct pnode	*p, *pp;
	struct xnode	*xnew, *xold;

	p = xmalloc(sizeof(struct pnode));
	p->node = n;

	xnew = &map->nodes[n->id];
	assert(NULL != xnew->node);

	TAILQ_FOREACH(pp, pq, entries) {
		xold = &map->nodes[pp->node->id];
		assert(NULL != xold->node);
		if (xnew->weight >= xold->weight)
			break;
	}
	if (NULL == pp) {
		TAILQ_INSERT_TAIL(pq, p, entries);
		return;
	} else if (xnew->weight > xold->weight) {
		TAILQ_INSERT_BEFORE(pp, p, entries);
		return;
	}

	for (; NULL != pp; pp = TAILQ_NEXT(pp, entries)) {
		assert(p->node->id != pp->node->id);
		if (p->node->id < pp->node->id)
			break;
	}
	if (NULL == pp) 
		TAILQ_INSERT_TAIL(pq, p, entries);
	else
		TAILQ_INSERT_BEFORE(pp, p, entries);
}

/*
 * Candidate optimality between "xnew" and "xold".
 * Climb the tree for a variable number of steps and see whether we've
 * already matched.
 */
static size_t
optimality(struct xnode *xnew, struct xmap *xnewmap,
	struct xnode *xold, struct xmap *xoldmap)
{
	size_t	 opt = 1, d = 2, i = 0;

	while (NULL != xnew->node->parent &&
	       NULL != xold->node->parent && i < d) {
		xnew = &xnewmap->nodes[xnew->node->parent->id];
		xold = &xoldmap->nodes[xold->node->parent->id];
		if (NULL != xnew->match && xnew->match == xold->node) 
			opt++;
		i++;
	}

	return(opt);
}

/*
 * Compute the candidacy of "xnew" to "xold".
 * If "xnew" does not have a match assigned (no prior candidacy), assign
 * it immediately to "xold".
 * If it does, then compute the optimality and select the greater of the
 * two optimalities.
 */
static void
candidate(struct xnode *xnew, struct xmap *xnewmap,
	struct xnode *xold, struct xmap *xoldmap)
{
	size_t	opt;

	assert(NULL != xnew->node);
	assert(NULL != xold->node);

	if (NULL == xnew->match) {
		xnew->match = xold->node;
		xnew->optimality = optimality
			(xnew, xnewmap, xold, xoldmap);
		return;
	}

	opt = optimality(xnew, xnewmap, xold, xoldmap);
	if (opt > xnew->optimality) {
		xnew->match = xold->node;
		xnew->optimality = opt;
	}
}

static void
match_up(struct xnode *xnew, struct xmap *xnewmap,
	struct xnode *xold, struct xmap *xoldmap)
{
	size_t	 d = 2, i = 0;

	while (NULL != xnew->node->parent &&
	       NULL != xold->node->parent && i < d) {
		/* Are the "labels" the same? */
		if (xnew->node->parent->type !=
		    xold->node->parent->type)
			break;
		xnew = &xnewmap->nodes[xnew->node->parent->id];
		xold = &xoldmap->nodes[xold->node->parent->id];
		xnew->match = xold->node;
		xold->match = xnew->node;
		i++;
	}
}

static void
match_down(struct xnode *xnew, struct xmap *xnewmap,
	struct xnode *xold, struct xmap *xoldmap)
{
	struct lowdown_node *nnew, *nold;

	xnew->match = xold->node;
	xold->match = xnew->node;

	nnew = TAILQ_FIRST(&xnew->node->children);
	nold = TAILQ_FIRST(&xold->node->children);

	while (NULL != nnew) {
		assert(NULL != nold);
		xnew = &xnewmap->nodes[nnew->id];
		xold = &xoldmap->nodes[nold->id];
		match_down(xnew, xnewmap, xold, xoldmap);
		nnew = TAILQ_NEXT(nnew, entries);
		nold = TAILQ_NEXT(nold, entries);
	}
}

static struct lowdown_node *
node_clone(const struct lowdown_node *v, size_t id)
{
	struct lowdown_node *n;

	n = xcalloc(1, sizeof(struct lowdown_node));
	TAILQ_INIT(&n->children);
	n->type = v->type;
	n->id = id;

	switch (n->type) {
	case LOWDOWN_LIST:
		n->rndr_list.flags = v->rndr_list.flags;
		break;
	case LOWDOWN_LISTITEM:
		n->rndr_listitem.flags = v->rndr_listitem.flags;
		n->rndr_listitem.num = v->rndr_listitem.num;
		break;
	case LOWDOWN_HEADER:
		n->rndr_header.level = v->rndr_header.level;
		break;
	case LOWDOWN_NORMAL_TEXT:
		hbuf_clone(&v->rndr_normal_text.text,
			&n->rndr_normal_text.text);
		break;
	case LOWDOWN_ENTITY:
		hbuf_clone(&v->rndr_entity.text,
			&n->rndr_entity.text);
		break;
	case LOWDOWN_LINK_AUTO:
		hbuf_clone(&v->rndr_autolink.link,
			&n->rndr_autolink.link);
		hbuf_clone(&v->rndr_autolink.text,
			&n->rndr_autolink.text);
		n->rndr_autolink.type = v->rndr_autolink.type;
		break;
	case LOWDOWN_RAW_HTML:
		hbuf_clone(&v->rndr_raw_html.text,
			&n->rndr_raw_html.text);
		break;
	case LOWDOWN_LINK:
		hbuf_clone(&v->rndr_link.link,
			&n->rndr_link.link);
		hbuf_clone(&v->rndr_link.title,
			&n->rndr_link.title);
		break;
	case LOWDOWN_BLOCKCODE:
		hbuf_clone(&v->rndr_blockcode.text,
			&n->rndr_blockcode.text);
		hbuf_clone(&v->rndr_blockcode.lang,
			&n->rndr_blockcode.lang);
		break;
	case LOWDOWN_CODESPAN:
		hbuf_clone(&v->rndr_codespan.text,
			&n->rndr_codespan.text);
		break;
	case LOWDOWN_TABLE_HEADER:
		/* Don't use the column metrics: mutable. */
		break;
	case LOWDOWN_TABLE_CELL:
		n->rndr_table_cell.flags = 
			v->rndr_table_cell.flags;
		/* Don't use the column number/count: mutable. */
		break;
	case LOWDOWN_FOOTNOTE_DEF:
	case LOWDOWN_FOOTNOTE_REF:
		/* Don't use footnote number: mutable. */
		break;
	case LOWDOWN_IMAGE:
		hbuf_clone(&v->rndr_image.link,
			&n->rndr_image.link);
		hbuf_clone(&v->rndr_image.title,
			&n->rndr_image.title);
		hbuf_clone(&v->rndr_image.dims,
			&n->rndr_image.dims);
		hbuf_clone(&v->rndr_image.alt,
			&n->rndr_image.alt);
		break;
	case LOWDOWN_MATH_BLOCK:
		n->rndr_math.displaymode = 
			v->rndr_math.displaymode;
		break;
	case LOWDOWN_BLOCKHTML:
		hbuf_clone(&v->rndr_blockhtml.text,
			&n->rndr_blockhtml.text);
		break;
	default:
		break;
	}

	return(n);
}

static struct lowdown_node *
node_clonetree(const struct lowdown_node *v, size_t *id)
{
	struct lowdown_node *n, *nn;
	const struct lowdown_node *vv;

	n = node_clone(v, *id++);

	TAILQ_FOREACH(vv, &v->children, entries) {
		nn = node_clonetree(vv, id);
		nn->parent = n;
		TAILQ_INSERT_TAIL(&n->children, nn, entries);
	}

	return(n);
}

/*
 * Merge the new tree "nnew" with the old tree "nold".
 * The produced tree will show the new tree with deleted nodes from the
 * old and added ones to the new.
 * It will also show moved nodes by delete/add pairs.
 */
static struct lowdown_node *
node_merge(const struct lowdown_node *nold,
	const struct xmap *xoldmap,
	const struct lowdown_node *nnew,
	const struct xmap *xnewmap,
	size_t *id)
{
	const struct xnode *xnew, *xold;
	struct lowdown_node *n, *nn;
	const struct lowdown_node *nnold;

	warnx("%s: %zu (%s)", __func__, nnew->id, names[nnew->type]);

	/* Basis: the current nodes are matched. */

	assert(NULL != nnew && NULL != nold);
	xnew = &xnewmap->nodes[nnew->id];
	xold = &xoldmap->nodes[nold->id];
	assert(xnew->match == xold->node);

	n = node_clone(nnew, *id++);

	/* Now walk through the children on both sides. */

	nold = TAILQ_FIRST(&nold->children);
	nnew = TAILQ_FIRST(&nnew->children);

	while (NULL != nnew) {
		/* 
		 * Begin by flushing out all of the nodes that have been
		 * deleted from the old tree at this level.
		 */

		while (NULL != nold) {
			xold = &xoldmap->nodes[nold->id];
			if (NULL != xold->match) 
				break;
			warnx("%s: flushing old %zu (%s)", 
				__func__, nold->id, 
				names[nold->type]);
			nn = node_clonetree(nold, id);
			nn->parent = n;
			nn->chng = LOWDOWN_CHNG_DELETE;
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nold = TAILQ_NEXT(nold, entries);
		}

		/* Now flush added nodes. */

		while (NULL != nnew) {
			/* FIXME: make an insertion. */
			xnew = &xnewmap->nodes[nnew->id];
			if (NULL != xnew->match)
				break;
			warnx("%s: flushing new %zu (%s)", 
				__func__, nnew->id, 
				names[nnew->type]);
			nn = node_clonetree(nnew, id);
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nn->parent = n;
			nn->chng = LOWDOWN_CHNG_INSERT;
			nnew = TAILQ_NEXT(nnew, entries);
		}

		/* 
		 * If there are no more in the new tree, then flush out
		 * all that's in the old tree as a deletion.
		 * We do this in our exit statement.
		 */

		if (NULL == nnew) {
			warnx("%s: nothing more at this level", __func__);
			break;
		}

		xnew = &xnewmap->nodes[nnew->id];
		assert(NULL != xnew->match);

		/* Scan ahead to match with an old. */
		
		for (nnold = nold; NULL != nnold ; ) {
			xold = &xoldmap->nodes[nnold->id];
			if (xnew->node == xold->match) 
				break;
			nnold = TAILQ_NEXT(nnold, entries);
		}

		/* 
		 * We did not find a match.
		 * Add the node and continue on.
		 */

		if (NULL == nnold) {
			warnx("%s: could not find match: %s",
				__func__, names[nnew->type]);
			nn = node_clonetree(nnew, id);
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nn->parent = n;
			nn->chng = LOWDOWN_CHNG_INSERT;
			nnew = TAILQ_NEXT(nnew, entries);
			continue;
		}

		/*
		 * We did find a match.
		 * First flush up until the match.
		 * Then, descend into the matched pair.
		 */

		while (NULL != nold) {
			xold = &xoldmap->nodes[nold->id];
			if (xnew->node == xold->match) 
				break;
			nn = node_clonetree(nold, id);
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nn->parent = n;
			nn->chng = LOWDOWN_CHNG_DELETE;
			nold = TAILQ_NEXT(nold, entries);
		}

		assert(NULL != nold);

		warnx("%s: match found: %s", 
			__func__, names[nnew->type]);

		nn = node_merge(nold, xoldmap, nnew, xnewmap, id);
		nn->parent = n;
		TAILQ_INSERT_TAIL(&n->children, nn, entries);

		nold = TAILQ_NEXT(nold, entries);
		nnew = TAILQ_NEXT(nnew, entries);
	}

	/* Flush remaining old nodes. */

	while (NULL != nold) {
		/* FIXME: make a deletion. */
		xold = &xoldmap->nodes[nold->id];
		nn = node_clonetree(nold, id);
		TAILQ_INSERT_TAIL(&n->children, nn, entries);
		nn->parent = n;
		nold = TAILQ_NEXT(nold, entries);
	}

	return(n);
}

/*
 * Algorithm: Detecting Changes in XML Documents.
 * Gregory Cobena, Serge Abiteboul, Amelie Marian.
 * https://www.cs.rutgers.edu/~amelie/papers/2002/diff.pdf
 */
struct lowdown_node *
lowdown_diff(const struct lowdown_node *nold,
	const struct lowdown_node *nnew)
{
	struct xmap	 xoldmap, xnewmap;
	struct xnode	*xnew, *xold;
	struct pnodeq	 pq;
	struct pnode	*p;
	const struct lowdown_node *n, *nn;
	struct lowdown_node *comp;
	size_t		 i;

	memset(&xoldmap, 0, sizeof(struct xmap));
	memset(&xnewmap, 0, sizeof(struct xmap));

	TAILQ_INIT(&pq);

	/* First, assign signatures and weights. */

	(void)assign_sigs(NULL, &xoldmap, nold);
	(void)assign_sigs(NULL, &xnewmap, nnew);

	/* XXX: debug. */

	fprintf(stderr, "--Old------------------------\n");
	diff_print(nold, &xoldmap, 0);
	fprintf(stderr, "--New------------------------\n");
	diff_print(nnew, &xnewmap, 0);

	/* Next, prime the priority queue. */

	pqueue(nnew, &xnewmap, &pq);

	/* 
	 * Match-make while we have nodes in the priority queue.
	 * This is guaranteed to be finite because with each step, we
	 * either add descendents or not.
	 */

	while (NULL != (p = TAILQ_FIRST(&pq))) {
		TAILQ_REMOVE(&pq, p, entries);
		n = p->node;
		free(p);
		xnew = &xnewmap.nodes[n->id];
		assert(NULL == xnew->match);
		assert(0 == xnew->optimality);
		for (i = 0; i < xoldmap.maxid; i++) {
			xold = &xoldmap.nodes[i];
			if (strcmp(xnew->sig, xold->sig))
				continue;
			/* Match found: test optimality. */

			candidate(xnew, &xnewmap, xold, &xoldmap);
			warnx("Candidate: %zu -> %zu (optimality %zu)",
				xnew->node->id,
				xold->node->id,
				xnew->optimality);
		}

		if (NULL != xnew->match) {
			warnx("Match: %zu -> %zu (optimality %zu)",
				xnew->node->id,
				xnew->match->id,
				xnew->optimality);

			/* Propogate match. */
			match_down(xnew, &xnewmap, 
				&xoldmap.nodes[xnew->match->id], 
				&xoldmap);
			match_up(xnew, &xnewmap, 
				&xoldmap.nodes[xnew->match->id], 
				&xoldmap);
			continue;
		}

		warnx("No match: %zu", xnew->node->id);

		/* No match found: recursive step. */
		TAILQ_FOREACH(nn, &n->children, entries)
			pqueue(nn, &xnewmap, &pq);
	}

	for (i = 0; i < xoldmap.maxid; i++) {
		if (NULL == xoldmap.nodes[i].node)
			continue;
		if (NULL == xoldmap.nodes[i].match)
			warnx("Deleted from old: %zu", 
				xoldmap.nodes[i].node->id);
	}

	for (i = 0; i < xnewmap.maxid; i++) {
		if (NULL == xnewmap.nodes[i].node)
			continue;
		if (NULL == xnewmap.nodes[i].match)
			warnx("Insert into new: %zu", 
				xnewmap.nodes[i].node->id);
	}

	i = 0;
	comp = node_merge(nold, &xoldmap, nnew, &xnewmap, &i);

	free(xoldmap.nodes);
	free(xnewmap.nodes);

	while (NULL != (p = TAILQ_FIRST(&pq))) {
		TAILQ_REMOVE(&pq, p, entries);
		free(p);
	}

	return(comp);
}
