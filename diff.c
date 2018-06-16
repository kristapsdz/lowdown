/*	$Id$ */
/*
 * Copyright (c) 2017, 2018 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <ctype.h>
#include <float.h>
#include <math.h>
#if HAVE_MD5
# include <md5.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <diff.h>

#include "lowdown.h"
#include "extern.h"

#define	DEBUG 0

struct	xnode {
	char		 sig[MD5_DIGEST_STRING_LENGTH]; /* signature */
	double		 weight; /* priority queue weight */
	const struct lowdown_node *node; /* basis node */
	const struct lowdown_node *match; /* matching node */
	size_t		 opt; /* optimality of match */
	const struct lowdown_node *optmatch; /* current optimal match */
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

/*
 * A node used in computing the shortest edit script.
 */
struct	sesnode {
	char		*buf; /* buffer */
	size_t		 bufsz; /* length of buffer (less NUL) */
	int		 tailsp; /* whether there's trailing space */
	int		 headsp; /* whether there's leading space */
};

#if DEBUG
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
#endif

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
	double		 v;
	struct xnode	*xn;

	/* Get our node slot. */

	if (n->id >= map->maxsize) {
		map->nodes = xrecallocarray
			(map->nodes, 
			 map->maxsize, n->id + 64,
			 sizeof(struct xnode));
		map->maxsize = n->id + 64;
	}

	assert(n->id < map->maxsize);
	xn = &map->nodes[n->id];
	memset(xn, 0, sizeof(struct xnode));
	assert(NULL == xn->node);
	assert(0.0 == xn->weight);
	xn->node = n;
	if (n->id > map->maxid)
		map->maxid = n->id;

	/* Recursive step. */

	MD5Init(&ctx);
	MD5Updatev(&ctx, &n->type, sizeof(enum lowdown_rndrt));

	v = 0.0;
	TAILQ_FOREACH(nn, &n->children, entries)
		v += assign_sigs(&ctx, map, nn);

	/* Re-assign "xn": child might have reallocated. */

	xn = &map->nodes[n->id];
	xn->weight = v;

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
	
	/* FIXME: are we supposed to bound to "d"? */

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

	if (NULL == xnew->optmatch) {
		xnew->optmatch = xold->node;
		xnew->opt = optimality
			(xnew, xnewmap, xold, xoldmap);
		return;
	}

	opt = optimality(xnew, xnewmap, xold, xoldmap);

	if (opt == xnew->opt) {
		/*
		 * Use a simple norm over the identifier space.
		 * Choose the lesser of the norms.
		 */
		dold = llabs((long long)
			(xnew->optmatch->id - xnew->node->id));
		dnew = llabs((long long)
			(xold->node->id - xnew->node->id));
		if (dold > dnew) {
			xnew->optmatch = xold->node;
			xnew->opt = opt;
		}
	} else if (opt > xnew->opt) {
		xnew->optmatch = xold->node;
		xnew->opt = opt;
	} 
}

/*
 * Do the two internal nodes equal each other?
 * This depends upon the node type.
 * By default, all similarly-labelled (typed) nodes are equal.
 * We special-case as noted.
 */
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
 * Return non-zero if this node is the only child.
 */
static int
match_singleton(const struct lowdown_node *n)
{

	if (NULL == n->parent)
		return(1);

	return(TAILQ_NEXT(n, entries) == 
	       TAILQ_PREV(n, lowdown_nodeq, entries));
}

/*
 * Algorithm to "propogate up" according to "Phase 3" of sec. 5.2.
 * This also uses the heuristic described in "Tuning" for how many
 * levels to search upward.
 * I augment this by making singleton children pass upward.
 * FIXME: right now, this doesn't clobber existing upward matches.  Is
 * that correct behaviour?
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
		if ( ! match_eq
		    (xnew->node->parent, xold->node->parent))
			break;
		xnew = &xnewmap->nodes[xnew->node->parent->id];
		xold = &xoldmap->nodes[xold->node->parent->id];
		if (NULL != xold->match || NULL != xnew->match)
			break;
		xnew->match = xold->node;
		xold->match = xnew->node;
		i++;
	}

	if (i != d)
		return;

	/* 
	 * Pass up singletons.
	 * This is an extension of the algorithm.
	 */

	while (NULL != xnew->node->parent &&
	       NULL != xold->node->parent) {
		if ( ! match_singleton(xnew->node) ||
		     ! match_singleton(xold->node))
			break;
		if ( ! match_eq
		    (xnew->node->parent, xold->node->parent))
			break;
		xnew = &xnewmap->nodes[xnew->node->parent->id];
		xold = &xoldmap->nodes[xold->node->parent->id];
		if (NULL != xold->match || NULL != xnew->match)
			break;
		xnew->match = xold->node;
		xold->match = xnew->node;
	}
}

