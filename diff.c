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
#include <math.h>
#if HAVE_MD5
# include <md5.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

struct	xnode {
	char		 sig[MD5_DIGEST_STRING_LENGTH]; /* signature */
	double		 weight; /* priority queue weight */
	const struct lowdown_node *node; /* basis node */
	const struct lowdown_node *match; /* matching node */
	size_t		 optimality; /* optimality of match */
};

struct	xmap {
	struct xnode	*nodes; /* all of the nodes (dense table) */
	size_t		 maxid; /* maximum id in nodes */
	size_t		 maxsize; /* size of "nodes" (allocation) */
	double		 maxweight; /* maximum node weight */
};

struct	pnode {
	const struct lowdown_node *node; /* priority node */
	TAILQ_ENTRY(pnode) entries;
};

TAILQ_HEAD(pnodeq, pnode);

static void
MD5Updatebuf(MD5_CTX *ctx, const hbuf *v)
{

	MD5Update(ctx, (const u_int8_t *)v->data, v->size);
}

static void
MD5Updatev(MD5_CTX *ctx, const void *v, size_t sz)
{

	MD5Update(ctx, (const unsigned char *)v, sz);
}

/*
 * Assign signatures and weights.
 * This is defined by "Phase 2" in sec. 5.2., along with the specific
 * heuristics given in the "Tuning" section.
 * We use the MD5 algorithm for computing hashes.
 * Returns the weight of the node rooted at "n".
 * If "parent" is not NULL, its hash is updated with the hash computed
 * for the current "n" and its children.
 */
static double
assign_sigs(MD5_CTX *parent, struct xmap *map, 
	const struct lowdown_node *n)
{
	const struct lowdown_node *nn;
	size_t		 weight = 0;
	MD5_CTX		 ctx;
	struct xnode	*xn;

	/* Get our node slot. */

	if (n->id >= map->maxsize) {
		map->nodes = xrecallocarray
			(map->nodes, map->maxsize, 
			 map->maxsize + 64,
			 sizeof(struct xnode));
		map->maxsize += 64;
	}

	xn = &map->nodes[n->id];
	assert(NULL == xn->node);
	assert(0.0 == xn->weight);
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
	 * The weight is either the log of the contained text length for
	 * leaf nodes or the accumulated sub-element weight for
	 * non-terminal nodes plus one.
	 */

	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
		weight = n->rndr_blockcode.text.size;
		break;
	case LOWDOWN_BLOCKHTML:
		weight = n->rndr_blockhtml.text.size;
		break;
	case LOWDOWN_LINK_AUTO:
		weight = n->rndr_autolink.link.size;
		break;
	case LOWDOWN_CODESPAN:
		weight = n->rndr_codespan.text.size;
		break;
	case LOWDOWN_IMAGE:
		weight = n->rndr_image.link.size +
			n->rndr_image.title.size +
			n->rndr_image.dims.size +
			n->rndr_image.alt.size;
		break;
	case LOWDOWN_RAW_HTML:
		weight = n->rndr_raw_html.text.size;
		break;
	case LOWDOWN_NORMAL_TEXT:
		weight = n->rndr_normal_text.text.size;
		break;
	case LOWDOWN_ENTITY:
		weight = n->rndr_entity.text.size;
		break;
	default:
		break;
	}

	switch (n->type) {
	case LOWDOWN_BLOCKCODE:
	case LOWDOWN_BLOCKHTML:
	case LOWDOWN_LINK_AUTO:
	case LOWDOWN_CODESPAN:
	case LOWDOWN_IMAGE:
	case LOWDOWN_RAW_HTML:
	case LOWDOWN_NORMAL_TEXT:
	case LOWDOWN_ENTITY:
		assert(0.0 == xn->weight);
		xn->weight = 1.0 + log(weight);
		break;
	default:
		xn->weight += 1.0;
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
		MD5Update(parent, (u_int8_t *)xn->sig, 
			MD5_DIGEST_STRING_LENGTH - 1);

	if (xn->weight > map->maxweight)
		map->maxweight = xn->weight;

	return(xn->weight);
}

/*
 * Enqueue "n" into a priority queue "pq".
 * Priority is given to weights; and if weights are equal, then
 * proximity to the parse root given by a pre-order identity.
 * FIXME: use a priority heap.
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
 * Candidate optimality between "xnew" and "xold" as described in "Phase
 * 3" of sec. 5.2.
 * This also uses the heuristic described in "Tuning" for how many
 * levels to search upward.
 */
static size_t
optimality(struct xnode *xnew, struct xmap *xnewmap,
	struct xnode *xold, struct xmap *xoldmap)
{
	size_t	 opt = 1, d, i = 0;

	/* Height: log(n) * W/W_0 or at least 1. */

	d = ceil(log(xnewmap->maxid) * 
		xnew->weight / xnewmap->maxweight);

