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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

/*
 * Convert newlines to spaces (if "oneline") and elide control
 * characters.  If "oneline" and a newline follows a period, it's
 * converted to two spaces.  Otherwise, just one space.
 * Return zero on failure (memory), non-zero on success.
 */
int
lowdown_gemini_esc(struct lowdown_buf *ob, const char *buf, size_t sz,
    int oneline)
{
	size_t	 	 i, start = 0;
	unsigned char	 ch;

	for (i = 0; i < sz; i++) {
		ch = (unsigned char)buf[i];
		if (ch == '\n' && oneline) {
			if (!hbuf_put(ob, buf + start, i - start))
				return 0;
			if (ob->size && 
			    ob->data[ob->size - 1] == '.' &&
			    !hbuf_putc(ob, ' '))
				return 0;
			if (!hbuf_putc(ob, ' '))
				return 0;
			start = i + 1;
		} else if (ch < 0x80 && iscntrl(ch)) {
			if (!hbuf_put(ob, buf + start, i - start))
				return 0;
			start = i + 1;
		}
	}

	if (start < sz && !hbuf_put(ob, buf + start, sz - start))
		return 0;
	return 1;
}
