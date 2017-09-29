/*	$Id$ */
/*
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

#include <stdio.h>

#include "lowdown.h"
#include "extern.h"

/*
 * Escape nroff.
 * If "span" is non-zero, don't test for leading periods.
 * Otherwise, a leading period will be escaped.
 * If "oneline" is non-zero, newlines are replaced with spaces.
 */
void
hesc_nroff(hbuf *ob, const char *data, 
	size_t size, int span, int oneline)
{
	size_t	 i;

	if (0 == size)
		return;

	if ( ! span && '.' == data[0])
		HBUF_PUTSL(ob, "\\&");

	/*
	 * According to mandoc_char(7), we need to escape the backtick,
	 * single apostrophe, and tilde or else they'll be considered as
	 * special Unicode output.
	 * Slashes need to be escaped too, and newlines if appropriate
	 */

	for (i = 0; i < size; i++) {
		switch (data[i]) {
		case '^':
			HBUF_PUTSL(ob, "\\(ha");
			break;
		case '~':
			HBUF_PUTSL(ob, "\\(ti");
			break;
		case '`':
			HBUF_PUTSL(ob, "\\(ga");
			break;
		case '\n':
			hbuf_putc(ob, oneline ? ' ' : '\n');
			break;
		case '\\':
			HBUF_PUTSL(ob, "\\e");
			break;
		case '.':
			if ( ! oneline && i && '\n' == data[i - 1])
				HBUF_PUTSL(ob, "\\&");
			/* FALLTHROUGH */
		default:
			hbuf_putc(ob, data[i]);
			break;
		}
	}
}
