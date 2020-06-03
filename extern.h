/*	$Id$ */
/*
 * Copyright (c) 2016--2020 Kristaps Dzonsons <kristaps@bsd.lv>
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

void		*xmalloc(size_t) __attribute__((malloc));
void		*xcalloc(size_t, size_t) __attribute__((malloc));
void		*xrealloc(void *, size_t);
void		*xreallocarray(void *, size_t, size_t);
void		*xrecallocarray(void *, size_t, size_t, size_t);
char		*xstrndup(const char *, size_t);
char		*xstrdup(const char *);

void	 	 smarty(struct lowdown_node *, size_t, enum lowdown_type);

int32_t	 	 entity_find_iso(const hbuf *);
const char	*entity_find_tex(const hbuf *, unsigned char *);
#define		 TEX_ENT_MATH	 0x01
#define		 TEX_ENT_ASCII	 0x02

int		 hbuf_eq(const hbuf *, const hbuf *);
int		 hbuf_streq(const hbuf *, const char *);
int		 hbuf_strprefix(const hbuf *, const char *);
void		 hbuf_free(hbuf *);
void		 hbuf_grow(hbuf *, size_t);
hbuf		*hbuf_clone(const hbuf *, hbuf *);
hbuf		*hbuf_new(size_t) __attribute__((malloc));
void		 hbuf_printf(hbuf *, const char *, ...) 
			__attribute__((format (printf, 2, 3)));
void		 hbuf_put(hbuf *, const char *, size_t);
void		 hbuf_putb(hbuf *, const hbuf *);
void		 hbuf_putc(hbuf *, char);
int		 hbuf_putf(hbuf *, FILE *);
void		 hbuf_puts(hbuf *, const char *);
void		 hbuf_truncate(hbuf *);

#define 	 HBUF_PUTSL(output, literal) \
		 hbuf_put(output, literal, sizeof(literal) - 1)

size_t		 halink_email(size_t *, hbuf *, char *, size_t, size_t);
size_t		 halink_url(size_t *, hbuf *, char *, size_t, size_t);
size_t		 halink_www(size_t *, hbuf *, char *, size_t, size_t);

void		 hesc_attr(hbuf *, const char *, size_t);
void		 hesc_href(hbuf *, const char *, size_t);
void		 hesc_html(hbuf *, const char *, size_t, int, int, int);

char		*rcsdate2str(const char *);
char		*date2str(const char *);
char		*rcsauthor2str(const char *);

#endif /* !EXTERN_H */