	if (0 == d)
		d = 1;

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
 * Compute the candidacy of "xnew" to "xold" as described in "Phase 3"
 * of sec. 5.2 and using the optimality() function as a basis.
 * If "xnew" does not have a match assigned (no prior candidacy), assign
 * it immediately to "xold".
 * If it does, then compute the optimality and select the greater of the
 * two optimalities.
 * As an extension to the paper, if the optimalities are equal, use the
 * "closer" node to the current identifier.
 */
static void
candidate(struct xnode *xnew, struct xmap *xnewmap,
	struct xnode *xold, struct xmap *xoldmap)
{
	size_t		 opt;
	long long	 dnew, dold;

	assert(NULL != xnew->node);
	assert(NULL != xold->node);

	if (NULL == xnew->match) {
		xnew->match = xold->node;
		xnew->optimality = optimality
			(xnew, xnewmap, xold, xoldmap);
		return;
	}

	opt = optimality(xnew, xnewmap, xold, xoldmap);

	if (opt == xnew->optimality) {
		/*
		 * Use a simple norm over the identifier space.
		 * Choose the lesser of the norms.
		 */
		dold = llabs(xnew->match->id - xnew->node->id);
		dnew = llabs(xold->node->id - xnew->node->id);
		if (dold > dnew) {
			xnew->match = xold->node;
			xnew->optimality = opt;
		}
	} else if (opt > xnew->optimality) {
		xnew->match = xold->node;
		xnew->optimality = opt;
	}
}

static int
match_eq(const struct lowdown_node *n1, 
	const struct lowdown_node *n2)
{

	if (n1->type != n2->type)
		return(0);

	if (LOWDOWN_LINK == n1->type) {
		/*
		 * Links have both contained nodes (for the alt text,
		 * which can be nested) and also attributes.
		 */
		if ( ! hbuf_eq(&n1->rndr_link.link,
			       &n2->rndr_link.link))
			return(0);
		if ( ! hbuf_eq(&n1->rndr_link.title,
			       &n2->rndr_link.title))
			return(0);
	}

	return(1);
}

/*
 * Algorithm to "propogate up" according to "Phase 4" of sec. 5.2.
 * This also uses the heuristic described in "Tuning" for how many
 * levels to search upward.
 */
static void
match_up(struct xnode *xnew, struct xmap *xnewmap,
	struct xnode *xold, struct xmap *xoldmap)
{
	size_t	 d, i = 0;

	/* Height: log(n) * W/W_0 or at least 1. */

	d = ceil(log(xnewmap->maxid) * 
		xnew->weight / xnewmap->maxweight);
	if (0 == d)
		d = 1;

