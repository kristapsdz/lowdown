/*	$Id$ */
/*
 * Copyright (c) 2017, Kristaps Dzonsons
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
#ifndef LOWDOWN_H
#define LOWDOWN_H

/*
 * All of this is documented in lowdown.3.
 * If it's not documented, don't use it.
 * Or report it as a bug.
 */

/* We need this for compilation on musl systems. */

#ifndef __BEGIN_DECLS
# ifdef __cplusplus
#  define       __BEGIN_DECLS           extern "C" {
# else
#  define       __BEGIN_DECLS
# endif
#endif
#ifndef __END_DECLS
# ifdef __cplusplus
#  define       __END_DECLS             }
# else
#  define       __END_DECLS
# endif
#endif

enum	lowdown_type {
	LOWDOWN_HTML,
	LOWDOWN_MAN,
	LOWDOWN_NROFF
};

enum	lowdown_err {
	LOWDOWN_ERR_SPACE_BEFORE_LINK = 0,
	LOWDOWN_ERR_METADATA_BAD_CHAR,
	LOWDOWN_ERR__MAX
};

typedef	void (*lowdown_msg)(enum lowdown_err, void *, const char *);

/*
 * All types of Markdown nodes that lowdown understands.
 */
enum	lowdown_rndrt {
	LOWDOWN_ROOT,
	LOWDOWN_BLOCKCODE,
	LOWDOWN_BLOCKQUOTE,
	LOWDOWN_HEADER,
	LOWDOWN_HRULE,
	LOWDOWN_LIST,
	LOWDOWN_LISTITEM,
	LOWDOWN_PARAGRAPH,
	LOWDOWN_TABLE_BLOCK,
	LOWDOWN_TABLE_HEADER,
	LOWDOWN_TABLE_BODY,
	LOWDOWN_TABLE_ROW,
	LOWDOWN_TABLE_CELL,
	LOWDOWN_FOOTNOTES_BLOCK,
	LOWDOWN_FOOTNOTE_DEF,
	LOWDOWN_BLOCKHTML,
	LOWDOWN_LINK_AUTO,
	LOWDOWN_CODESPAN,
	LOWDOWN_DOUBLE_EMPHASIS,
	LOWDOWN_EMPHASIS,
	LOWDOWN_HIGHLIGHT,
	LOWDOWN_IMAGE,
	LOWDOWN_LINEBREAK,
	LOWDOWN_LINK,
	LOWDOWN_TRIPLE_EMPHASIS,
	LOWDOWN_STRIKETHROUGH,
	LOWDOWN_SUPERSCRIPT,
	LOWDOWN_FOOTNOTE_REF,
	LOWDOWN_MATH_BLOCK,
	LOWDOWN_RAW_HTML,
	LOWDOWN_ENTITY,
	LOWDOWN_NORMAL_TEXT,
	LOWDOWN_BACKSPACE,
	LOWDOWN_DOC_HEADER,
	LOWDOWN_DOC_FOOTER,
	LOWDOWN__MAX
};

typedef struct hbuf {
	uint8_t		*data;	/* actual character data */
	size_t		 size;	/* size of the string */
	size_t		 asize;	/* allocated size (0 = volatile) */
	size_t		 unit;	/* realloc unit size (0 = read-only) */
	int 		 buffer_free; /* obj should be freed */
} hbuf;

/*
 */
TAILQ_HEAD(lowdown_nodeq, lowdown_node);

/* XXX: remove */
typedef enum htbl_flags {
	HTBL_FL_ALIGN_LEFT = 1,
	HTBL_FL_ALIGN_RIGHT = 2,
	HTBL_FL_ALIGN_CENTER = 3,
	HTBL_FL_ALIGNMASK = 3,
	HTBL_FL_HEADER = 4
} htbl_flags;

/* XXX: un-typedef */
typedef enum halink_type {
	HALINK_NONE, /* used internally when it is not an autolink */
	HALINK_NORMAL, /* normal http/http/ftp/mailto/etc link */
	HALINK_EMAIL /* e-mail link without explit mailto: */
} halink_type;


/*
 * Node parsed from input document.
 * Each node is part of the parse tree.
 */
