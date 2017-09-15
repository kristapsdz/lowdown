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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

/*
 * FIXME: merge with re-written hesc_nroff.
 */
static void
hesc_nroff_oneline(hbuf *ob, const char *data, size_t sz, int span)
{
	size_t	 i;

	if (0 == sz)
		return;

	assert(NULL != data);

	/* Pre-terminate. */

	if (0 == span && '.' == data[0])
		HBUF_PUTSL(ob, "\\&");

	for (i = 0; i < sz; i++) {
		if ('\n' == data[i]) {
			HBUF_PUTSL(ob, " ");
			continue;
		} else if ('\\' == data[i]) {
			HBUF_PUTSL(ob, "\\e");
			continue;
		}
		hbuf_putc(ob, data[i]);
	}
}

/*
 * Escape nroff.
 * This function was just copied from hesc_html and needs re-writing.
 * There are two ways to do this: block and span (controlled by the
 * "span" variable).
 * Then there's "oneline", which removes all newlines.
 * If "span" is non-zero, then we only escape characters following the
 * first.
 * If "span" is zero, then we also check the first character.
 * The intuition is that a "block" has its initial character after a
 * newline, and thus needs the newline check.
 * Finally, "oneline" strips out newlines.
 *
 * FIXME: after newline, strip leading spaces.
 * This only happens (I think?) when pasting metadata.
 */
void
hesc_nroff(hbuf *ob, const char *data, 
	size_t size, int span, int oneline)
{
	size_t	 i = 0, mark, slash;

	if (oneline) {
		hesc_nroff_oneline(ob, data, size, span);
		return;
	}

	while (1) {
		slash = 0;
		for (mark = i; i < size; i++) {
			if ('\\' == data[i]) {
				slash = 1;
				break;
			}
			if (i > 0 && '.' == data[i] &&
			    '\n' == data[i - 1])
				break;
			if (0 == span && i == 0 && '.' == data[i])
				break;
		}

		if (mark == 0 && i >= size) {
			hbuf_put(ob, data, size);
			return;
		}

		if (i > mark)
			hbuf_put(ob, data + mark, i - mark);

		if (i >= size)
			break;

		if (slash)
			hbuf_puts(ob, "\\e");
		else
			hbuf_puts(ob, "\\&.");
		i++;
	}
}
