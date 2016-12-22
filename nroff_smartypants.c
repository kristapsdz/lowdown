/*	$Id$ */
/*
 * Copyright (c) 2008, Natacha Porté
 * Copyright (c) 2011, Vicent Martí
 * Copyright (c) 2014, Xavier Mendez, Devin Torres and the Hoedown authors
 * Copyright (c) 2016, Kristaps Dzonsons
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
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

struct 	sm_dat {
	int 	 in_squote;
	int 	 in_dquote;
};

typedef	size_t (*sm_cb_ptr)(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);

static size_t sm_cb_dquote(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);
static size_t sm_cb_amp(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);
static size_t sm_cb_number(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);
static size_t sm_cb_dash(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);
static size_t sm_cb_parens(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);
static size_t sm_cb_squote(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);
static size_t sm_cb_backtick(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);
static size_t sm_cb_dot(hbuf *, struct sm_dat *, uint8_t, const uint8_t *, size_t);

static	sm_cb_ptr sm_cb_ptrs[] = {
	NULL,		/* 0 */
	sm_cb_dash,	/* 1 */
	sm_cb_parens,	/* 2 */
	sm_cb_squote,	/* 3 */
	sm_cb_dquote,	/* 4 */
	sm_cb_amp,	/* 5 */
	NULL,		/* 6 */
	sm_cb_number,	/* 7 */
	sm_cb_dot,	/* 8 */
	sm_cb_backtick, /* 9 */
	NULL,		/* 10 */
};

static const uint8_t sm_cb_chars[UINT8_MAX+1] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* nul -- si */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* dle -- us */
	0, 0, 4, 0, 0, 0, 5, 3, 2, 0, 0, 0, 0, 1, 8, 0, /* sp -- / */
	0, 7, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0 -- ? */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* @ -- O */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* P -- _ */
	9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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

static int
word_boundary(uint8_t c)
{
	return c == 0 || isspace(c) || ispunct(c);
}

/*
 * If 'text' begins with any kind of single quote (e.g. "'" or "&apos;"
 * etc.), returns the length of the sequence of characters that makes up
 * the single- quote.  Otherwise, returns zero.
 */
static size_t
squote_len(const uint8_t *text, size_t size)
{
	static const char* single_quote_list[] = { "'", "&#39;", "&#x27;", "&apos;", NULL };
	const char** p;

	for (p = single_quote_list; *p; ++p) {
		size_t len = strlen(*p);
		if (size >= len && memcmp(text, *p, len) == 0) {
			return len;
		}
	}

	return 0;
}

/* 
 * Converts " or ' at very beginning or end of a word to left or right
 * quote 
 */
static int
smartypants_quotes(hbuf *ob, uint8_t previous_char, uint8_t next_char, uint8_t quote, int *is_open)
{

	if (*is_open && !word_boundary(next_char))
		return 0;

	if (!(*is_open) && !word_boundary(previous_char))
		return 0;

	assert('d' == quote || 's' == quote);

	if ('d' == quote)
		hbuf_puts(ob, *is_open ? "\\(rq" : "\\(lq");
	else
		hbuf_puts(ob, *is_open ? "\\(cq" : "\\(oq");

	*is_open = !(*is_open);
	return 1;
}

/*
 * Converts ' to left or right single quote; but the initial ' might be
 * in different forms, e.g. &apos; or &#39; or &#x27;.
 * 'squote_text' points to the original single quote, and 'squote_size'
 * is its length.  'text' points at the last character of the
 * single-quote, e.g. ' or ;
 */
static size_t
smartypants_squote(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size,
				   const uint8_t *squote_text, size_t squote_size)
{
	uint8_t	 t1, t2, next_char;
	size_t	 next_squote_len;

	if (size >= 2) {
		t1 = tolower((int)text[1]);
		next_squote_len = squote_len(text+1, size-1);

		/* convert '' to &ldquo; or &rdquo; */
		if (next_squote_len > 0) {
			next_char = (size > 1+next_squote_len) ? 
				text[1+next_squote_len] : 0;
			if (smartypants_quotes(ob, previous_char, 
			    next_char, 'd', &smrt->in_dquote))
				return next_squote_len;
		}

		/* Tom's, isn't, I'm, I'd */
		if ((t1 == 's' || t1 == 't' || t1 == 'm' || 
		     t1 == 'd') && (size == 3 || 
		    word_boundary(text[2]))) {
			HOEDOWN_BUFPUTSL(ob, "\\(cq");
			return 0;
		}

		/* you're, you'll, you've */
		if (size >= 3) {
			t2 = tolower((int)text[2]);
			if (((t1 == 'r' && t2 == 'e') ||
		   	     (t1 == 'l' && t2 == 'l') ||
			     (t1 == 'v' && t2 == 'e')) &&
			    (size == 4 || word_boundary(text[3]))) {
				HOEDOWN_BUFPUTSL(ob, "\\(cq");
				return 0;
			}
		}
	}

	if (smartypants_quotes(ob, previous_char, 
	    size > 0 ? text[1] : 0, 's', &smrt->in_squote))
		return 0;

	hbuf_put(ob, squote_text, squote_size);
	return 0;
}

/* 
 * Converts ' to left or right single quote. 
 */
static size_t
sm_cb_squote(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	return smartypants_squote(ob, smrt, 
		previous_char, text, size, text, 1);
}

/* 
 * Converts (c), (r), (tm) 
 */
