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
#include "config.h"

#if HAVE_SYS_QUEUE
# include <sys/queue.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lowdown.h"
#include "extern.h"

/*
 * Escape LaTeX special characters.
 * Return zero on failure (memory), non-zero on success.
 */
int
lowdown_latex_esc(struct lowdown_buf *ob, const char *data, size_t sz)
{
	size_t	 i;

	for (i = 0; i < sz; i++)
		switch (data[i]) {
		case '&':
		case '%':
		case '$':
		case '#':
		case '_':
		case '{':
		case '}':
			if (!hbuf_putc(ob, '\\'))
				return 0;
			if (!hbuf_putc(ob, data[i]))
				return 0;
			break;
		case '~':
			if (!HBUF_PUTSL(ob, "\\textasciitilde{}"))
				return 0;
			break;
		case '^':
			if (!HBUF_PUTSL(ob, "\\textasciicircum{}"))
				return 0;
			break;
		case '\\':
			if (!HBUF_PUTSL(ob, "\\textbackslash{}"))
				return 0;
			break;
		default:
			if (!hbuf_putc(ob, data[i]))
				return 0;
			break;
		}

	return 1;
}