struct	lowdown_node {
	enum lowdown_rndrt	 type;
	union {
		struct rndr_list {
#define HLIST_ORDERED	0x01 /* ordered list */
#define HLIST_BLOCK	0x02 /* item has block data */
			int flags;
		} rndr_list; 
		struct rndr_listitem {
			int flags; /* see rndr_list */
			size_t num; /* index in ordered */
		} rndr_listitem; 
		struct rndr_header {
			size_t level; /* hN level */
		} rndr_header; 
		struct rndr_normal_text {
			hbuf text;
		} rndr_normal_text; 
		struct rndr_entity {
			hbuf text;
		} rndr_entity; 
		struct rndr_autolink {
			hbuf link;
			hbuf text;
			halink_type type;
		} rndr_autolink; 
		struct rndr_raw_html {
			hbuf text;
		} rndr_raw_html; 
		struct rndr_link {
			hbuf text;
			hbuf link;
			hbuf title;
		} rndr_link; 
		struct rndr_blockcode {
			hbuf text;
			hbuf lang;
		} rndr_blockcode; 
		struct rndr_codespan {
			hbuf text;
		} rndr_codespan; 
		struct rndr_table_header {
			htbl_flags *flags;
			size_t columns;
		} rndr_table_header; 
		struct rndr_table_cell {
			htbl_flags flags;
			size_t col;
			size_t columns;
		} rndr_table_cell; 
		struct rndr_footnote_def {
			size_t num;
		} rndr_footnote_def;
		struct rndr_footnote_ref {
			size_t num;
		} rndr_footnote_ref;
		struct rndr_image {
			hbuf link;
			hbuf title;
			hbuf dims;
			hbuf alt;
		} rndr_image;
		struct rndr_math {
			int displaymode;
		} rndr_math;
	};
	struct lowdown_node *parent;
	struct lowdown_nodeq children;
	TAILQ_ENTRY(lowdown_node) entries;
};

/*
 * A callback for rendering.
 */
typedef	void (*lowdown_rndr_fp)(hbuf *, const hbuf *,
		const struct lowdown_node *, void *);

struct	lowdown_rndr {
	lowdown_rndr_fp	 tab[LOWDOWN__MAX];
	void		*data;
};


struct	lowdown_meta {
	char		*key;
	char		*value;
};

struct	lowdown_opts {
	lowdown_msg	  msg;
	enum lowdown_type type;
	void		 *arg;
	unsigned int	  feat;
#define LOWDOWN_TABLES	  0x01
#define LOWDOWN_FENCED	  0x02
#define LOWDOWN_FOOTNOTES 0x04
#define LOWDOWN_AUTOLINK  0x08
#define LOWDOWN_STRIKE	  0x10
#define LOWDOWN_HILITE	  0x40
#define LOWDOWN_SUPER	  0x100
#define LOWDOWN_MATH	  0x200
#define LOWDOWN_NOINTEM	  0x400
#define LOWDOWN_SPHD	  0x800
#define LOWDOWN_MATHEXP	  0x1000
#define LOWDOWN_NOCODEIND 0x2000
#define	LOWDOWN_METADATA  0x4000
	unsigned int	  oflags;
#define LOWDOWN_HTML_SKIP_HTML	0x01
#define LOWDOWN_HTML_ESCAPE	0x02
#define LOWDOWN_HTML_HARD_WRAP	0x04
#define LOWDOWN_NROFF_SKIP_HTML	0x08
#define LOWDOWN_NROFF_HARD_WRAP	0x10
#define LOWDOWN_NROFF_GROFF	0x20
#define LOWDOWN_NROFF_NUMBERED	0x80
#define	LOWDOWN_SMARTY	  	0x40
};

__BEGIN_DECLS

void		 lowdown_buf(const struct lowdown_opts *, 
			const unsigned char *, size_t,
			unsigned char **, size_t *,
			struct lowdown_meta **, size_t *);
const char	*lowdown_errstr(enum lowdown_err);
int		 lowdown_file(const struct lowdown_opts *, 
			FILE *, unsigned char **, size_t *,
			struct lowdown_meta **, size_t *);
void		 lowdown_standalone_open
			(const struct lowdown_opts *,
			 const struct lowdown_meta *, size_t,
			 unsigned char **, size_t *);
void		 lowdown_standalone_close
			(const struct lowdown_opts *,
			 unsigned char **, size_t *);

__END_DECLS

#endif /* !EXTERN_H */
