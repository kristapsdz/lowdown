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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

/*
 * Convert the "$Date$" string to a simple ISO date in a
 * static buffer.
 * Returns NULL if the string is malformed at all or the date otherwise.
 */
char *
rcsdate2str(const char *v)
{
	unsigned int	y, m, d, h, min, s;
	int		rc;
	static char	buf[32];

	if (v == NULL || strlen(v) < 12)
		return NULL;

	/* Escaped dollar sign. */

	if ('\\' == v[0])
		v++;
	
	/* Date and perforce datetime. */

	if (strncmp(v, "$Date: ", 7) == 0)
		v += 7;
	else if (strncmp(v, "$DateTime: ", 11) == 0)
		v += 11;
	else
		return NULL;

	/* 
	 * Try for long and short format dates.
	 * Use regular forward slash and HTML escapes.
	 */

	rc = sscanf(v, "%u/%u/%u %u:%u:%u", 
		&y, &m, &d, &h, &min, &s);
	if (rc != 6)
		rc = sscanf(v, "%u&#47;%u&#47;%u %u:%u:%u", 
			&y, &m, &d, &h, &min, &s);
	if (rc != 6) {
		rc = sscanf(v, "%u/%u/%u", &y, &m, &d);
		if (rc != 3)
			rc = sscanf(v, "%u&#47;%u&#47;%u", &y, &m, &d);
		if (rc != 3)
			return NULL;
	}

	snprintf(buf, sizeof(buf), "%u-%.2u-%.2u", y, m, d);
	return buf;
}

/*
 * Convert the "$Author$" string to just the author in a static
 * buffer of a fixed length.
 * Returns NULL if the string is malformed (too long, too short, etc.)
 * at all or the author name otherwise.
 */
char *
rcsauthor2str(const char *v)
{
	static char	buf[1024];
	size_t		sz;

	if (v == NULL || strlen(v) < 12)
		return NULL;

	/* Check for LaTeX. */

	if ('\\' == v[0])
		v++;

	if (strncmp(v, "$Author: ", 9))
		return NULL;
	if ((sz = strlcpy(buf, v + 9, sizeof(buf))) >= sizeof(buf))
		return NULL;

	/* Strip end (with LaTeX). */

	if (sz && buf[sz - 1] == '$') {
		buf[--sz] = '\0';
		if (sz && buf[sz - 1] == '\\')
			buf[--sz] = '\0';
		if (sz && buf[sz - 1] == ' ')
			buf[--sz] = '\0';
	}

	return buf;
}

struct lowdown_meta *
lowdown_get_meta(const struct lowdown_node *n, struct lowdown_metaq *mq)
{
	struct lowdown_meta		*m, *ret = NULL;
	struct lowdown_buf		*ob = NULL;
	const struct lowdown_node	*child;
	const struct rndr_meta		*params = &n->rndr_meta;

	assert(n->type == LOWDOWN_META);

	if ((m = calloc(1, sizeof(struct lowdown_meta))) == NULL)
		goto out;
	TAILQ_INSERT_TAIL(mq, m, entries);

	m->key = strndup(params->key.data, params->key.size);
	if (m->key == NULL)
		goto out;

	/*
	 * (Re-)Concatenate all child text, but discard any escaping,
	 * which is handled when the metadata is written to output.
	 */

	if ((ob = hbuf_new(32)) == NULL)
		goto out;
	TAILQ_FOREACH(child, &n->children, entries) {
		assert(child->type == LOWDOWN_NORMAL_TEXT);
		if (!hbuf_putb(ob, &child->rndr_normal_text.text))
			goto out;
	}
	m->value = ob->size == 0 ?
		strdup("") : strndup(ob->data, ob->size);
	if (m->value == NULL)
		goto out;

	ret = m;
out:
	hbuf_free(ob);
	return ret;
}
