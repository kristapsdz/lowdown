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

typedef struct hstack {
	void		**item;
	size_t		  size;
	size_t		  asize;
} hstack;

typedef enum hlist_fl {
	HLIST_FL_ORDERED = (1 << 0),
	HLIST_FL_BLOCK = (1 << 1) /* <li> containing block data */
} hlist_fl;

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
	void (*paragraph)(hbuf *, const hbuf *, void *, size_t);
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

	int (*autolink)(hbuf *, const hbuf *, halink_type, void *, int);
	int (*codespan)(hbuf *, const hbuf *, void *, int);
	int (*double_emphasis)(hbuf *, const hbuf *, void *, int);
	int (*emphasis)(hbuf *, const hbuf *, void *, int);
	int (*highlight)(hbuf *, const hbuf *, void *, int);
	int (*image)(hbuf *, const hbuf *, const hbuf *, 
		const hbuf *, const hbuf *, void *);
	int (*linebreak)(hbuf *, void *);
	int (*link)(hbuf *, const hbuf *, 
		const hbuf *, const hbuf *, void *, int);
	int (*triple_emphasis)(hbuf *, const hbuf *, void *, int);
	int (*strikethrough)(hbuf *, const hbuf *, void *, int);
	int (*superscript)(hbuf *, const hbuf *, void *, int);
	int (*footnote_ref)(hbuf *, unsigned int num, void *);
	int (*math)(hbuf *, const hbuf *, int, void *);
	int (*raw_html)(hbuf *, const hbuf *, void *);

	/* 
	 * Low level callbacks: NULL copies input directly into the
	 * output.
	 */

	void (*entity)(hbuf *, const hbuf *, void *);
	void (*normal_text)(hbuf *, const hbuf *, void *, int);
	void (*backspace)(hbuf *);

	/* Miscellaneous callbacks. */

	void (*doc_header)(hbuf *, int, void *);
	void (*doc_footer)(hbuf *, int, void *);
} hrend;

__BEGIN_DECLS

void	*xmalloc(size_t) __attribute__((malloc));
void	*xcalloc(size_t, size_t) __attribute__((malloc));
void	*xrealloc(void *, size_t);
void	*xreallocarray(void *, size_t, size_t);
char	*xstrndup(const char *, size_t);

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

/* Optimized hbuf_puts of a string literal. */
#define HBUF_PUTSL(output, literal) \
	hbuf_put(output, (const uint8_t *)literal, sizeof(literal) - 1)

size_t	 halink_email(size_t *, hbuf *, uint8_t *, size_t, size_t);
size_t	 halink_url(size_t *, hbuf *, uint8_t *, size_t, size_t);
size_t	 halink_www(size_t *, hbuf *, uint8_t *, size_t, size_t);

void	 hstack_grow(hstack *, size_t);
void	 hstack_init(hstack *, size_t);
void	 hstack_push(hstack *, void *);
void	*hstack_top(const hstack *);
void	 hstack_uninit(hstack *);

hdoc 	*hdoc_new(const hrend *, const struct lowdown_opts *, 
		unsigned int, size_t, int) __attribute__((malloc));
void	 hdoc_render(hdoc *, hbuf *, const uint8_t *, size_t, 
		struct lowdown_meta **, size_t *);
void	 hdoc_free(hdoc *);

void	 hesc_href(hbuf *, const uint8_t *, size_t);
void	 hesc_html(hbuf *, const uint8_t *, size_t, int);
void	 hesc_nroff(hbuf *, const uint8_t *, size_t, int, int);

void	 hrend_html_free(hrend *);
hrend	*hrend_html_new(unsigned int, int) __attribute__ ((malloc));

hrend	*hrend_nroff_new(unsigned int, int) __attribute__ ((malloc));
void	 hrend_nroff_free(hrend *);

void	 hsmrt_html(hbuf *, const uint8_t *, size_t);
void	 hsmrt_nroff(hbuf *, const uint8_t *, size_t);

void	 lmsg(const struct lowdown_opts *, enum lowdown_err, const char *, ...)
		__attribute__ ((format(printf, 3, 4)));

__END_DECLS

#endif /* !EXTERN_H */
