/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016, Kristaps Dzonsons
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
#ifndef EXTERN_H
#define EXTERN_H

/*
 * We need this for compilation on musl systems.
 */

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

typedef struct hbuf {
	uint8_t		*data;	/* actual character data */
	size_t		 size;	/* size of the string */
	size_t		 asize;	/* allocated size (0 = volatile) */
	size_t		 unit;	/* realloc unit size (0 = read-only) */
	int 		 buffer_free; /* obj should be freed */
} hbuf;

typedef enum hoedown_autolink_flags {
	HOEDOWN_AUTOLINK_SHORT_DOMAINS = (1 << 0)
} hoedown_autolink_flags;

typedef struct hoedown_stack {
	void **item;
	size_t size;
	size_t asize;
} hoedown_stack;

typedef enum hoedown_extensions {
	/* block-level extensions */
	HOEDOWN_EXT_TABLES = (1 << 0),
	HOEDOWN_EXT_FENCED_CODE = (1 << 1),
	HOEDOWN_EXT_FOOTNOTES = (1 << 2),

	/* span-level extensions */
	HOEDOWN_EXT_AUTOLINK = (1 << 3),
	HOEDOWN_EXT_STRIKETHROUGH = (1 << 4),
	HOEDOWN_EXT_UNDERLINE = (1 << 5),
	HOEDOWN_EXT_HIGHLIGHT = (1 << 6),
	HOEDOWN_EXT_QUOTE = (1 << 7),
	HOEDOWN_EXT_SUPERSCRIPT = (1 << 8),
	HOEDOWN_EXT_MATH = (1 << 9),

	/* other flags */
	HOEDOWN_EXT_NO_INTRA_EMPHASIS = (1 << 11),
	HOEDOWN_EXT_SPACE_HEADERS = (1 << 12),
	HOEDOWN_EXT_MATH_EXPLICIT = (1 << 13),

	/* negative flags */
	HOEDOWN_EXT_DISABLE_INDENTED_CODE = (1 << 14)
} hoedown_extensions;

#define HOEDOWN_EXT_BLOCK (\
	HOEDOWN_EXT_TABLES |\
	HOEDOWN_EXT_FENCED_CODE |\
	HOEDOWN_EXT_FOOTNOTES )

#define HOEDOWN_EXT_SPAN (\
	HOEDOWN_EXT_AUTOLINK |\
	HOEDOWN_EXT_STRIKETHROUGH |\
	HOEDOWN_EXT_UNDERLINE |\
	HOEDOWN_EXT_HIGHLIGHT |\
	HOEDOWN_EXT_QUOTE |\
	HOEDOWN_EXT_SUPERSCRIPT |\
	HOEDOWN_EXT_MATH )

#define HOEDOWN_EXT_FLAGS (\
	HOEDOWN_EXT_NO_INTRA_EMPHASIS |\
	HOEDOWN_EXT_SPACE_HEADERS |\
	HOEDOWN_EXT_MATH_EXPLICIT )

#define HOEDOWN_EXT_NEGATIVE (\
	HOEDOWN_EXT_DISABLE_INDENTED_CODE )

typedef enum hoedown_list_flags {
	HOEDOWN_LIST_ORDERED = (1 << 0),
	HOEDOWN_LI_BLOCK = (1 << 1)	/* <li> containing block data */
} hoedown_list_flags;

typedef enum hoedown_table_flags {
	HOEDOWN_TABLE_ALIGN_LEFT = 1,
	HOEDOWN_TABLE_ALIGN_RIGHT = 2,
	HOEDOWN_TABLE_ALIGN_CENTER = 3,
	HOEDOWN_TABLE_ALIGNMASK = 3,
	HOEDOWN_TABLE_HEADER = 4
} hoedown_table_flags;

typedef enum hoedown_autolink_type {
	HOEDOWN_AUTOLINK_NONE,		/* used internally when it is not an autolink*/
	HOEDOWN_AUTOLINK_NORMAL,	/* normal http/http/ftp/mailto/etc link */
	HOEDOWN_AUTOLINK_EMAIL		/* e-mail link without explit mailto: */
} hoedown_autolink_type;

struct hoedown_document;

typedef struct hoedown_document hoedown_document;