	while (NULL != xnew->node->parent &&
	       NULL != xold->node->parent && i < d) {
		/* Are the "labels" the same? */
		/* 
		 * FIXME: for some labels (e.g., links), this is not
		 * sufficient: we also need to check equality. 
		 */
		if ( ! match_eq
		    (xnew->node->parent, xold->node->parent))
			break;
		xnew = &xnewmap->nodes[xnew->node->parent->id];
		xold = &xoldmap->nodes[xold->node->parent->id];
		xnew->match = xold->node;
		xold->match = xnew->node;
		i++;
	}
}

/*
 * Algorithm that "propogates down" according to "Phase 4" of sec. 5.2.
 * This (recursively) makes sure that a matched tree has all of the
 * subtree nodes also matched.
 */
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

/*
 * Clone a single node and all of its "attributes".
 * That is, its type and "leaf node" data.
 * Assign the identifier as given.
 * Note that some attributes, such as the table column array, aren't
 * copied.
 * We'll re-create those later.
 */
static struct lowdown_node *
node_clone(const struct lowdown_node *v, size_t id)
{
	struct lowdown_node *n;
	size_t		 i;

	n = xcalloc(1, sizeof(struct lowdown_node));
	TAILQ_INIT(&n->children);
	n->type = v->type;
	n->id = id;

	switch (n->type) {
	case LOWDOWN_DOC_HEADER:
		n->rndr_doc_header.msz =
			v->rndr_doc_header.msz;
		if (0 == n->rndr_doc_header.msz)
			break;
		n->rndr_doc_header.m = xcalloc
			(v->rndr_doc_header.msz,
			 sizeof(struct lowdown_meta));
		for (i = 0; i < n->rndr_doc_header.msz; i++) {
			n->rndr_doc_header.m[i].key = xstrdup
				(v->rndr_doc_header.m[i].key);
			n->rndr_doc_header.m[i].value = xstrdup
				(v->rndr_doc_header.m[i].value);
		}
		break;
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

/*
 * Take the sub-tree "v" and clone it and all of the nodes beneath it,
 * returning the cloned node.
 * This starts using identifiers at "id".
 */
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
 * Merge the new tree "nnew" with the old "nold" using a depth-first
 * algorithm.
 * The produced tree will show the new tree with deleted nodes from the
 * old and inserted ones.
 * It will also show moved nodes by delete/add pairs.
 * This uses "Phase 5" semantics, but implements the merge algorithm
 * without notes from the paper.
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

	/* 
	 * Invariant: the current nodes are matched.
	 * Start by putting that node into the current output.
	 */

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
		 * According to the paper, deleted nodes have no match.
		 * These will leave us with old nodes that are in the
		 * new tree (not necessarily at this level, though).
		 */

		while (NULL != nold) {
			xold = &xoldmap->nodes[nold->id];
			if (NULL != xold->match) 
				break;
			nn = node_clonetree(nold, id);
			nn->parent = n;
			nn->chng = LOWDOWN_CHNG_DELETE;
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nold = TAILQ_NEXT(nold, entries);
		}

		/* 
		 * Now flush inserted nodes.
		 * According to the paper, these have no match.
		 * This leaves us with nodes that are matched somewhere
		 * (not necessarily at this level) with the old.
		 */

		while (NULL != nnew) {
			xnew = &xnewmap->nodes[nnew->id];
			if (NULL != xnew->match)
				break;
			nn = node_clonetree(nnew, id);
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nn->parent = n;
			nn->chng = LOWDOWN_CHNG_INSERT;
			nnew = TAILQ_NEXT(nnew, entries);
		}

		/* Nothing more to do at this level? */

		if (NULL == nnew)
			break;

		/*
		 * Now we take the current new node and see if it's a
		 * match with a node in the current level.
		 * If it is, then we can flush out old nodes (moved,
		 * which we call deleted and re-inserted) until we get
		 * to the matching one.
		 * Then we'll be in lock-step with the old tree.
		 */

		xnew = &xnewmap->nodes[nnew->id];
		assert(NULL != xnew->match);

		/* Scan ahead to find a matching old. */
		
		for (nnold = nold; NULL != nnold ; ) {
			xold = &xoldmap->nodes[nnold->id];
			if (xnew->node == xold->match) 
				break;
			nnold = TAILQ_NEXT(nnold, entries);
		}

		/* 
		 * We did not find a match.
		 * This means that the new node has been moved from
		 * somewhere else in the tree.
		 */

		if (NULL == nnold) {
			nn = node_clonetree(nnew, id);
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nn->parent = n;
			nn->chng = LOWDOWN_CHNG_INSERT;
			nnew = TAILQ_NEXT(nnew, entries);
			continue;
		}

		/* Match found: flush old nodes til the match. */

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

		/*
		 * Now we're in lock-step.
		 * Do the recursive step between the matched pair.
		 * Then continue on to the next nodes.
		 */

		nn = node_merge(nold, xoldmap, nnew, xnewmap, id);
		nn->parent = n;
		TAILQ_INSERT_TAIL(&n->children, nn, entries);

		nold = TAILQ_NEXT(nold, entries);
		nnew = TAILQ_NEXT(nnew, entries);
	}

	/* Flush remaining old nodes. */

	while (NULL != nold) {
		nn = node_clonetree(nold, id);
		TAILQ_INSERT_TAIL(&n->children, nn, entries);
		nn->parent = n;
		nn->chng = LOWDOWN_CHNG_DELETE;
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

	/* 
	 * First, assign signatures and weights.
	 * See "Phase 2", sec 5.2.
	 */

	(void)assign_sigs(NULL, &xoldmap, nold);
	(void)assign_sigs(NULL, &xnewmap, nnew);

	/* Prime the priority queue with the root. */

	pqueue(nnew, &xnewmap, &pq);

	/* 
	 * Match-make while we have nodes in the priority queue.
	 * This is guaranteed to be finite.
	 * See "Phase 3" and "Phase 4", sec 5.2.
	 */

	while (NULL != (p = TAILQ_FIRST(&pq))) {
		/* TODO: cache of pnodes. */
		TAILQ_REMOVE(&pq, p, entries);
		n = p->node;
		free(p);

		xnew = &xnewmap.nodes[n->id];
		assert(NULL == xnew->match);
		assert(0 == xnew->optimality);

		/*
		 * Look for candidates: if we have a matching signature,
		 * test for optimality.
		 * Highest optimality gets to be matched.
		 * See "Phase 3", sec. 5.2.
		 */

		for (i = 0; i < xoldmap.maxid + 1; i++) {
			xold = &xoldmap.nodes[i];
			if (strcmp(xnew->sig, xold->sig))
				continue;
			candidate(xnew, &xnewmap, xold, &xoldmap);
		}

		/* No match: enqueue children ("Phase 3" cont.). */

		if (NULL == xnew->match) {
			TAILQ_FOREACH(nn, &n->children, entries)
				pqueue(nn, &xnewmap, &pq);
			continue;
		}

		/*
		 * Match found and is optimal.
		 * Now optimise using the bottom-up and top-down
		 * (doesn't matter which order) algorithms.
		 * See "Phase 4", sec. 5.2.
		 */

		match_down(xnew, &xnewmap, 
			&xoldmap.nodes[xnew->match->id], &xoldmap);
		match_up(xnew, &xnewmap, 
			&xoldmap.nodes[xnew->match->id], &xoldmap);
	}

	/*
	 * All nodes have been processed.
	 * Now we need to compute the delta and merge the trees.
	 * See "Phase 5", sec. 5.2.
	 */

	i = 0;
	comp = node_merge(nold, &xoldmap, nnew, &xnewmap, &i);

	/* Clean up and exit. */

	assert(TAILQ_EMPTY(&pq));
	free(xoldmap.nodes);
	free(xnewmap.nodes);
	return(comp);
}