static size_t
sm_cb_parens(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	uint8_t	 t1, t2;

	if (size >= 3) {
		t1 = tolower((int)text[1]);
		t2 = tolower((int)text[2]);

		if (t1 == 'c' && t2 == ')') {
			HOEDOWN_BUFPUTSL(ob, "\\(co");
			return 2;
		}

		if (t1 == 'r' && t2 == ')') {
			HOEDOWN_BUFPUTSL(ob, "\\(rg");
			return 2;
		}

		if (size >= 4 && t1 == 't' && 
		    t2 == 'm' && text[3] == ')') {
			HOEDOWN_BUFPUTSL(ob, "\\(tm");
			return 3;
		}
	}

	hbuf_putc(ob, text[0]);
	return 0;
}

/* 
 * Converts "--" to em-dash, etc. 
 */
static size_t
sm_cb_dash(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{

	if (size >= 3 && text[1] == '-' && text[2] == '-') {
		HOEDOWN_BUFPUTSL(ob, "\\(em");
		return 2;
	}

	if (size >= 2 && text[1] == '-') {
		HOEDOWN_BUFPUTSL(ob, "\\(en");
		return 1;
	}

	hbuf_putc(ob, text[0]);
	return 0;
}

/* 
 * Converts &quot; etc. 
 */
static size_t
sm_cb_amp(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	size_t	len;

	if (size >= 6 && memcmp(text, "&quot;", 6) == 0) {
		if (smartypants_quotes(ob, previous_char, 
		    size >= 7 ? text[6] : 0, 'd', &smrt->in_dquote))
			return 5;
	}

	len = squote_len(text, size);
	if (len > 0)
		return (len-1) + smartypants_squote(ob, smrt, 
			previous_char, text+(len-1), 
			size-(len-1), text, len);

	if (size >= 4 && memcmp(text, "&#0;", 4) == 0)
		return 3;

	hbuf_putc(ob, '&');
	return 0;
}

static size_t
sm_cb_dot(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{
	size_t	 	 i = 0;
	const uint8_t	*cp;

	/* FIXME: code span */

	if ((0 == previous_char || '\n' == previous_char) && 
	    (size >= 3 && 0 == memcmp(text + 1, "DS\n", 3))) {
		i = 3;
		hbuf_put(ob, text, i);
		cp = memmem(text + i, size - i, "\n.DE\n", 4);
		assert(NULL != cp);
		hbuf_put(ob, text + i, cp - (text + i));
		i += cp - (text + i) - 1;
	} else
		hbuf_putc(ob, text[0]);

	return i;
}

/* 
 * Converts `` to opening double quote.
 */
static size_t
sm_cb_backtick(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{

	if (size >= 2 && text[1] == '`') {
		if (smartypants_quotes(ob, previous_char, 
		    size >= 3 ? text[2] : 0, 'd', &smrt->in_dquote))
			return 1;
	}

	hbuf_putc(ob, text[0]);
	return 0;
}

/* Converts 1/2, 1/4, 3/4 */
static size_t
sm_cb_number(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{

	if (word_boundary(previous_char) && size >= 3) {
		/* 1/2 */
		if (text[0] == '1' && 
		    text[1] == '/' && text[2] == '2') {
			if (size == 3 || word_boundary(text[3])) {
				HOEDOWN_BUFPUTSL(ob, "\\[12]");
				return 2;
			}
		}
		/* 1/4 */
		if (text[0] == '1' && 
		    text[1] == '/' && text[2] == '4') {
			if (size == 3 || word_boundary(text[3]) ||
			    (size >= 5 && 
			     tolower((int)text[3]) == 't' && 
			     tolower((int)text[4]) == 'h')) {
				HOEDOWN_BUFPUTSL(ob, "\\[14]");
				return 2;
			}
		}
		/* 3/4 */
		if (text[0] == '3' && 
		    text[1] == '/' && text[2] == '4') {
			if (size == 3 || word_boundary(text[3]) ||
			    (size >= 6 && 
			     tolower((int)text[3]) == 't' && 
			     tolower((int)text[4]) == 'h' && 
			     tolower((int)text[5]) == 's')) {
				HOEDOWN_BUFPUTSL(ob, "\\[34]");
				return 2;
			}
		}
	}

	hbuf_putc(ob, text[0]);
	return 0;
}

/* 
 * Converts " to left or right double quote.
 */
static size_t
sm_cb_dquote(hbuf *ob, struct sm_dat *smrt, uint8_t previous_char, const uint8_t *text, size_t size)
{

	if ( ! smartypants_quotes(ob, previous_char, 
	    size > 0 ? text[1] : 0, 'd', &smrt->in_dquote))
		HOEDOWN_BUFPUTSL(ob, "\\(dq");

	return 0;
}

void
hoedown_nroff_smartypants(hbuf *ob, const uint8_t *text, size_t size)
{
	size_t 		 i, org;
	struct sm_dat	 smrt;
	uint8_t		 action = 0;

	if (NULL == text || 0 == size)
		return;

	memset(&smrt, 0, sizeof(struct sm_dat));

	hbuf_grow(ob, size);

	for (i = 0; i < size; ++i) {
		action = 0;

		org = i;
		while (i < size && (action = sm_cb_chars[text[i]]) == 0)
			i++;

		if (i > org)
			hbuf_put(ob, text + org, i - org);

		if (i < size)
			i += sm_cb_ptrs[(int)action](ob, 
				&smrt, i ? text[i - 1] : 0, 
				text + i, size - i);
	}
}
