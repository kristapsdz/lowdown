/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
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
#ifndef FORMAT_H
#define FORMAT_H

const char
*entity_find_nroff(const struct lowdown_buf *, int32_t *);

int32_t
entity_find_iso(const struct lowdown_buf *);

const char
*entity_find_tex(const struct lowdown_buf *, unsigned char *);

#define		 TEX_ENT_MATH	 0x01
#define		 TEX_ENT_ASCII	 0x02

int
lowdown_gemini_esc(struct lowdown_buf *, const char *, size_t, int);

int
lowdown_html_esc(struct lowdown_buf *, const char *, size_t, int, int, int);

int
lowdown_html_esc_attr(struct lowdown_buf *, const char *, size_t);

int
lowdown_html_esc_href(struct lowdown_buf *, const char *, size_t);

int
lowdown_latex_esc(struct lowdown_buf *, const char *, size_t);

int
lowdown_roff_esc(struct lowdown_buf *, const char *, size_t, int, int);

char *
rcsdate2str(const char *);

char *
rcsauthor2str(const char *);

struct lowdown_meta *
lowdown_get_meta(const struct lowdown_node *, struct lowdown_metaq *);

int
lowdown_template(const char *, const struct lowdown_buf *,
    struct lowdown_buf *,const struct lowdown_metaq *, int);

#endif /* !FORMAT_H */
