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

typedef	void *(*hoedown_realloc_callback)(void *, size_t);
typedef	void (*hoedown_free_callback)(void *);

typedef struct hoedown_buffer {
	uint8_t *data;	/* actual character data */
	size_t size;	/* size of the string */
	size_t asize;	/* allocated size (0 = volatile buffer) */
	size_t unit;	/* reallocation unit size (0 = read-only buffer) */

	hoedown_realloc_callback data_realloc;
	hoedown_free_callback data_free;
	hoedown_free_callback buffer_free;
} hoedown_buffer;

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

typedef struct hoedown_renderer_data {
	void *opaque;
} hoedown_renderer_data;

/* hoedown_renderer - functions for rendering parsed data */
typedef struct hoedown_renderer {
	/* state object */
	void *opaque;

	/* block level callbacks - NULL skips the block */
	void (*blockcode)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_buffer *lang, const hoedown_renderer_data *data);
	void (*blockquote)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*header)(hoedown_buffer *ob, const hoedown_buffer *content, int level, const hoedown_renderer_data *data);
	void (*hrule)(hoedown_buffer *ob, const hoedown_renderer_data *data);
	void (*list)(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data);
	void (*listitem)(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data);
	void (*paragraph)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table_header)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table_body)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table_row)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table_cell)(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_table_flags flags, const hoedown_renderer_data *data);
	void (*footnotes)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*footnote_def)(hoedown_buffer *ob, const hoedown_buffer *content, unsigned int num, const hoedown_renderer_data *data);
	void (*blockhtml)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);

	/* span level callbacks - NULL or return 0 prints the span verbatim */
	int (*autolink)(hoedown_buffer *ob, const hoedown_buffer *link, hoedown_autolink_type type, const hoedown_renderer_data *data);
	int (*codespan)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);
	int (*double_emphasis)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*emphasis)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*underline)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*highlight)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*quote)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*image)(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_buffer *alt, const hoedown_renderer_data *data);
	int (*linebreak)(hoedown_buffer *ob, const hoedown_renderer_data *data);
	int (*link)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_renderer_data *data);
	int (*triple_emphasis)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*strikethrough)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*superscript)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*footnote_ref)(hoedown_buffer *ob, unsigned int num, const hoedown_renderer_data *data);
	int (*math)(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, const hoedown_renderer_data *data);
	int (*raw_html)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);

	/* low level callbacks - NULL copies input directly into the output */
	void (*entity)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);
	void (*normal_text)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);

	/* miscellaneous callbacks */
	void (*doc_header)(hoedown_buffer *ob, int inline_render, const hoedown_renderer_data *data);
	void (*doc_footer)(hoedown_buffer *ob, int inline_render, const hoedown_renderer_data *data);
} hoedown_renderer;

typedef enum hoedown_html_flags {
	HOEDOWN_HTML_SKIP_HTML = (1 << 0),
	HOEDOWN_HTML_ESCAPE = (1 << 1),
	HOEDOWN_HTML_HARD_WRAP = (1 << 2),
	HOEDOWN_HTML_USE_XHTML = (1 << 3)
} hoedown_html_flags;

typedef enum hoedown_html_tag {
	HOEDOWN_HTML_TAG_NONE = 0,
	HOEDOWN_HTML_TAG_OPEN,
	HOEDOWN_HTML_TAG_CLOSE
} hoedown_html_tag;

typedef struct hoedown_html_renderer_state {
	void *opaque;

	struct {
		int header_count;
		int current_level;
		int level_offset;
		int nesting_level;
	} toc_data;

	hoedown_html_flags flags;

	/* extra callbacks */
	void (*link_attributes)(hoedown_buffer *ob, const hoedown_buffer *url, const hoedown_renderer_data *data);
} hoedown_html_renderer_state;

__BEGIN_DECLS

/* allocation wrappers */
void	*hoedown_malloc(size_t size) __attribute__((malloc));
void	*hoedown_calloc(size_t nmemb, size_t size) __attribute__((malloc));
void	*hoedown_realloc(void *ptr, size_t size) __attribute__((malloc));

/* hoedown_buffer_init: initialize a buffer with custom allocators */
void	 hoedown_buffer_init(hoedown_buffer *buffer,
		size_t unit,
		hoedown_realloc_callback data_realloc,
		hoedown_free_callback data_free,
		hoedown_free_callback buffer_free);

/* hoedown_buffer_uninit: uninitialize an existing buffer */
void	 hoedown_buffer_uninit(hoedown_buffer *buf);

/* hoedown_buffer_new: allocate a new buffer */
hoedown_buffer *hoedown_buffer_new(size_t unit) __attribute__((malloc));

/* hoedown_buffer_reset: free internal data of the buffer */
void	 hoedown_buffer_reset(hoedown_buffer *buf);

/* hoedown_buffer_grow: increase the allocated size to the given value */
void	 hoedown_buffer_grow(hoedown_buffer *buf, size_t neosz);

/* hoedown_buffer_put: append raw data to a buffer */
void	 hoedown_buffer_put(hoedown_buffer *buf, const uint8_t *data, size_t size);

