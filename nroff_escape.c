/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
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
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

/*
 * Escape unsafe text into roff output such that no roff features are
 * invoked by the text (macros, escapes, etc.).
 * If "oneline" is non-zero, newlines are replaced with spaces.
 * If "literal", doesn't strip leading space.
 * Return zero on failure, non-zero on success.
 */
int
lowdown_nroff_esc(struct lowdown_buf *ob, const char *data, size_t size,
    int oneline, int literal)
{
	size_t	 	i = 0;

	if (size == 0)
		return 1;

	/* Strip leading whitespace. */

	if (!literal && ob->size > 0 && ob->data[ob->size - 1] == '\n')
		while (i < size && (data[i] == ' ' || data[i] == '\n'))
			i++;

	/*
	 * According to mandoc_char(7), we need to escape the backtick,
	 * single apostrophe, and tilde or else they'll be considered as
	 * special Unicode output.
	 * Slashes need to be escaped too.
	 * We also escape double-quotes because this text might be used
	 * within quoted macro arguments.
	 */

	for ( ; i < size; i++)
		switch (data[i]) {
		case '^':
			if (!HBUF_PUTSL(ob, "\\(ha"))
				return 0;
			break;
		case '~':
			if (!HBUF_PUTSL(ob, "\\(ti"))
				return 0;
			break;
		case '`':
			if (!HBUF_PUTSL(ob, "\\(ga"))
				return 0;
			break;
		case '"':
			if (!HBUF_PUTSL(ob, "\\(dq"))
				return 0;
			break;
		case '\n':
			if (!hbuf_putc(ob, oneline ? ' ' : '\n'))
				return 0;
			if (literal)
				break;

			/* Prevent leading spaces on the line. */

			while (i + 1 < size && 
			       (data[i + 1] == ' ' || 
				data[i + 1] == '\n'))
				i++;
			break;
		case '\\':
			if (!HBUF_PUTSL(ob, "\\e"))
				return 0;
			break;
		case '\'':
		case '.':
			if (!oneline &&
			    ob->size > 0 && 
			    ob->data[ob->size - 1] == '\n' &&
			    !HBUF_PUTSL(ob, "\\&"))
				return 0;
			/* FALLTHROUGH */
		default:
			if (!hbuf_putc(ob, data[i]))
				return 0;
			break;
		}

	return 1;
}
