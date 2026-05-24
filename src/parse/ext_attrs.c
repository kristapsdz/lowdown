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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"
#include "parse.h"

/*
 * Standard HTML attributes.
 */
static const char *strattrs[] = {
	"id", /* LOWDOWN_ATTR_ID */
	"class", /* LOWDOWN_ATTR_CLASS */
	"crossorigin", /* LOWDOWN_ATTR_CROSSORIGIN */
	"referrerpolicy", /* LOWDOWN_ATTR_REFERRERPOLICY */
	"ismap", /* LOWDOWN_ATTR_ISMAP */
	"usemap", /* LOWDOWN_ATTR_USEMAP */
	"sizes", /* LOWDOWN_ATTR_SIZES */
	"srcset", /* LOWDOWN_ATTR_SRCSET */
	"width", /* LOWDOWN_ATTR_WIDTH */
	"height", /* LOWDOWN_ATTR_HEIGHT */
	"rel", /* LOWDOWN_ATTR_REL */
	"target", /* LOWDOWN_ATTR_TARGET */
	"download", /* LOWDOWN_ATTR_DOWLOAD */
	"loading", /* LOWDOWN_ATTR_LOADING */
	"ping", /* LOWDOWN_ATTR_PING */
	NULL, /* LOWDOWN_ATTR_CUSTOM */
};

/*
 * Free all attributes and their keys, including the "attrs" pointer as
 * well.
 */
void
lowdown_attrs_free(struct lowdown_attr *attrs, size_t attrsz)
{
	size_t	 i;

	for (i = 0; i < attrsz; i++) {
		free(attrs[i].key);
		hbuf_free(attrs[i].value);
	}
	free(attrs);
}

/*
 * Add an HTML attribute named "key" with value "val" to the list of
 * attributes attached to a node.
 * The attribute list is guaranteed to be either of size 0 or at least
 * LOWDOWN_ATTR_CUSTOM.
 * This makes it possible to do instant lookups on known attributes,
 * while allowing iteration over un-known attributes.
 * If the attribute is a custom (unrecognised), duplicates are allowed;
 * if the attribute is a class, it's appended to the existing classes;
 * and otherwise, the attribute replaces what's currently there.
 * Returns FALSE on failure (memory), TRUE on success.
 */
static int
lowdown_attrs_add(struct lowdown_attr **attrs, size_t *attrsz,
    enum lowdown_attr_type type, const char *typestr, size_t typesz,
    const char *val, size_t valsz)
{
	void	*p;
	char	*key = NULL;

	/* Strip surrounding quotes, if applicable. */

	if (valsz > 2 &&
	    val[0] == '"' &&
	    val[valsz - 1] == '"') {
		valsz -= 2;
		val++;
	}

	if (typestr != NULL) {
		if ((key = strndup(typestr, typesz)) == NULL)
			return 0;
		for (type = 0; type != LOWDOWN_ATTR_CUSTOM; type++)
			if (strcmp(key, strattrs[type]) == 0)
				break;
	} else {
		assert(type == LOWDOWN_ATTR_ID ||
			type == LOWDOWN_ATTR_CLASS);
		if (type == LOWDOWN_ATTR_ID)
			key = strdup("id");
		else if (type == LOWDOWN_ATTR_CLASS)
			key = strdup("class");
		if (key == NULL)
			return 0;
	}

	/* Initialise all known types. */

	if (*attrsz == 0) {
		*attrsz = LOWDOWN_ATTR_CUSTOM;
		*attrs = calloc(*attrsz, sizeof(struct lowdown_attr));
		if (*attrs == NULL) {
			free(key);
			return 0;
		}
	}

	/* If a custom attribute, append to the array, allowing dupes. */

	if (type == LOWDOWN_ATTR_CUSTOM) {
		p = recallocarray(*attrs, *attrsz, *attrsz + 1,
			sizeof(struct lowdown_attr));
		if (p == NULL) {
			free(key);
			return 0;
		}
		*attrs = p;
		(*attrsz)++;
		(*attrs)[*attrsz - 1].key = key;
		(*attrs)[*attrsz - 1].value = hbuf_strndup(val, valsz);
		return (*attrs)[*attrsz - 1].value != NULL;
	}

	/* The "class" attribute is appended to; others are replaced. */

	free((*attrs)[type].key);
	(*attrs)[type].key = key;

	if (type == LOWDOWN_ATTR_CLASS) {
		if ((*attrs)[type].value != NULL &&
		    (!HBUF_PUTSL((*attrs)[type].value, " ") ||
		     !hbuf_put((*attrs)[type].value, val, valsz)))
			return 0;
		if ((*attrs)[type].value == NULL &&
		    ((*attrs)[type].value =
		     hbuf_strndup(val, valsz)) == NULL)
			return 0;
	} else {
		hbuf_free((*attrs)[type].value);
		if (((*attrs)[type].value =
		    hbuf_strndup(val, valsz)) == NULL)
			return 0;
	}

	return 1;
}

/*
 * Parse attributes from the buffer "data".  The buffer should not have
 * any enclosing characters, e.g., { foo }.  Return 0 on failure or
 * position of *next* word.
 */
size_t
lowdown_attrs_parse(const char *data, size_t size,
    struct lowdown_attr **attrs, size_t *attrsz)
{
	size_t	 word_b = 0, word_e, i;
	int	 in_quot;

	while (word_b < size) {
		while (word_b < size && data[word_b] == ' ')
			word_b++;
		word_e = word_b;
		in_quot = 0;
		while (word_e < size) {
			if (data[word_e] == '"')
				in_quot = !in_quot;
			if (!in_quot && data[word_e] == ' ')
				break;
			word_e++;
		}

		if (word_e > word_b + 1 &&
		    data[word_b] == '#' &&
		    !lowdown_attrs_add(attrs, attrsz,
		     LOWDOWN_ATTR_ID, NULL, 0,
		     data + word_b + 1, word_e - word_b - 1))
			return 0;

		if (word_e > word_b + 1 &&
		    data[word_b] == '.' &&
		    !lowdown_attrs_add(attrs, attrsz,
		     LOWDOWN_ATTR_CLASS, NULL, 0,
		     data + word_b + 1, word_e - word_b - 1))
			return 0;

		for (i = word_b; i < word_e; i++) {
			if (data[i] != '=')
				continue;
			if (!lowdown_attrs_add(attrs, attrsz,
			    0, &data[word_b], i - word_b,
			    data + i + 1, word_e - i - 1))
				return 0;
		}

		word_b = word_e + 1;
	}

	return word_b;
}