/*
 * Algorithm that "propogates down" according to "Phase 3" of sec. 5.2.
 * This (recursively) makes sure that a matched tree has all of the
 * subtree nodes also matched.
 */
static void
match_down(struct xnode *xnew, struct xmap *xnewmap,
	struct xnode *xold, struct xmap *xoldmap)
{
	struct lowdown_node *nnew, *nold;

	assert(NULL == xnew->match);
	assert(NULL == xold->match);

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
 * Count the number of words in a normal-text node.
 */
static size_t
node_countwords(const struct lowdown_node *n)
{
	const char	*cp;
	size_t		 i = 0, sz, words = 0;

	cp = n->rndr_normal_text.text.data;
	sz = n->rndr_normal_text.text.size;

	/* Skip leading space. */

	while (i < sz &&
	       isspace((unsigned char)cp[i]))
		i++;

	/* First go through word, then trailing space. */

	while (i < sz) {
		assert( ! isspace((unsigned char)cp[i]));
		words++;
		while (i < sz &&
		       ! isspace((unsigned char)cp[i]))
			i++;
		while (i < sz && 
		       isspace((unsigned char)cp[i]))
			i++;
	}

	return words;
}

/*
 * Like node_countwords(), except dupping individual words into a
 * structure.
 */
static void
node_tokenise(const struct lowdown_node *n, 
	struct sesnode *toks, size_t toksz, char **savep)
{
	char	*cp;
	size_t	 i = 0, sz, words = 0;

	*savep = NULL;

	if (0 == toksz)
		return;

	sz = n->rndr_normal_text.text.size;
	cp = xstrndup(n->rndr_normal_text.text.data, sz);

	*savep = cp;

	/* Skip leading space. */

	if (i < sz)
		toks[0].headsp = isspace((unsigned char)cp[0]);

	while (i < sz &&
	       isspace((unsigned char)cp[i]))
		i++;

	while (i < sz) {
		assert(words < toksz);
		assert( ! isspace((unsigned char)cp[i]));
		toks[words].buf = &cp[i];
		toks[words].bufsz = 0;
		while (i < sz &&
		       ! isspace((unsigned char)cp[i])) {
			toks[words].bufsz++;
			i++;
		}
		words++;
		if (i == sz)
			break;
		toks[words - 1].tailsp = 1;
		assert(isspace((unsigned char)cp[i]));
		cp[i++] = '\0';
		while (i < sz && 
		       isspace((unsigned char)cp[i]))
			i++;
	}
}

static int
node_word_cmp(const void *p1, const void *p2)
{
	const struct sesnode *l1 = p1, *l2 = p2;

	if (l1->bufsz != l2->bufsz)
		return 0;
	return 0 == strncmp(l1->buf, l2->buf, l1->bufsz);
}

static void
node_lcs(const struct lowdown_node *nold,
	const struct lowdown_node *nnew,
	struct lowdown_node *n, size_t *id)
{
	const struct sesnode *tmp;
	struct lowdown_node *nn;
	struct sesnode	*newtok, *oldtok;
	char		*newtokbuf, *oldtokbuf;
	size_t		 i, newtoksz, oldtoksz;
	struct diff	 d;
	int		 rc;

	newtoksz = node_countwords(nnew);
	oldtoksz = node_countwords(nold);

	newtok = xcalloc(newtoksz, sizeof(struct sesnode));
	oldtok = xcalloc(oldtoksz, sizeof(struct sesnode));

	node_tokenise(nnew, newtok, newtoksz, &newtokbuf);
	node_tokenise(nold, oldtok, oldtoksz, &oldtokbuf);

	rc = diff(&d, node_word_cmp, sizeof(struct sesnode), 
		oldtok, oldtoksz, newtok, newtoksz);

