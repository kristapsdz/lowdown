/*	$Id$ */
/*
 * Copyright (c) 2017, Kristaps Dzonsons
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "lowdown.h"
#include "extern.h"

#define DEF_IUNIT 1024
#define DEF_OUNIT 64
#define DEF_MAX_NESTING 16

static	const char *const errs[LOWDOWN_ERR__MAX] = {
	"space before link (CommonMark violation)",
	"bad character in metadata key (MultiMarkdown violation)",
};

const char *
lowdown_errstr(enum lowdown_err err)
{

	return(errs[err]);
}

void
lowdown_buf(const struct lowdown_opts *opts,
	const unsigned char *data, size_t datasz,
	unsigned char **res, size_t *rsz,
	struct lowdown_meta **m, size_t *msz)
{
	hbuf	 	*ob, *spb;
	hrend 		*renderer = NULL;
	hdoc 		*document;
	size_t		 i;

	/*
	 * Begin by creating our buffers, renderer, and document.
	 */

	ob = hbuf_new(DEF_OUNIT);

	renderer = NULL == opts || LOWDOWN_HTML == opts->type ?
		hrend_html_new
		(NULL == opts ? 0 : opts->oflags, 0) :
		hrend_nroff_new(opts->oflags, 
			LOWDOWN_MAN == opts->type);

	document = hdoc_new
		(renderer, opts, NULL == opts ?
		 0 : opts->feat, DEF_MAX_NESTING,
		 NULL != opts &&
		 LOWDOWN_HTML != opts->type);

	/* Parse the output and free resources. */

	hdoc_render(document, ob, data, datasz, m, msz);
	hdoc_free(document);

	if (NULL == opts || LOWDOWN_HTML == opts->type)
		hrend_html_free(renderer);
	else
		hrend_nroff_free(renderer);

	/*
	 * Now we escape all of our metadata values.
	 * This may not be standard (?), but it's required: we generally
	 * include metadata into our documents, and if not here, we'd
	 * leave escaping to our caller.
	 * Which we should never do!
	 */

	for (i = 0; i < *msz; i++) {
		spb = hbuf_new(DEF_OUNIT);
		if (NULL == opts ||
		    LOWDOWN_HTML == opts->type)
			hesc_html(spb, (uint8_t *)(*m)[i].value, 
				strlen((*m)[i].value), 0);
		else
			hesc_nroff(spb, (uint8_t *)(*m)[i].value, 
				strlen((*m)[i].value), 0, 1);
		free((*m)[i].value);
		(*m)[i].value = xstrndup
			((char *)spb->data, spb->size);
		hbuf_free(spb);
	}

	/* Reprocess the output as smartypants. */

	if (NULL != opts && 
	    LOWDOWN_SMARTY & opts->oflags) {
		spb = hbuf_new(DEF_OUNIT);
		if (LOWDOWN_HTML == opts->type)
			hsmrt_html(spb, ob->data, ob->size);
		else
			hsmrt_nroff(spb, ob->data, ob->size);
		*res = spb->data;
		*rsz = spb->size;
		spb->data = NULL;
		hbuf_free(spb);
		for (i = 0; i < *msz; i++) {
			spb = hbuf_new(DEF_OUNIT);
			if (NULL == opts ||
			    LOWDOWN_HTML == opts->type)
				hsmrt_html(spb, 
					(uint8_t *)(*m)[i].value, 
					strlen((*m)[i].value));
			else
				hsmrt_nroff(spb, 
					(uint8_t *)(*m)[i].value, 
					strlen((*m)[i].value));
			free((*m)[i].value);
			(*m)[i].value = xstrndup
				((char *)spb->data, spb->size);
			hbuf_free(spb);
		}
	} else {
		*res = ob->data;
		*rsz = ob->size;
		ob->data = NULL;
	}
	hbuf_free(ob);
}

int
lowdown_file(const struct lowdown_opts *opts,
	FILE *fin, unsigned char **res, size_t *rsz,
	struct lowdown_meta **m, size_t *msz)
{
	hbuf *ib = NULL;

