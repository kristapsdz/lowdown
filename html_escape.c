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
#include "config.h"

#include <sys/queue.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

/*
 * The following characters will not be escaped:
 *
 *		-_.+!*'(),%#@?=;:/,+&$ alphanum
 *
 * Note that this character set is the addition of:
 *
 * - The characters which are safe to be in an URL
 * - The characters which are *not* safe to be in an URL because they
 *   are RESERVED characters.
 *
 * We assume (lazily) that any RESERVED char that appears inside an URL
 * is actually meant to have its native function (i.e. as an URL
 * component/separator) and hence needs no escaping.
 *
 * There are two exceptions: the chacters & (amp) and ' (single quote)
 * do not appear in the table.  They are meant to appear in the URL as
 * components, yet they require special HTML-entity escaping to generate
 * valid HTML markup.
 *
 * All other characters will be escaped to %XX.
 */
static const int HREF_SAFE[UINT8_MAX + 1] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/*
 * According to the OWASP rules:
 *
 * & --> &amp;
 * < --> &lt;
 * > --> &gt;
 * " --> &quot;
 * ' --> &#x27;     &apos; is not recommended
 * / --> &#x2F;     forward slash helps end an HTML entity
 *
 */
static const int HTML_ESCAPE_TABLE[UINT8_MAX + 1] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 1, 0, 0, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0, 4,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 6, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const char *HTML_ESCAPES[] = {
        "",
        "&quot;",
        "&amp;",
        "&#39;",
        "&#47;",
        "&lt;",
        "&gt;"
};

/* 
 * Escape (part of) a URL inside HTML.
 */
void
hesc_href(hbuf *ob, const char *data, size_t size)
{
	static const char hex_chars[] = "0123456789ABCDEF";
	size_t  i = 0, mark;
	char hex_str[3];

	hex_str[0] = '%';

	while (i < size) {
		mark = i;
		while (i < size && 
		       HREF_SAFE[(unsigned char)data[i]]) 
			i++;

		/* 
		 * Optimization for cases where there's nothing to
		 * escape.
		*/

		if (mark == 0 && i >= size) {
			hbuf_put(ob, data, size);
			return;
		}

		if (i > mark)
			hbuf_put(ob, data + mark, i - mark);

		/* Escaping... */

		if (i >= size)
			break;

		switch (data[i]) {
		case '&':
			/* 
			 * Amp appears all the time in URLs, but needs
			 * HTML-entity escaping to be inside an href.
			*/
			HBUF_PUTSL(ob, "&amp;");
			break;
		case '\'':
			/* 
			 * The single quote is a valid URL character
			 * according to the standard; it needs HTML
			 * entity escaping too.
			*/
			HBUF_PUTSL(ob, "&#x27;");
			break;
#if 0
		case ' ':
			/* 
			 * The space can be escaped to %20 or a plus
			 * sign. we're going with the generic escape for
			 * now. the plus thing is more commonly seen
			 * when building GET strings.
			*/
			hbuf_putc(ob, '+');
			break;
#endif
		default:
			/* 
			 * Every other character goes with a %XX
			 * escaping.
			*/
			hex_str[1] = hex_chars[(data[i] >> 4) & 0xF];
			hex_str[2] = hex_chars[data[i] & 0xF];
			hbuf_put(ob, hex_str, 3);
			break;
		}
		i++;
	}
}

/* 
 * Escape HTML.
 */
void
hesc_html(hbuf *ob, const char *data, size_t size, int secure)
{
	size_t i = 0, mark;

	while (1) {
		mark = i;
		while (i < size && 
		       HTML_ESCAPE_TABLE[(unsigned char)data[i]] == 0) 
			i++;

		/* Optimization for cases where there's nothing to escape */
		if (mark == 0 && i >= size) {
			hbuf_put(ob, data, size);
			return;
		}

		if (i > mark)
			hbuf_put(ob, data + mark, i - mark);

		if (i >= size) 
			break;

		/* The forward slash is only escaped in secure mode */

		if ( ! secure && data[i] == '/')
			hbuf_putc(ob, '/');
		else
			hbuf_puts(ob, HTML_ESCAPES
				[HTML_ESCAPE_TABLE[(unsigned char)data[i]]]);
		i++;
	}
}