	for (i = 0; i < d.sessz; i++) {
		tmp = d.ses[i].e;

		if (tmp->headsp) {
			nn = xcalloc(1, sizeof(struct lowdown_node));
			TAILQ_INIT(&nn->children);
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nn->type = LOWDOWN_NORMAL_TEXT;
			nn->id = (*id)++;
			nn->parent = n;
			nn->rndr_normal_text.text.size = 1;
			nn->rndr_normal_text.text.data = xstrdup(" ");
		}

		nn = xcalloc(1, sizeof(struct lowdown_node));
		TAILQ_INIT(&nn->children);
		TAILQ_INSERT_TAIL(&n->children, nn, entries);
		nn->type = LOWDOWN_NORMAL_TEXT;
		nn->id = (*id)++;
		nn->parent = n;
		nn->rndr_normal_text.text.size = tmp->bufsz;
		nn->rndr_normal_text.text.data = 
			xcalloc(1, tmp->bufsz + 1);
		memcpy(nn->rndr_normal_text.text.data,
			tmp->buf, tmp->bufsz);
		nn->chng = DIFF_DELETE == d.ses[i].type ?
			LOWDOWN_CHNG_DELETE :
			DIFF_ADD == d.ses[i].type ?
			LOWDOWN_CHNG_INSERT :
			LOWDOWN_CHNG_NONE;

		if (tmp->tailsp) {
			nn = xcalloc(1, sizeof(struct lowdown_node));
			TAILQ_INIT(&nn->children);
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nn->type = LOWDOWN_NORMAL_TEXT;
			nn->id = (*id)++;
			nn->parent = n;
			nn->rndr_normal_text.text.size = 1;
			nn->rndr_normal_text.text.data = xstrdup(" ");
		}
	}

	free(d.ses);
	free(d.lcs);
	free(newtok);
	free(oldtok);
	free(newtokbuf);
	free(oldtokbuf);
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
	assert(NULL != xnew->match);
	assert(NULL != xold->match);

	assert(xnew->match == xold->node);

	n = node_clone(nnew, (*id)++);

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
			if (NULL != xold->match ||
			    LOWDOWN_NORMAL_TEXT == nold->type)
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
			if (NULL != xnew->match ||
			    LOWDOWN_NORMAL_TEXT == nnew->type)
				break;
			nn = node_clonetree(nnew, id);
			TAILQ_INSERT_TAIL(&n->children, nn, entries);
			nn->parent = n;
			nn->chng = LOWDOWN_CHNG_INSERT;
			nnew = TAILQ_NEXT(nnew, entries);
		}

		/*
		 * If both nodes are text nodes, then we want to run the
		 * LCS algorithm on them.
		 * This is an extension of the BULD algorithm.
		 */

		if (NULL != nold && NULL != nnew &&
		    LOWDOWN_NORMAL_TEXT == nold->type &&
		    NULL == xold->match &&
		    LOWDOWN_NORMAL_TEXT == nnew->type &&
		    NULL == xnew->match) {
			node_lcs(nold, nnew, n, id);
			nold = TAILQ_NEXT(nold, entries);
			nnew = TAILQ_NEXT(nnew, entries);
		}

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

	return n;
}

#if DEBUG
static void
node_print(const struct lowdown_node *n, const struct xmap *map, size_t ind)
{
	const struct lowdown_node *nn;
	const struct xnode *xn;
	size_t	 i;

	xn = &map->nodes[n->id];
	for (i = 0; i < ind; i++)
		fputc(' ', stderr);

	fprintf(stderr, "%zu:%s (%g)", n->id,names[n->type], xn->weight);
	if (xn->match) {
		fprintf(stderr, " -> %zu", xn->match->id);
	}

	fputc('\n', stderr);


	TAILQ_FOREACH(nn, &n->children, entries)
		node_print(nn, map, ind + 1);
}
#endif

/*
 * Optimise from top down.
 */
static void
node_optimise_topdown(const struct lowdown_node *n, 
	struct xmap *newmap, struct xmap *oldmap)
{
	struct xnode *xn, *xmatch, *xnchild, *xmchild;
	const struct lowdown_node *match, *nchild, *mchild;

	xn = &newmap->nodes[n->id];
	assert(NULL != xn);
	assert(NULL != xn->match);

	match = xn->match;
	xmatch = &oldmap->nodes[match->id];
	assert(NULL != xmatch);

	TAILQ_FOREACH(nchild, &n->children, entries) {
		/* Only process "inner" nodes. */
		if (TAILQ_EMPTY(&nchild->children))
			continue;
		xnchild = &newmap->nodes[nchild->id];
		assert(NULL != xnchild);
		if (NULL != xnchild->match)
			continue;
		TAILQ_FOREACH(mchild, &match->children, entries) {
			/* Only process "inner" nodes. */
			if (TAILQ_EMPTY(&mchild->children))
				continue;
			xmchild = &oldmap->nodes[mchild->id];
			assert(NULL != xmchild);
			if (NULL != xmchild->match)
				continue;
			if ( ! match_eq(nchild, mchild))
				continue;
			xnchild->match = mchild;
			xmchild->match = nchild;
			break;
		}
		if (NULL == mchild)
			continue;
		node_optimise_topdown(nchild, newmap, oldmap);
	}
}

