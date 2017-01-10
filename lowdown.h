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

typedef enum halink_flags {
	HALINK_SHORT_DOMAINS = (1 << 0)
} halink_flags;

typedef enum hdoc_ext {
	/* Block-level extensions. */
	HDOC_EXT_TABLES = (1 << 0),
	HDOC_EXT_FENCED_CODE = (1 << 1),
	HDOC_EXT_FOOTNOTES = (1 << 2),

	/* Span-level extensions. */
	HDOC_EXT_AUTOLINK = (1 << 3),
	HDOC_EXT_STRIKETHROUGH = (1 << 4),
	HDOC_EXT_UNDERLINE = (1 << 5),
	HDOC_EXT_HIGHLIGHT = (1 << 6),
	HDOC_EXT_QUOTE = (1 << 7),
	HDOC_EXT_SUPERSCRIPT = (1 << 8),
	HDOC_EXT_MATH = (1 << 9),

	/* Other flags. */
	HDOC_EXT_NO_INTRA_EMPHASIS = (1 << 11),
	HDOC_EXT_SPACE_HEADERS = (1 << 12),
	HDOC_EXT_MATH_EXPLICIT = (1 << 13),

	/* Negative flags. */
	HDOC_EXT_DISABLE_INDENTED_CODE = (1 << 14)
} hdoc_ext;

typedef enum hlist_fl {
	HLIST_ORDERED = (1 << 0),
	HLIST_BLOCK = (1 << 1) /* <li> containing block data */
} hlist_fl;

typedef enum htbl_flags {
	HTBL_ALIGN_LEFT = 1,
	HTBL_ALIGN_RIGHT = 2,
	HTBL_ALIGN_CENTER = 3,
	HTBL_ALIGNMASK = 3,
	HTBL_HEADER = 4
} htbl_flags;

typedef enum halink_type {
	HALINK_NONE, /* used internally when it is not an autolink */
	HALINK_NORMAL, /* normal http/http/ftp/mailto/etc link */
	HALINK_EMAIL /* e-mail link without explit mailto: */
} halink_type;

struct hdoc;

typedef struct hdoc hdoc;

/*
 * Functions callbacks for rendering parsed data.
 */
typedef struct hrend {
	/* Private object passed as void argument. */

	void *opaque;

	/* Block level callbacks: NULL skips the block. */

	void (*blockcode)(hbuf *, const hbuf *, const hbuf *, void *);
	void (*blockquote)(hbuf *, const hbuf *, void *);
	void (*header)(hbuf *, const hbuf *, int, void *);
	void (*hrule)(hbuf *, void *);
	void (*list)(hbuf *, const hbuf *, hlist_fl, void *);
	void (*listitem)(hbuf *, const hbuf *, hlist_fl, void *, size_t);
	void (*paragraph)(hbuf *, const hbuf *, void *);
	void (*table)(hbuf *, const hbuf *, void *);
	void (*table_header)(hbuf *, const hbuf *,
		void *, const htbl_flags *, size_t);
	void (*table_body)(hbuf *, const hbuf *, void *);
	void (*table_row)(hbuf *, const hbuf *, void *);
	void (*table_cell)(hbuf *, const hbuf *,
		htbl_flags, void *, size_t, size_t);
	void (*footnotes)(hbuf *, const hbuf *, void *);
	void (*footnote_def)(hbuf *, 
		const hbuf *, unsigned int, void *);
	void (*blockhtml)(hbuf *, const hbuf *, void *);

	/*
	 * Span level callbacks: NULL or return 0 prints the span
	 * verbatim.
	 */

	int (*autolink)(hbuf *, const hbuf *, halink_type, void *);
	int (*codespan)(hbuf *, const hbuf *, void *);
	int (*double_emphasis)(hbuf *, const hbuf *, void *);
	int (*emphasis)(hbuf *, const hbuf *, void *);
	int (*underline)(hbuf *, const hbuf *, void *);
	int (*highlight)(hbuf *, const hbuf *, void *);
	int (*quote)(hbuf *, const hbuf *, void *);
	int (*image)(hbuf *, const hbuf *,
		const hbuf *, const hbuf *, void *);
	int (*linebreak)(hbuf *, void *);
	int (*link)(hbuf *, const hbuf *,
		const hbuf *, const hbuf *, void *);
	int (*triple_emphasis)(hbuf *, const hbuf *, void *);
	int (*strikethrough)(hbuf *, const hbuf *, void *);
	int (*superscript)(hbuf *, const hbuf *, void *);
	int (*footnote_ref)(hbuf *, unsigned int num, void *);
	int (*math)(hbuf *, const hbuf *, int, void *);
	int (*raw_html)(hbuf *, const hbuf *, void *);

	/*
	 * Low level callbacks: NULL copies input directly into the
	 * output.
	 */

	void (*entity)(hbuf *, const hbuf *, void *);
	void (*normal_text)(hbuf *, const hbuf *, void *, int);

	/* Miscellaneous callbacks. */

	void (*doc_header)(hbuf *, int, void *);
	void (*doc_footer)(hbuf *, int, void *);
} hrend;

typedef enum hhtml_fl {
	HOEDOWN_HTML_SKIP_HTML = (1 << 0),
	HOEDOWN_HTML_ESCAPE = (1 << 1),
	HOEDOWN_HTML_HARD_WRAP = (1 << 2),
	HOEDOWN_HTML_USE_XHTML = (1 << 3),
	HOEDOWN_HTML_ASIDE = (1 << 4)
} hhtml_fl;

typedef enum hhtml_tag {
	HOEDOWN_HTML_TAG_NONE = 0,
	HOEDOWN_HTML_TAG_OPEN,
	HOEDOWN_HTML_TAG_CLOSE
} hhtml_tag;

__BEGIN_DECLS

void	 hbuf_free(hbuf *);
void	 hbuf_grow(hbuf *, size_t);
hbuf	*hbuf_new(size_t) __attribute__((malloc));
int	 hbuf_prefix(const hbuf *, const char *);
void	 hbuf_printf(hbuf *, const char *, ...)
		__attribute__((format (printf, 2, 3)));
void	 hbuf_put(hbuf *, const uint8_t *, size_t);
void	 hbuf_putc(hbuf *, uint8_t);
int	 hbuf_putf(hbuf *, FILE *);
void	 hbuf_puts(hbuf *, const char *);

hdoc 	*hdoc_new(const hrend *, hdoc_ext, size_t) __attribute__((malloc));
void	 hdoc_render(hdoc *, hbuf *, const uint8_t *, size_t );
void	 hdoc_free(hdoc *);

void	 hrend_html_free(hrend *);
hrend	*hrend_html_new(hhtml_fl, int) __attribute__ ((malloc));