/* hoedown_renderer - functions for rendering parsed data */
typedef struct hoedown_renderer {
	/* state object */
	void *opaque;

	/* block level callbacks - NULL skips the block */
	void (*blockcode)(hbuf *ob, const hbuf *text, const hbuf *lang, void *data);
	void (*blockquote)(hbuf *ob, const hbuf *content, void *data);
	void (*header)(hbuf *ob, const hbuf *content, int level, void *data);
	void (*hrule)(hbuf *ob, void *data);
	void (*list)(hbuf *ob, const hbuf *content, hoedown_list_flags flags, void *data);
	void (*listitem)(hbuf *ob, const hbuf *content, hoedown_list_flags flags, void *data);
	void (*paragraph)(hbuf *ob, const hbuf *content, void *data);
	void (*table)(hbuf *ob, const hbuf *content, void *data);
	void (*table_header)(hbuf *ob, const hbuf *content, void *data);
	void (*table_body)(hbuf *ob, const hbuf *content, void *data);
	void (*table_row)(hbuf *ob, const hbuf *content, void *data);
	void (*table_cell)(hbuf *ob, const hbuf *content, hoedown_table_flags flags, void *data, size_t, size_t);
	void (*footnotes)(hbuf *ob, const hbuf *content, void *data);
	void (*footnote_def)(hbuf *ob, const hbuf *content, unsigned int num, void *data);
	void (*blockhtml)(hbuf *ob, const hbuf *text, void *data);

	/* span level callbacks - NULL or return 0 prints the span verbatim */
	int (*autolink)(hbuf *ob, const hbuf *link, hoedown_autolink_type type, void *data);
	int (*codespan)(hbuf *ob, const hbuf *text, void *data);
	int (*double_emphasis)(hbuf *ob, const hbuf *content, void *data);
	int (*emphasis)(hbuf *ob, const hbuf *content, void *data);
	int (*underline)(hbuf *ob, const hbuf *content, void *data);
	int (*highlight)(hbuf *ob, const hbuf *content, void *data);
	int (*quote)(hbuf *ob, const hbuf *content, void *data);
	int (*image)(hbuf *ob, const hbuf *link, const hbuf *title, const hbuf *alt, void *data);
	int (*linebreak)(hbuf *ob, void *data);
	int (*link)(hbuf *ob, const hbuf *content, const hbuf *link, const hbuf *title, void *data);
	int (*triple_emphasis)(hbuf *ob, const hbuf *content, void *data);
	int (*strikethrough)(hbuf *ob, const hbuf *content, void *data);
	int (*superscript)(hbuf *ob, const hbuf *content, void *data);
	int (*footnote_ref)(hbuf *ob, unsigned int num, void *data);
	int (*math)(hbuf *ob, const hbuf *text, int displaymode, void *data);
	int (*raw_html)(hbuf *ob, const hbuf *text, void *data);

	/* low level callbacks - NULL copies input directly into the output */
	void (*entity)(hbuf *ob, const hbuf *text, void *data);
	void (*normal_text)(hbuf *ob, const hbuf *text, void *data);

	/* miscellaneous callbacks */
	void (*doc_header)(hbuf *ob, int inline_render, void *data);
	void (*doc_footer)(hbuf *ob, int inline_render, void *data);
} hoedown_renderer;

typedef enum hoedown_html_flags {
	HOEDOWN_HTML_SKIP_HTML = (1 << 0),
	HOEDOWN_HTML_ESCAPE = (1 << 1),
	HOEDOWN_HTML_HARD_WRAP = (1 << 2),
	HOEDOWN_HTML_USE_XHTML = (1 << 3),
	HOEDOWN_HTML_ASIDE = (1 << 4)
} hoedown_html_flags;

typedef enum hoedown_html_tag {
	HOEDOWN_HTML_TAG_NONE = 0,
	HOEDOWN_HTML_TAG_OPEN,
	HOEDOWN_HTML_TAG_CLOSE
} hoedown_html_tag;

__BEGIN_DECLS

void	*xmalloc(size_t size) __attribute__((malloc));
void	*xcalloc(size_t nmemb, size_t size) __attribute__((malloc));
void	*xrealloc(void *ptr, size_t size);

hbuf	*hbuf_new(size_t) __attribute__((malloc));
void	 hbuf_grow(hbuf *, size_t);
void	 hbuf_put(hbuf *, const uint8_t *, size_t);
void	 hbuf_puts(hbuf *, const char *);
void	 hbuf_putc(hbuf *, uint8_t);
int	 hbuf_putf(hbuf *, FILE *);
int	 hbuf_prefix(const hbuf *, const char *);
void	 hbuf_printf(hbuf *, const char *, ...) 
		__attribute__((format (printf, 2, 3)));
