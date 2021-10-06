/*
 * Copyright (c) 2021 Kristaps Dzonsons <kristaps@bsd.lv>
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
 * This file is for direct inclusion into term.c.  It allows an easy
 * place to make compile-term overrides of default styles.
 *
 * Please note that if NO_COLOR is specified during run-time, all of the
 * colour codes will be stripped.  When customising this, please make
 * sure that your style will work both with colours and without.
 */

/* For inserted content.  Common parent style. */
static const struct sty sty_chng_ins =	{ 0, 0, 0, 0,  47, 30, 0 };

/* For deleted content.  Common parent style. */
static const struct sty sty_chng_del =	{ 0, 0, 0, 0, 100,  0, 0 };

/* Image: >![alt](link)< */
static const struct sty sty_img =	{ 0, 0, 1, 0,   0, 93, 1 };

/* Box around image link (in sty_img): ![alt](>link<) */
static const struct sty sty_imgurlbox =	{ 0, 0, 0, 0,   0, 37, 2 };

/* Image link text (in sty_imgurlbox): ![alt](>link<) */
static const struct sty sty_imgurl = 	{ 0, 0, 0, 1,   0, 32, 2 };

/* Footnote reference (as a number): >[^ref]< */
static const struct sty sty_foot_ref =	{ 0, 0, 1, 0,   0, 93, 1 };

/* In-line code: >`foo(void)`< */
static const struct sty sty_codespan = 	{ 0, 0, 1, 0,   0, 94, 0 };

/* Block code: ```foo(void)```< */
static const struct sty sty_blockcode =	{ 0, 0, 1, 0,   0,  0, 0 };

/* Horizontal line: >***< */
static const struct sty sty_hrule = 	{ 0, 0, 0, 0,   0, 37, 0 };

/* Block HTML: ><html></html>< */
static const struct sty sty_blockhtml =	{ 0, 0, 0, 0,   0, 37, 0 };

/* In-line HTML: ><span>< */
static const struct sty sty_rawhtml = 	{ 0, 0, 0, 0,   0, 37, 0 };

/* Strike-through: >~~foo~~< */
static const struct sty sty_strike = 	{ 0, 1, 0, 0,   0,  0, 0 };

/* Emphasis: >*foo*< */
static const struct sty sty_emph = 	{ 1, 0, 0, 0,   0,  0, 0 };

/* Highlight: >==foo==< */
static const struct sty sty_highlight =	{ 0, 0, 1, 0,   0,  0, 0 };

/* Double-emphasis: >**foo**< */
static const struct sty sty_d_emph = 	{ 0, 0, 1, 0,   0,  0, 0 };

/* Triple emphasis: >***foo***< */
static const struct sty sty_t_emph = 	{ 1, 0, 1, 0,   0,  0, 0 };

/* Link: >[text](link)< */
static const struct sty sty_link = 	{ 0, 0, 0, 1,   0, 32, 0 };

/* Link text (in sty_link): [>text<](link) */
static const struct sty sty_linkalt =	{ 0, 0, 1, 0,   0, 93, 1|2 };

/* Standalone link: >https://link< */
static const struct sty sty_autolink =	{ 0, 0, 0, 1,   0, 32, 0 };

/* Header: >## Header< */
static const struct sty sty_header =	{ 0, 0, 1, 0,   0,  0, 0 };

/* First header (in sty_header): ># Header< */
static const struct sty sty_header_1 = 	{ 0, 0, 0, 0,   0, 91, 0 };

/* Non-first header (in sty_header): >### Header< */
static const struct sty sty_header_n = 	{ 0, 0, 0, 0,   0, 36, 0 };

/* Footnote block: >[^ref]: foo bar< */
static const struct sty sty_foot =	{ 0, 0, 0, 0,   0, 37, 0 };

/* Footnote prefix (in sty_foot, as a number): >[^ref]<: foo bar */
static const struct sty sty_fdef_pfx =	{ 0, 0, 0, 0,   0, 92, 1 };

/* Metadata key: >key:< val */
static const struct sty sty_meta_key =	{ 0, 0, 0, 0,   0, 37, 0 };

/* Entity (if not valid): >&#badent;< */
static const struct sty sty_bad_ent = 	{ 0, 0, 0, 0,   0, 37, 0 };

/* Definition list data prefix: foo \n >:< bar */
static const struct sty sty_ddata_pfx =	{ 0, 0, 0, 0,   0, 93, 0 };

/* Ordered list prefix: >1.< foo */
static const struct sty sty_oli_pfx =	{ 0, 0, 0, 0,   0, 93, 0 };

/* Unordered list prefix: >-< foo */
static const struct sty sty_uli_pfx =	{ 0, 0, 0, 0,   0, 93, 0 };

/* Block quote prefix: >|< foo */
static const struct sty sty_bkqt_pfx =	{ 0, 0, 0, 0,   0, 93, 0 };

/* Block code prefix: ``` >|< void \n >|< main``` */
static const struct sty sty_bkcd_pfx =	{ 0, 0, 0, 0,   0, 94, 0 };

