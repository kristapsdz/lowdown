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
 */

/*
 * Styles
 * ======
 *
 * Begin with text styles.  Each style should be formatted as follows:
 *
 *     static const struct sty sty_STYLE = {
 *     	italic?, strike?, bold?, under?, bgcolour, colour, override?
 *     };
 *
 * Italic, strike, bold, and under may be zero or non-zero numbers.  If
 * non-zero, the given style is applied and is inherited by all child
 * styles.
 *
 * Override is a bit-mask of styles that are overridden.  If 1 is set,
 * the underline is overridden; if 2, the bold.
 *
 * Bgcolour and colour may be zero or an 8-bit ANSI colour escape code.
 * See <https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit>.  These
 * are not inherited by child styles.
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

/* Definition list data prefix (see pfx_dli_1): foo \n >:< bar */
static const struct sty sty_dli_pfx =	{ 0, 0, 0, 0,   0, 93, 0 };

/* List prefix (see pfx_li_1): >1.< foo */
static const struct sty sty_li_pfx =	{ 0, 0, 0, 0,   0, 93, 0 };

/* Block quote prefix (see pfx_bkqt): >|< foo */
static const struct sty sty_bkqt_pfx =	{ 0, 0, 0, 0,   0, 93, 0 };

/* Block code prefix (see pfx_bkcd): ``` >|< void \n >|< main``` */
static const struct sty sty_bkcd_pfx =	{ 0, 0, 0, 0,   0, 94, 0 };

/*
 * Prefixes
 * ========
 *
 * What follows are hard-coded prefixes.  These appear on the left of
 * the output and have various rules not covered here to how they're
 * inherited by children.  Each prefix is arranged as:
 *
 *     static const struct pfx pfx_STYLE = { text, columns };
 *
 * The text is a quoted string that will be inserted as-is.  It may
 * contain UTF-8 values.  It may *only* be NULL if the documentation
 * specifically says that the value is ignored.
 *
 * Columns is the number of terminal columns that the prefix fills.  If
 * this is wrong, it will throw off line wrapping.  XXX: this may be
 * dynamically computed in later versions of lowodwn.
 */

/* Paragraph, table, definition title. */
static const struct pfx pfx_para =	{ "    ", 4 };

/* Block code (see sty_bkcd_pfx). */
static const struct pfx pfx_bkcd =	{ "    | ", 6 };

/* Block quote (see sty_bkqt_pfx). */
static const struct pfx pfx_bkqt =	{ "    | ", 6 };

/* Definition list data, first line (see sty_dli_pfx). */
static const struct pfx pfx_dli_1 =	{ "    : ", 6 };

/* Ordered list item, first line (see sty_li_pfx).  Text ignored. */
static const struct pfx pfx_oli_1 =	{ NULL, 6 };

/* Unordered list item, first line (see sty_li_pfx). */
static const struct pfx pfx_uli_1 =	{ "    · ", 6 };

/* Unordered, checked list data, first line (see sty_li_pfx). */
static const struct pfx pfx_uli_c1 =	{ "    ☑ ", 6 };

/* Unordered, unchecked list data, first line (see sty_li_pfx). */
static const struct pfx pfx_uli_nc1 =	{ "    ☐ ", 6 };

/* List items, subsequent lines (see sty_li_pfx). */
static const struct pfx pfx_li_n =	{ "      ", 6 };

/* Footnote prefix, first line (see sty_fdef_pfx).  Text ignored. */
static const struct pfx pfx_fdef_1 =	{ NULL, 4 };

/* Footnote prefix, subsequent lines (see sty_fdef_pfx). */
static const struct pfx pfx_fdef_n =	{ "    ", 4 };

/* Header first prefix (see sty_header_1). */
static const struct pfx pfx_header_1 =	{ "", 0 };

/* Header non-first prefix, one per head level (see sty_header_n). */
static const struct pfx pfx_header_n =	{ "#", 1 };

