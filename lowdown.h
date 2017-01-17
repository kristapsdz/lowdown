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
	LOWDOWN_ERR__MAX
};

typedef	void (*lowdown_msg)(enum lowdown_err, void *, const char *);

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
#define LOWDOWN_UNDER	  0x20
#define LOWDOWN_HILITE	  0x40
#define LOWDOWN_QUOTE	  0x80
#define LOWDOWN_SUPER	  0x100
#define LOWDOWN_MATH	  0x200
#define LOWDOWN_NOINTEM	  0x400
#define LOWDOWN_SPHD	  0x800
#define LOWDOWN_MATHEXP	  0x1000
#define LOWDOWN_NOCODEIND 0x2000
#define	LOWDOWN_METADATA  0x4000
};

__BEGIN_DECLS

int		lowdown_file(const struct lowdown_opts *, 
			FILE *, unsigned char **, size_t *);
void		lowdown_buf(const struct lowdown_opts *, 
			const unsigned char *, size_t,
			unsigned char **, size_t *);
const char	*lowdown_errstr(enum lowdown_err);

__END_DECLS

#endif /* !EXTERN_H */