void	 hbuf_free(hbuf *);

/* HBUF_PUTSL: optimized hbuf_puts of a string literal */
#define HBUF_PUTSL(output, literal) \
	hbuf_put(output, (const uint8_t *)literal, sizeof(literal) - 1)

/* hoedown_autolink_is_safe: verify that a URL has a safe protocol */
int	 hoedown_autolink_is_safe(const uint8_t *data, size_t size);

/* hoedown_autolink__www: search for the next www link in data */
size_t	 hoedown_autolink__www(size_t *rewind_p, 
		hbuf *link, uint8_t *data, 
		size_t offset, size_t size, 
		hoedown_autolink_flags flags);

/* hoedown_autolink__email: search for the next email in data */
size_t	 hoedown_autolink__email(size_t *rewind_p, 
		hbuf *link, uint8_t *data, 
		size_t offset, size_t size, 
		hoedown_autolink_flags flags);

/* hoedown_autolink__url: search for the next URL in data */
size_t	 hoedown_autolink__url(size_t *rewind_p, 
		hbuf *link, uint8_t *data, 
		size_t offset, size_t size, 
		hoedown_autolink_flags flags);

/* hoedown_stack_init: initialize a stack */
void	 hoedown_stack_init(hoedown_stack *st, size_t initial_size);

/* hoedown_stack_uninit: free internal data of the stack */
void	 hoedown_stack_uninit(hoedown_stack *st);

/* hoedown_stack_grow: increase the allocated size to the given value */
void	 hoedown_stack_grow(hoedown_stack *st, size_t neosz);

/* hoedown_stack_push: push an item to the top of the stack */
void	 hoedown_stack_push(hoedown_stack *st, void *item);

/* hoedown_stack_pop: retrieve and remove the item at the top of the stack */
void	*hoedown_stack_pop(hoedown_stack *st);

/* hoedown_stack_top: retrieve the item at the top of the stack */
void	*hoedown_stack_top(const hoedown_stack *st);

/* hoedown_document_new: allocate a new document processor instance */
hoedown_document *hoedown_document_new(const hoedown_renderer *renderer,
		hoedown_extensions extensions, size_t max_nesting) 
		__attribute__((malloc));

/* hoedown_document_render: render regular Markdown using the document processor */
void	 hoedown_document_render(hoedown_document *doc, 
		hbuf *ob, const uint8_t *data, 
		size_t size);

/* hoedown_document_free: deallocate a document processor instance */
void	 hoedown_document_free(hoedown_document *doc);

/* hoedown_escape_href: escape (part of) a URL inside HTML */
void	 hoedown_escape_href(hbuf *ob, const uint8_t *data, size_t size);

/* hoedown_escape_html: escape HTML */
void	 hoedown_escape_html(hbuf *ob, const uint8_t *data, size_t size, int secure);

/* hoedown_escape_nroff: escape HTML */
void	 hoedown_escape_nroff(hbuf *ob, const uint8_t *data, size_t size, int secure);

/* hoedown_html_is_tag: checks if data starts with a specific tag, returns the tag type or NONE */
hoedown_html_tag hoedown_html_is_tag(const uint8_t *data, 
		size_t size, const char *tagname);

/* hoedown_html_renderer_new: allocates a regular HTML renderer */
hoedown_renderer *hoedown_html_renderer_new(hoedown_html_flags render_flags,
		int nesting_level) __attribute__ ((malloc));

/* hoedown_html_renderer_free: deallocate an HTML renderer */
void	 hoedown_html_renderer_free(hoedown_renderer *renderer);

hoedown_renderer *hoedown_nroff_renderer_new(hoedown_html_flags render_flags,
		int nesting_level) __attribute__ ((malloc));
void	 hoedown_nroff_renderer_free(hoedown_renderer *renderer);

/* hoedown_html_smartypants: process an HTML snippet using SmartyPants for smart punctuation */
void	 hoedown_html_smartypants(hbuf *ob, const uint8_t *data, size_t size);

/* hoedown_html_smartypants: process an HTML snippet using SmartyPants for smart punctuation */
void	 hoedown_nroff_smartypants(hbuf *ob, const uint8_t *data, size_t size);

__END_DECLS

#endif /* !EXTERN_H */