	ib = hbuf_new(DEF_IUNIT);

	if (hbuf_putf(ib, fin)) {
		hbuf_free(ib);
		return(0);
	}

	lowdown_buf(opts, ib->data, ib->size, res, rsz, m, msz);
	hbuf_free(ib);
	return(1);
}

/*
 * Convert an ISO date (y/m/d or y-m-d) to a canonical form.
 * Returns NULL if the string is malformed at all or the date otherwise.
 */
static char *
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

/*
 * Convert the "$Author$" string to just the author in a static
 * buffer of a fixed length.
 * Returns NULL if the string is malformed (too long, too short, etc.)
 * at all or the author name otherwise.
 */
static char *
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
 * Convert the "$Date$" string to a simple ISO date in a
 * static buffer.
 * Returns NULL if the string is malformed at all or the date otherwise.
 */
static char *
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

void
lowdown_standalone_open(const struct lowdown_opts *opts,
	const struct lowdown_meta *m, size_t msz,
	unsigned char **res, size_t *rsz)
{
	const char	*date = NULL, *author = NULL,
	      		*title = "Untitled article";
	time_t		 t;
	char		 buf[32];
	struct tm	*tm;
	size_t		 i;
	hbuf		*op;

	op = hbuf_new(DEF_OUNIT);

	/* Acquire metadata that we'll fill in. */

	for (i = 0; i < msz; i++) 
		if (0 == strcmp(m[i].key, "title"))
			title = m[i].value;
		else if (0 == strcmp(m[i].key, "author"))
			author = m[i].value;
		else if (0 == strcmp(m[i].key, "rcsauthor"))
			author = rcsauthor2str(m[i].value);
		else if (0 == strcmp(m[i].key, "rcsdate"))
			date = rcsdate2str(m[i].value);
		else if (0 == strcmp(m[i].key, "date"))
			date = date2str(m[i].value);

	/* FIXME: convert to buf without strftime. */

	if (NULL == date) {
		t = time(NULL);
		tm = localtime(&t);
		strftime(buf, sizeof(buf), "%F", tm);
		date = buf;
	}

	switch (opts->type) {
	case LOWDOWN_HTML:
		HBUF_PUTSL(op, 
		      "<!DOCTYPE html>\n"
		      "<html>\n"
		      "<head>\n"
		      "<meta charset=\"utf-8\" />\n"
		      "<meta name=\"viewport\" content=\""
		       "width=device-width,initial-scale=1\" />\n");
		if (NULL != author) {
			HBUF_PUTSL(op, "<meta name=\"author\" content=\"");
			hbuf_puts(op, author);
			HBUF_PUTSL(op, "\" />\n");
		}
		HBUF_PUTSL(op, "<title>");
		hbuf_puts(op, title);
		HBUF_PUTSL(op, 
		      "</title>\n"
		      "</head>\n"
		      "<body>\n");
		break;
	case LOWDOWN_NROFF:
		hbuf_printf(op, ".DA %s\n.TL\n", date);
		hbuf_puts(op, title);
		HBUF_PUTSL(op, "\n");
		if (NULL != author) {
			HBUF_PUTSL(op, ".AU\n");
			hbuf_puts(op, author);
			HBUF_PUTSL(op, "\n");
		}
		break;
	case LOWDOWN_MAN:
		HBUF_PUTSL(op, ".TH \"");
		hbuf_puts(op, title);
		hbuf_printf(op, "\" 7 %s\n", date);
		break;
	}

	*res = op->data;
	*rsz = op->size;

	op->data = NULL;
	hbuf_free(op);
}

void
lowdown_standalone_close(const struct lowdown_opts *opts,
	unsigned char **res, size_t *rsz)
{
	hbuf	*op;

	op = hbuf_new(DEF_OUNIT);

	switch (opts->type) {
	case LOWDOWN_HTML:
		HBUF_PUTSL(op, "</body>\n</html>\n");
		break;
	default:
		break;
	}

	*res = op->data;
	*rsz = op->size;

	op->data = NULL;
	hbuf_free(op);
}
