/*	$Id$ */
/*
 * Copyright (c) 2017 Kristaps Dzonsons <kristaps@bsd.lv>
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

	if (NULL == v ||
	    strlen(v) < 10 ||
	    strncmp(v, "$Date: ", 7))
		return(NULL);

	rc = sscanf(v + 7, "%u/%u/%u %u:%u:%u", 
		&y, &m, &d, &h, &min, &s);

	if (6 != rc)
		return(NULL);

	snprintf(buf, sizeof(buf), "%u-%.2u-%.2u", y, m, d);
	return(buf);
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

	if (NULL == v ||
	    strlen(v) < 12 ||
	    strncmp(v, "$Author: ", 9))
		return(NULL);

	if ((sz = strlcpy(buf, v + 9, sizeof(buf))) >= sizeof(buf))
		return(NULL);

	if ('$' == buf[sz - 1])
		buf[sz - 1] = '\0';
	if (' ' == buf[sz - 2])
		buf[sz - 2] = '\0';

	return(buf);
}

/*
 * Convert an ISO date (y/m/d or y-m-d) to a canonical form.
 * Returns NULL if the string is malformed at all or the date otherwise.
 */
char *
date2str(const char *v)
{
	unsigned int	y, m, d;
	int		rc;
	static char	buf[32];

	if (NULL == v)
		return(NULL);

	rc = sscanf(v, "%u/%u/%u", &y, &m, &d);
	if (3 != rc) {
		rc = sscanf(v, "%u-%u-%u", &y, &m, &d);
		if (3 != rc)
			return(NULL);
	}

	snprintf(buf, sizeof(buf), "%u-%.2u-%.2u", y, m, d);
	return(buf);
}

