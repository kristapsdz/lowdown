/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016--2017 Kristaps Dzonsons <kristaps@bsd.lv>
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

void	 hesc_href(hbuf *, const uint8_t *, size_t);
void	 hesc_html(hbuf *, const uint8_t *, size_t, int);
void	 hesc_nroff(hbuf *, const uint8_t *, size_t, int, int);

void	 lmsg(const struct lowdown_opts *, enum lowdown_err, const char *, ...)
		__attribute__ ((format(printf, 3, 4)));

__END_DECLS

#endif /* !EXTERN_H */