/* hoedown_buffer_puts: append a NUL-terminated string to a buffer */
void	 hoedown_buffer_puts(hoedown_buffer *buf, const char *str);

/* hoedown_buffer_putc: append a single char to a buffer */
void	 hoedown_buffer_putc(hoedown_buffer *buf, uint8_t c);

/* hoedown_buffer_putf: read from a file and append to a buffer, until EOF or error */
int	 hoedown_buffer_putf(hoedown_buffer *buf, FILE* file);

/* hoedown_buffer_set: replace the buffer's contents with raw data */
void	 hoedown_buffer_set(hoedown_buffer *buf, const uint8_t *data, size_t size);

/* hoedown_buffer_sets: replace the buffer's contents with a NUL-terminated string */
void	 hoedown_buffer_sets(hoedown_buffer *buf, const char *str);

/* hoedown_buffer_eq: compare a buffer's data with other data for equality */
int	 hoedown_buffer_eq(const hoedown_buffer *buf, const uint8_t *data, size_t size);

/* hoedown_buffer_eq: compare a buffer's data with NUL-terminated string for equality */
int	 hoedown_buffer_eqs(const hoedown_buffer *buf, const char *str);

/* hoedown_buffer_prefix: compare the beginning of a buffer with a string */
int	 hoedown_buffer_prefix(const hoedown_buffer *buf, const char *prefix);

/* hoedown_buffer_slurp: remove a given number of bytes from the head of the buffer */
void	 hoedown_buffer_slurp(hoedown_buffer *buf, size_t size);

/* hoedown_buffer_cstr: NUL-termination of the string array (making a C-string) */
const char *hoedown_buffer_cstr(hoedown_buffer *buf);

/* hoedown_buffer_printf: formatted printing to a buffer */
void	 hoedown_buffer_printf(hoedown_buffer *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));

/* hoedown_buffer_put_utf8: put a Unicode character encoded as UTF-8 */
void	 hoedown_buffer_put_utf8(hoedown_buffer *buf, unsigned int codepoint);

/* hoedown_buffer_free: free the buffer */
void	 hoedown_buffer_free(hoedown_buffer *buf);

/* HOEDOWN_BUFPUTSL: optimized hoedown_buffer_puts of a string literal */
#define HOEDOWN_BUFPUTSL(output, literal) \
	hoedown_buffer_put(output, (const uint8_t *)literal, sizeof(literal) - 1)

/* HOEDOWN_BUFSETSL: optimized hoedown_buffer_sets of a string literal */
#define HOEDOWN_BUFSETSL(output, literal) \
	hoedown_buffer_set(output, (const uint8_t *)literal, sizeof(literal) - 1)

/* HOEDOWN_BUFEQSL: optimized hoedown_buffer_eqs of a string literal */
#define HOEDOWN_BUFEQSL(output, literal) \
	hoedown_buffer_eq(output, (const uint8_t *)literal, sizeof(literal) - 1)

/* hoedown_autolink_is_safe: verify that a URL has a safe protocol */
int	 hoedown_autolink_is_safe(const uint8_t *data, size_t size);

/* hoedown_autolink__www: search for the next www link in data */
size_t	 hoedown_autolink__www(size_t *rewind_p, 
		hoedown_buffer *link, uint8_t *data, 
		size_t offset, size_t size, 
		hoedown_autolink_flags flags);

/* hoedown_autolink__email: search for the next email in data */
size_t	 hoedown_autolink__email(size_t *rewind_p, 
		hoedown_buffer *link, uint8_t *data, 
		size_t offset, size_t size, 
		hoedown_autolink_flags flags);

/* hoedown_autolink__url: search for the next URL in data */
size_t	 hoedown_autolink__url(size_t *rewind_p, 
		hoedown_buffer *link, uint8_t *data, 
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
		hoedown_buffer *ob, const uint8_t *data, 
		size_t size);

/* hoedown_document_render_inline: render inline Markdown using the document processor */
void	 hoedown_document_render_inline(hoedown_document *doc, 
		hoedown_buffer *ob, const uint8_t *data, 
		size_t size);

/* hoedown_document_free: deallocate a document processor instance */
void	 hoedown_document_free(hoedown_document *doc);

/* hoedown_escape_href: escape (part of) a URL inside HTML */
void	 hoedown_escape_href(hoedown_buffer *ob, const uint8_t *data, size_t size);

/* hoedown_escape_html: escape HTML */
void	 hoedown_escape_html(hoedown_buffer *ob, const uint8_t *data, size_t size, int secure);

/* hoedown_html_is_tag: checks if data starts with a specific tag, returns the tag type or NONE */
hoedown_html_tag hoedown_html_is_tag(const uint8_t *data, 
		size_t size, const char *tagname);

/* hoedown_html_renderer_new: allocates a regular HTML renderer */
hoedown_renderer *hoedown_html_renderer_new(hoedown_html_flags render_flags,
		int nesting_level) __attribute__ ((malloc));

/* hoedown_html_renderer_free: deallocate an HTML renderer */
void	 hoedown_html_renderer_free(hoedown_renderer *renderer);

__END_DECLS

#endif /* !EXTERN_H */