/*
 * Optimise bottom-up over all un-matched nodes: examine all the
 * children of the un-matched nodes and see which of their matches, if
 * found, are under a root that's the same node as we are.
 * This lets us compute the largest fraction of un-matched nodes'
 * children that are in the same tree.
 * If that fraction is >50%, then we consider that the subtrees are
 * matched.
 */
static void
node_optimise_bottomup(const struct lowdown_node *n, 
	struct xmap *newmap, struct xmap *oldmap)
{
	const struct lowdown_node *nn, *on, *nnn, *maxn = NULL;
	double		w, maxw = 0.0, tw = 0.0;

	/* Do a depth-first pre-order search. */

	if (TAILQ_EMPTY(&n->children))
		return;

	TAILQ_FOREACH(nn, &n->children, entries) {
		tw += newmap->nodes[nn->id].weight;
		node_optimise_bottomup(nn, newmap, oldmap);
	}

	/*
	 * We're now at a non-leaf node.
	 * If we're already matched, then move on.
	 */

	if (NULL != newmap->nodes[n->id].match)
		return;

	TAILQ_FOREACH(nn, &n->children, entries) {
		if (NULL == newmap->nodes[nn->id].match)
			continue;
		if (NULL == (on = newmap->nodes[nn->id].match->parent))
			continue;
		if (on == maxn)
			continue;
		if ( ! match_eq(n, on))
			continue;
		
		/*
		 * We've now established "on" as the parent of the
		 * matched node, and that "on" is equivalent.
		 * See what fraction of on's children are matched to our
		 * children.
		 * FIXME: this will harmlessly (except in time) look at
		 * the same parent multiple times.
		 */

		w = 0.0;
		TAILQ_FOREACH(nnn, &n->children, entries) {
			if (NULL == newmap->nodes[nnn->id].match)
				continue;
			if (on != newmap->nodes[nnn->id].match->parent)
				continue;
			w += newmap->nodes[nnn->id].weight;
		}

		/* Is this the highest fraction? */

		if (w > maxw) {
			maxw = w;
			maxn = on;
		}
	}

	/* See if we found any similar sub-trees. */

	if (NULL == maxn)
		return;

	/*
	 * Our magic breakpoint is 50%.
	 * If the matched sub-tree has a greater than 50% match by
	 * weight, then set us as a match!
	 */

	if (maxw / tw >= 0.5) {
		newmap->nodes[n->id].match = maxn;
		oldmap->nodes[maxn->id].match = n;
	}
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
	 * See "Phase 3", sec 5.2.
	 */

	while (NULL != (p = TAILQ_FIRST(&pq))) {
		/* TODO: cache of pnodes. */
		TAILQ_REMOVE(&pq, p, entries);
		n = p->node;
		free(p);

		xnew = &xnewmap.nodes[n->id];
		assert(NULL == xnew->match);
		assert(NULL == xnew->optmatch);
		assert(0 == xnew->opt);

		/*
		 * Look for candidates: if we have a matching signature,
		 * test for optimality.
		 * Highest optimality gets to be matched.
		 * See "Phase 3", sec. 5.2.
		 */

		for (i = 0; i < xoldmap.maxid + 1; i++) {
			xold = &xoldmap.nodes[i];
			if (NULL == xold->node)
				continue;
			if (NULL != xold->match)
				continue;
			if (strcmp(xnew->sig, xold->sig))
				continue;

			assert(NULL == xold->match);
			candidate(xnew, &xnewmap, xold, &xoldmap);
		}

		/* No match: enqueue children ("Phase 3" cont.). */

		if (NULL == xnew->optmatch) {
			TAILQ_FOREACH(nn, &n->children, entries)
				pqueue(nn, &xnewmap, &pq);
			continue;
		}

		/*
		 * Match found and is optimal.
		 * Now use the bottom-up and top-down (doesn't matter
		 * which order) algorithms.
		 * See "Phase 3", sec. 5.2.
		 */

		assert(NULL == xnew->match);
		assert(NULL == xoldmap.nodes[xnew->optmatch->id].match);

		match_down(xnew, &xnewmap, 
			&xoldmap.nodes[xnew->optmatch->id], &xoldmap);
		match_up(xnew, &xnewmap, 
			&xoldmap.nodes[xnew->optmatch->id], &xoldmap);
	}

	/*
	 * All nodes have been processed.
	 * Now we need to optimise, so run a "Phase 4", sec. 5.2.
	 * Our optimisation is nothing like the paper's.
	 */

	node_optimise_topdown(nnew, &xnewmap, &xoldmap);
	node_optimise_bottomup(nnew, &xnewmap, &xoldmap);

#if DEBUG
	node_print(nnew, &xnewmap, 0);
	node_print(nold, &xoldmap, 0);
#endif

	/*
	 * The tree is optimal.
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
