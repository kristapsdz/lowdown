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
#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

#define REF_TABLE_SIZE	8
#define BUFFER_BLOCK	0
#define BUFFER_SPAN	1
#define HOEDOWN_LI_END	8 /* internal list flag */

/* Reference to a link. */
struct link_ref {
	unsigned int	 id;
	hbuf		*link;
	hbuf		*title;
	struct link_ref	*next;
};

/* Feference to a footnote. */
struct footnote_ref {
	unsigned int	 id;
	int		 is_used;
	unsigned int	 num;
	hbuf		*contents;
};

/* An item in a footnote_list. */
struct footnote_item {
	struct footnote_ref *ref;
	struct footnote_item *next;
};

/* Linked list of footnote_item. */
struct footnote_list {
	unsigned int count;
	struct footnote_item *head;
	struct footnote_item *tail;
};

/*
 * Function pointer to render active chars.
 * Returns the number of chars taken care of.
 * "data" is the pointer of the beginning of the span.
 * "offset" is the number of valid chars before data.
 */
typedef size_t (*char_trigger)(hbuf *ob, hdoc *doc,
	uint8_t *data, size_t offset, size_t size, int nln);

static size_t char_emphasis(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_linebreak(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_codespan(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_escape(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_entity(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_langle_tag(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_autolink_url(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_autolink_email(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_autolink_www(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_link(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_image(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_superscript(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);
static size_t char_math(hbuf *, hdoc *, uint8_t *, size_t, size_t, int);

enum markdown_char_t {
	MD_CHAR_NONE = 0,
	MD_CHAR_EMPHASIS,
	MD_CHAR_CODESPAN,
	MD_CHAR_LINEBREAK,
	MD_CHAR_LINK,
	MD_CHAR_IMAGE,
	MD_CHAR_LANGLE,
	MD_CHAR_ESCAPE,
	MD_CHAR_ENTITY,
	MD_CHAR_AUTOLINK_URL,
	MD_CHAR_AUTOLINK_EMAIL,
	MD_CHAR_AUTOLINK_WWW,
	MD_CHAR_SUPERSCRIPT,
	MD_CHAR_QUOTE,
	MD_CHAR_MATH
};

static char_trigger markdown_char_ptrs[] = {
	NULL,
	&char_emphasis,
	&char_codespan,
	&char_linebreak,
	&char_link,
	&char_image,
	&char_langle_tag,
	&char_escape,
	&char_entity,
	&char_autolink_url,
	&char_autolink_email,
	&char_autolink_www,
	&char_superscript,
	NULL,
	&char_math
};

struct 	hdoc {
	hrend		 md;
	void		*data;
	const uint8_t	*start;
	const struct lowdown_opts *opts;
	struct link_ref	*refs[REF_TABLE_SIZE];
	struct footnote_list footnotes_found;
	struct footnote_list footnotes_used;
	uint8_t		 active_char[256];
	hstack		 work_bufs[2];
	unsigned int	 ext_flags;
	size_t		 max_nesting;
	size_t	 	 cur_par;
	int		 in_link_body;
	int		 link_nospace;
};

/* Some forward declarations. */

static void parse_block(hbuf *, hdoc *, uint8_t *, size_t);

static int
buf_newln(hbuf *buf)
{

	assert(NULL != buf);
	return(0 == buf->size || '\n' == buf->data[buf->size - 1]);
}

static hbuf *
newbuf(hdoc *doc, int type)
{
	static const size_t buf_size[2] = {256, 64};
	hbuf *work = NULL;
	hstack *pool = &doc->work_bufs[type];

	if (pool->size < pool->asize &&
		pool->item[pool->size] != NULL) {
		work = pool->item[pool->size++];
		work->size = 0;
	} else {
		work = hbuf_new(buf_size[type]);
		hstack_push(pool, work);
	}

	return work;
}

static void
popbuf(hdoc *doc, int type)
{

	doc->work_bufs[type].size--;
}

static void
unscape_text(hbuf *ob, hbuf *src)
{
	size_t i = 0, org;

	while (i < src->size) {
		org = i;
		while (i < src->size && src->data[i] != '\\')
			i++;

		if (i > org)
			hbuf_put(ob, src->data + org, i - org);

		if (i + 1 >= src->size)
			break;

		hbuf_putc(ob, src->data[i + 1]);
		i += 2;
	}
}

static unsigned int
hash_link_ref(const uint8_t *link_ref, size_t length)
{
	size_t i;
	unsigned int hash = 0;

	for (i = 0; i < length; ++i)
		hash = tolower(link_ref[i]) +
			(hash << 6) + (hash << 16) - hash;

	return hash;
}

static struct link_ref *
add_link_ref(struct link_ref **refs,
	const uint8_t *name, size_t name_size)
{
	struct link_ref *ref = xcalloc(1, sizeof(struct link_ref));

	ref->id = hash_link_ref(name, name_size);
	ref->next = refs[ref->id % REF_TABLE_SIZE];

	refs[ref->id % REF_TABLE_SIZE] = ref;
	return ref;
}

static struct link_ref *
find_link_ref(struct link_ref **refs, uint8_t *name, size_t length)
{
	unsigned int hash = hash_link_ref(name, length);
	struct link_ref *ref = NULL;

	ref = refs[hash % REF_TABLE_SIZE];

	while (ref != NULL) {
		if (ref->id == hash)
			return ref;

		ref = ref->next;
	}

	return NULL;
}

static void
free_link_refs(struct link_ref **refs)
{
	size_t i;
	struct link_ref *r, *next;

	for (i = 0; i < REF_TABLE_SIZE; ++i) {
		r = refs[i];
		while (r) {
			next = r->next;
			hbuf_free(r->link);
			hbuf_free(r->title);
			free(r);
			r = next;
		}
	}
}

static struct footnote_ref *
create_footnote_ref(struct footnote_list *list,
	const uint8_t *name, size_t name_size)
{
	struct footnote_ref *ref;

	ref = xcalloc(1, sizeof(struct footnote_ref));
	ref->id = hash_link_ref(name, name_size);

	return ref;
}

static void
add_footnote_ref(struct footnote_list *list, struct footnote_ref *ref)
{
	struct footnote_item *item;

	item = xcalloc(1, sizeof(struct footnote_item));
	item->ref = ref;

	if (list->head == NULL) {
		list->head = list->tail = item;
	} else {
		list->tail->next = item;
		list->tail = item;
	}
	list->count++;
}

static struct footnote_ref *
find_footnote_ref(struct footnote_list *list, uint8_t *name, size_t length)
{
	unsigned int hash = hash_link_ref(name, length);
	struct footnote_item *item = NULL;

	item = list->head;

	while (item != NULL) {
		if (item->ref->id == hash)
			return item->ref;
		item = item->next;
	}

	return NULL;
}

static void
free_footnote_ref(struct footnote_ref *ref)
{

	hbuf_free(ref->contents);
	free(ref);
}

static void
free_footnote_list(struct footnote_list *list, int free_refs)
{
	struct footnote_item *item = list->head;
	struct footnote_item *next;

	while (item) {
		next = item->next;
		if (free_refs)
			free_footnote_ref(item->ref);
		free(item);
		item = next;
	}
}


/*
 * Check whether a char is a Markdown spacing char.
 * Right now we only consider spaces the actual space and a newline:
 * tabs and carriage returns are filtered out during the preprocessing
 * phase.
 * If we wanted to actually be UTF-8 compliant, we should instead
 * extract an Unicode codepoint from this character and check for space
 * properties.
 */
static int
xisspace(int c)
{

	return c == ' ' || c == '\n';
}

/*
 * Verify that all the data is spacing.
 */
static int
is_empty_all(const uint8_t *data, size_t size)
{
	size_t i = 0;

	while (i < size && xisspace(data[i]))
		i++;

	return i == size;
}

/*
 * Replace all spacing characters in data with spaces. As a special
 * case, this collapses a newline with the previous space, if possible.
 */
static void
replace_spacing(hbuf *ob, const uint8_t *data, size_t size)
{
	size_t i = 0, mark;

	hbuf_grow(ob, size);

	while (1) {
		mark = i;
		while (i < size && data[i] != '\n')
			i++;
		hbuf_put(ob, data + mark, i - mark);

		if (i >= size)
			break;

		if (!(i > 0 && data[i-1] == ' '))
			hbuf_putc(ob, ' ');
		i++;
	}
}

/*
 * Looks for the address part of a mail autolink and '>'.
 * This is less strict than the original markdown e-mail address matching.
 */
static size_t
is_mail_autolink(uint8_t *data, size_t size)
{
	size_t i = 0, nb = 0;

	/* Assumed to be: [-@._a-zA-Z0-9]+ with exactly one '@'. */

	for (i = 0; i < size; ++i) {
		if (isalnum(data[i]))
			continue;

		switch (data[i]) {
		case '@':
			nb++;
		case '-':
		case '.':
		case '_':
			break;
		case '>':
			return (nb == 1) ? i + 1 : 0;
		default:
			return 0;
		}
	}

	return 0;
}

/*
 * Returns the length of the given tag, or 0 is it's not valid.
 */
static size_t
tag_length(uint8_t *data, size_t size, halink_type *autolink)
{
	size_t i, j;

	/* A valid tag can't be shorter than 3 chars. */

	if (size < 3)
		return 0;

	if (data[0] != '<')
		return 0;

        /* HTML comment, laxist form. */

        if (size > 5 && data[1] == '!' &&
	    data[2] == '-' && data[3] == '-') {
		i = 5;
		while (i < size && !(data[i - 2] == '-' &&
		       data[i - 1] == '-' && data[i] == '>'))
			i++;
		i++;
		if (i <= size)
			return i;
        }

	/*
	 * Begins with a '<' optionally followed by '/', followed by letter or
	 * number.
	 */

        i = (data[1] == '/') ? 2 : 1;

	if (!isalnum(data[i]))
		return 0;

	/* Scheme test. */

	*autolink = HALINK_NONE;

	/* Try to find the beginning of an URI. */

	while (i < size && (isalnum(data[i]) ||
	       data[i] == '.' || data[i] == '+' || data[i] == '-'))
		i++;

	if (i > 1 && data[i] == '@')
		if ((j = is_mail_autolink(data + i, size - i)) != 0) {
			*autolink = HALINK_EMAIL;
			return i + j;
		}

	if (i > 2 && data[i] == ':') {
		*autolink = HALINK_NORMAL;
		i++;
	}

	/* Completing autolink test: no spacing or ' or ". */

	if (i >= size)
		*autolink = HALINK_NONE;
	else if (*autolink) {
		j = i;
		while (i < size) {
			if (data[i] == '\\')
				i += 2;
			else if (data[i] == '>' || data[i] == '\'' ||
				 data[i] == '"' || data[i] == ' ' ||
				 data[i] == '\n')
				break;
			else
				i++;
		}

		if (i >= size)
			return 0;
		if (i > j && data[i] == '>')
			return i + 1;

		/* One of the forbidden chars has been found. */

		*autolink = HALINK_NONE;
	}

	/* Looking for something looking like a tag end. */

	while (i < size && data[i] != '>')
		i++;
	if (i >= size)
		return 0;
	return i + 1;
}

/*
 * Parses inline markdown elements.
 * This function is important because it handles raw input that we pass
 * directly to the output formatter ("normal_text").
 * The "nln" business is entirely for the nroff.c frontend, which needs
 * to understand newline status in the output buffer: it indicates to
 * parse_inline that the currently-known output is starting on a fresh
 * line.
 * Recursive invocations of parse_inline, which reset "ob" (and thus
 * we'll lose whether we're on a newline or not) need to respect this.
 * This is a limitation of the design of the compiler, which, in my
 * honest opinion, is pretty ad hoc and unstructured.
 */
static void
parse_inline(hbuf *ob, hdoc *doc, uint8_t *data, size_t size, int nln)
{
	size_t	 i = 0, end = 0, consumed = 0, svsz;
	hbuf	 work;
	uint8_t	*active_char = doc->active_char;

	memset(&work, 0, sizeof(hbuf));
	
	if (doc->work_bufs[BUFFER_SPAN].size +
	    doc->work_bufs[BUFFER_BLOCK].size > doc->max_nesting)
		return;

	while (i < size) {
		/* 
		 * Copying non-macro chars into the output. 
		 * Keep track of where we started in the output buffer.
		 */

		svsz = ob->size;
		while (end < size && active_char[data[end]] == 0)
			end++;

		/* 
		 * Push out all text until the current "active" (i.e.,
		 * signalling a markdown macro) character.
		 */

		if (doc->md.normal_text) {
			work.data = data + i;
			work.size = end - i;
			doc->md.normal_text(ob, &work, doc->data, nln);
		} else
			hbuf_put(ob, data + i, end - i);

		/* End of file? */

		if (end >= size)
			break;

		/* 
		 * If we've written something to our output buffer, keep
		 * track of whether it ended with a newline.
		 * This is 
		 */

		i = end;
		nln = svsz != ob->size ? buf_newln(ob) : nln;

		end = markdown_char_ptrs[(int)active_char[data[end]]]
			(ob, doc, data + i, i - consumed, size - i, nln);

		/* Check if no action from the callback. */

		if (0 == end) {
			end = i + 1;
			continue;
		} else {
			i += end;
			end = consumed = i;
		}

		/* 
		 * If we're in nroff mode, some of our inline macros
		 * produce newlines so we should crunch trailing
		 * whitespace.
		 */

		if (ob->size && '\n' == ob->data[ob->size - 1])
			nln = 1;
		else
			nln = 0;

		if (nln && doc->link_nospace) {
			if ( ! xisspace((int)data[i]) &&
			    NULL != doc->md.backspace)
				(*doc->md.backspace)(ob);
			while (i < size && xisspace((int)data[i]))
				i++;
			consumed = end = i;
		}
	}
}

/*
 * Returns whether special char at data[loc] is escaped by '\\'.
 */
static int
is_escaped(uint8_t *data, size_t loc)
{
	size_t i = loc;

	while (i >= 1 && data[i - 1] == '\\')
		i--;

	/* Odd numbers of backslashes escapes data[loc]. */

	return (loc - i) % 2;
}

/*
 * Looks for the next emph uint8_t, skipping other constructs.
  */
static size_t
find_emph_char(uint8_t *data, size_t size, uint8_t c)
{
	size_t i = 0;

	while (i < size) {
		while (i < size && data[i] != c &&
		       data[i] != '[' && data[i] != '`')
			i++;

		if (i == size)
			return 0;

		/* Not counting escaped chars. */

		if (is_escaped(data, i)) {
			i++;
			continue;
		}

		if (data[i] == c)
			return i;

		/* Skipping a codespan. */

		if (data[i] == '`') {
			size_t span_nb = 0, bt;
			size_t tmp_i = 0;

			/* Counting the number of opening backticks. */

			while (i < size && data[i] == '`') {
				i++;
				span_nb++;
			}

			if (i >= size)
				return 0;

			/* Finding the matching closing sequence. */

			bt = 0;
			while (i < size && bt < span_nb) {
				if (!tmp_i && data[i] == c)
					tmp_i = i;

				if (data[i] == '`')
					bt++;
				else
					bt = 0;
				i++;
			}

			/*
			 * Not a well-formed codespan; use found
			 * matching emph char.
			 */
			if (bt < span_nb && i >= size)
				return tmp_i;
		} else if (data[i] == '[') {
			size_t tmp_i = 0;
			uint8_t cc;

			/* Skipping a link. */

			i++;
			while (i < size && data[i] != ']') {
				if (!tmp_i && data[i] == c)
					tmp_i = i;
				i++;
			}

			i++;
			while (i < size && xisspace(data[i]))
				i++;

			if (i >= size)
				return tmp_i;

			switch (data[i]) {
			case '[':
				cc = ']';
				break;
			case '(':
				cc = ')';
				break;
			default:
				if (tmp_i)
					return tmp_i;
				else
					continue;
			}

			i++;
			while (i < size && data[i] != cc) {
				if (!tmp_i && data[i] == c)
					tmp_i = i;
				i++;
			}

			if (i >= size)
				return tmp_i;

			i++;
		}
	}

	return 0;
}

/*
 * Parsing single emphase.
 * Closed by a symbol not preceded by spacing and not followed by
 * symbol.
 */
static size_t
parse_emph1(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t size, uint8_t c, int nln)
{
	size_t	 i = 0, len;
	hbuf	*work = NULL;
	int	 r;

	/* Skipping one symbol if coming from emph3. */

	if (size > 1 && data[0] == c && data[1] == c) 
		i = 1;

	while (i < size) {
		len = find_emph_char(data + i, size - i, c);
		if (!len)
			return 0;
		i += len;
		if (i >= size)
			return 0;

		if (data[i] == c && !xisspace(data[i - 1])) {
			if (doc->ext_flags & LOWDOWN_NOINTEM)
				if (i + 1 < size && isalnum(data[i + 1]))
					continue;

			work = newbuf(doc, BUFFER_SPAN);
			parse_inline(work, doc, data, i, 1);

			r = doc->md.emphasis(ob, work, doc->data, nln);

			popbuf(doc, BUFFER_SPAN);
			return r ? i + 1 : 0;
		}
	}

	return 0;
}

/*
 * Parsing single emphase.
 */
static size_t
parse_emph2(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t size, uint8_t c, int nln)
{
	size_t	 i = 0, len;
	hbuf	*work = NULL;
	int	 r;

	while (i < size) {
		len = find_emph_char(data + i, size - i, c);
		if (0 == len) 
			return 0;
		i += len;

		if (i + 1 < size && data[i] == c && 
		    data[i + 1] == c && i && !xisspace(data[i - 1])) {
			work = newbuf(doc, BUFFER_SPAN);
			parse_inline(work, doc, data, i, 1);

			if (c == '~')
				r = doc->md.strikethrough
					(ob, work, doc->data, nln);
			else if (c == '=')
				r = doc->md.highlight
					(ob, work, doc->data, nln);
			else
				r = doc->md.double_emphasis
					(ob, work, doc->data, nln);

			popbuf(doc, BUFFER_SPAN);
			return r ? i + 2 : 0;
		}
		i++;
	}
	return 0;
}

/* 
 * Parsing single emphase
 * Finds the first closing tag, and delegates to the other emph.
 */
static size_t
parse_emph3(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t size, uint8_t c, int nln)
{
	size_t	 i = 0, len;
	int	 r;
	hbuf	*work;

	while (i < size) {
		len = find_emph_char(data + i, size - i, c);
		if (0 == len) 
			return 0;
		i += len;

		/* Skip spacing preceded symbols. */

		if (data[i] != c || xisspace(data[i - 1]))
			continue;

		if (i + 2 < size && data[i + 1] == c && 
		    data[i + 2] == c && doc->md.triple_emphasis) {
			/* Triple symbol found. */
			work = newbuf(doc, BUFFER_SPAN);
			parse_inline(work, doc, data, i, 1);
			r = doc->md.triple_emphasis
				(ob, work, doc->data, nln);
			popbuf(doc, BUFFER_SPAN);
			return r ? i + 3 : 0;
		} else if (i + 1 < size && data[i + 1] == c) {
			/* Double symbol found: handing to emph1. */
			len = parse_emph1(ob, 
				doc, data - 2, size + 2, c, nln);
			if (!len) 
				return 0;
			else 
				return len - 2;
		} else {
			/* Single symbol found: handing to emph2. */
			len = parse_emph2(ob, doc, 
				data - 1, size + 1, c, nln);
			if (!len) 
				return 0;
			else 
				return len - 1;
		}
	}
	return 0;
}

/* 
 * Parses a math span until the given ending delimiter.
 */
static size_t
parse_math(hbuf *ob, hdoc *doc, uint8_t *data, 
	size_t offset, size_t size, const char *end, 
	size_t delimsz, int displaymode)
{
	hbuf	 text;
	size_t	 i = delimsz;

	memset(&text, 0, sizeof(hbuf));

	if (!doc->md.math)
		return 0;

	/* Find ending delimiter. */

	while (1) {
		while (i < size && data[i] != (uint8_t)end[0])
			i++;

		if (i >= size)
			return 0;

		if (!is_escaped(data, i) && 
		    !(i + delimsz > size) && 
		    memcmp(data + i, end, delimsz) == 0)
			break;

		i++;
	}

	/* Prepare buffers. */

	text.data = data + delimsz;
	text.size = i - delimsz;

	/* 
	 * If this is a $$ and MATH_EXPLICIT is not active, guess whether
	 * displaymode should be enabled from the context.
	 */

	i += delimsz;
	if (delimsz == 2 && !(doc->ext_flags & LOWDOWN_MATHEXP))
		displaymode = is_empty_all(data - offset, offset) && 
			is_empty_all(data + i, size - i);

	/* Call callback. */

	if (doc->md.math(ob, &text, displaymode, doc->data))
		return i;

	return 0;
}

/* 
 * Single and double emphasis parsing.
 */
static size_t
char_emphasis(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t offset, size_t size, int nln)
{
	uint8_t c = data[0];
	size_t ret;

	if (doc->ext_flags & LOWDOWN_NOINTEM) 
		if (offset > 0 && !xisspace(data[-1]) && 
		    data[-1] != '>' && data[-1] != '(')
			return 0;

	if (size > 2 && data[1] != c) {
		/* 
		 * Spacing cannot follow an opening emphasis;
		 * strikethrough and highlight only takes '~~'.
		 */
		if (c == '~' || c == '=' || xisspace(data[1]) || 
		    (ret = parse_emph1(ob, doc, 
		     data + 1, size - 1, c, nln)) == 0)
			return 0;

		return ret + 1;
	}

	if (size > 3 && data[1] == c && data[2] != c) {
		if (xisspace(data[2]) || 
		    (ret = parse_emph2(ob, doc, 
		     data + 2, size - 2, c, nln)) == 0)
			return 0;

		return ret + 2;
	}

	if (size > 4 && data[1] == c && data[2] == c && data[3] != c) {
		if (c == '~' || c == '=' || xisspace(data[3]) || 
		    (ret = parse_emph3(ob, doc, 
		     data + 3, size - 3, c, nln)) == 0)
			return 0;

		return ret + 3;
	}

	return 0;
}


/* 
 * '\n' preceded by two spaces (assuming linebreak != 0) 
 */
static size_t
char_linebreak(hbuf *ob, hdoc *doc, uint8_t *data, size_t offset, size_t size, int nln)
{

	if (offset < 2 || data[-1] != ' ' || data[-2] != ' ')
		return 0;

	/* Removing the last space from ob and rendering. */

	while (ob->size && ob->data[ob->size - 1] == ' ')
		ob->size--;

	return doc->md.linebreak(ob, doc->data) ? 1 : 0;
}


/* 
 * '`' parsing a code span (assuming codespan != 0) 
 */
static size_t
char_codespan(hbuf *ob, hdoc *doc, uint8_t *data, size_t offset, size_t size, int nln)
{
	hbuf	 work;
	size_t	 end, nb = 0, i, f_begin, f_end;

	memset(&work, 0, sizeof(hbuf));

	/* Counting the number of backticks in the delimiter. */

	while (nb < size && data[nb] == '`')
		nb++;

	/* Finding the next delimiter. */

	i = 0;
	for (end = nb; end < size && i < nb; end++) {
		if (data[end] == '`') 
			i++;
		else 
			i = 0;
	}

	if (i < nb && end >= size)
		return 0; /* no matching delimiter */

	/* Trimming outside spaces. */

	f_begin = nb;
	while (f_begin < end && data[f_begin] == ' ')
		f_begin++;

	f_end = end - nb;
	while (f_end > nb && data[f_end-1] == ' ')
		f_end--;

	/* Real code span. */

	if (f_begin < f_end) {
		work.data = data + f_begin;
		work.size = f_end - f_begin;
		if (!doc->md.codespan(ob, &work, doc->data, nln))
			end = 0;
	} else {
		if (!doc->md.codespan(ob, NULL, doc->data, nln))
			end = 0;
	}

	return end;
}

/*
 * '\\' backslash escape
 */
static size_t
char_escape(hbuf *ob, hdoc *doc,
	uint8_t *data, size_t offset, size_t size, int nln)
{
	static const char *escape_chars =
		"\\`*_{}[]()#+-.!:|&<>^~=\"$";
	hbuf		 work;
	size_t		 w;
	const char	*end;

	memset(&work, 0, sizeof(hbuf));

	if (size > 1) {
		if (data[1] == '\\' &&
		    (doc->ext_flags & LOWDOWN_MATH) &&
		    size > 2 &&
		    (data[2] == '(' || data[2] == '[')) {
			end = (data[2] == '[') ? "\\\\]" : "\\\\)";
			w = parse_math(ob, doc, data, offset,
				size, end, 3, data[2] == '[');
			if (w)
				return w;
		}

		if (strchr(escape_chars, data[1]) == NULL)
			return 0;

		if (doc->md.normal_text) {
			work.data = data + 1;
			work.size = 1;
			doc->md.normal_text(ob, &work, doc->data, nln);
		} else
			hbuf_putc(ob, data[1]);
	} else if (size == 1) {
		if (doc->md.normal_text) {
			work.data = data;
			work.size = 1;
			doc->md.normal_text(ob, &work, doc->data, nln);
		} else
			hbuf_putc(ob, data[0]);
	}

	return 2;
}

/* 
 * '&' escaped when it doesn't belong to an entity 
 * Valid entities are assumed to be anything matching &#?[A-Za-z0-9]+;
 */
static size_t
char_entity(hbuf *ob, hdoc *doc, uint8_t *data, size_t offset, size_t size, int nln)
{
	size_t	 end = 1;
	hbuf	 work;

	memset(&work, 0, sizeof(work));

	if (end < size && data[end] == '#')
		end++;

	while (end < size && isalnum(data[end]))
		end++;

	if (end < size && data[end] == ';')
		end++; /* real entity */
	else
		return 0; /* lone '&' */

	if (doc->md.entity) {
		work.data = data;
		work.size = end;
		doc->md.entity(ob, &work, doc->data);
	} else 
		hbuf_put(ob, data, end);

	return end;
}

/* 
 * '<' when tags or autolinks are allowed.
 */
static size_t
char_langle_tag(hbuf *ob, hdoc *doc, uint8_t *data, size_t offset, size_t size, int nln)
{
	hbuf	 	 work;
	halink_type 	 altype = HALINK_NONE;
	size_t 	 	 end = tag_length(data, size, &altype);
	int 		 ret = 0;
	
	memset(&work, 0, sizeof(hbuf));

	work.data = data;
	work.size = end;

	if (end > 2) {
		if (doc->md.autolink && altype != HALINK_NONE) {
			hbuf *u_link = newbuf(doc, BUFFER_SPAN);
			work.data = data + 1;
			work.size = end - 2;
			unscape_text(u_link, &work);
			ret = doc->md.autolink(ob, u_link, altype, doc->data, nln);
			popbuf(doc, BUFFER_SPAN);
		} else if (doc->md.raw_html)
			ret = doc->md.raw_html(ob, &work, doc->data);
	}

	if (!ret) 
		return 0;
	else 
		return end;
}

static size_t
char_autolink_www(hbuf *ob, hdoc *doc,
	uint8_t *data, size_t offset, size_t size, int nln)
{
	hbuf	*link, *link_url, *link_text;
	size_t	 link_len, rewind;

	if (NULL == doc->md.link || doc->in_link_body)
		return 0;

	link = newbuf(doc, BUFFER_SPAN);
	link_len = halink_www(&rewind, link, data, offset, size);

	if (link_len > 0) {
		link_url = newbuf(doc, BUFFER_SPAN);
		HBUF_PUTSL(link_url, "http://");
		hbuf_put(link_url, link->data, link->size);

		if (ob->size > rewind)
			ob->size -= rewind;
		else
			ob->size = 0;

		if (doc->md.normal_text) {
			link_text = newbuf(doc, BUFFER_SPAN);
			doc->md.normal_text
				(link_text, link, doc->data, nln);
			doc->md.link(ob, link_text,
				link_url, NULL, doc->data, nln);
			popbuf(doc, BUFFER_SPAN);
		} else
			doc->md.link(ob, link, link_url, NULL, doc->data, nln);

		popbuf(doc, BUFFER_SPAN);
	}

	popbuf(doc, BUFFER_SPAN);
	return link_len;
}

static size_t
char_autolink_email(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t offset, size_t size, int nln)
{
	hbuf	*link;
	size_t	 link_len, rewind;

	if (NULL == doc->md.autolink || doc->in_link_body)
		return 0;

	link = newbuf(doc, BUFFER_SPAN);
	link_len = halink_email(&rewind, link, data, offset, size);

	if (link_len > 0) {
		if (ob->size > rewind) {
			ob->size -= rewind;
			nln = 0 == ob->size ? nln :
				'\n' == ob->data[ob->size - 1];
		} else
			ob->size = 0;

		doc->md.autolink(ob, link, HALINK_EMAIL, doc->data, nln);
	}

	popbuf(doc, BUFFER_SPAN);
	return link_len;
}

static size_t
char_autolink_url(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t offset, size_t size, int nln)
{
	hbuf	*link;
	size_t	 link_len, rewind;

	if (NULL == doc->md.autolink || doc->in_link_body)
		return 0;

	link = newbuf(doc, BUFFER_SPAN);
	link_len = halink_url(&rewind, link, data, offset, size);

	if (link_len > 0) {
		if (ob->size > rewind) {
			ob->size -= rewind;
			nln = 0 == ob->size ? nln :
				'\n' == ob->data[ob->size - 1];
		} else
			ob->size = 0;

		doc->md.autolink(ob, link, HALINK_NORMAL, doc->data, nln);
	}

	popbuf(doc, BUFFER_SPAN);
	return link_len;
}

static size_t
char_image(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t offset, size_t size, int nln) 
{
	size_t	 ret;

	if (size < 2 || data[1] != '[') 
		return 0;

	ret = char_link(ob, doc, data + 1, offset + 1, size - 1, nln);
	if (!ret) 
		return 0;

	return ret + 1;
}

/* 
 * '[': parsing a link, a footnote or an image.
 */
static size_t
char_link(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t offset, size_t size, int nln)
{
	hbuf 	*content = NULL, *link = NULL , *title = NULL, 
		*u_link = NULL, *dims = NULL;
	size_t 	 i = 1, txt_e, link_b = 0, link_e = 0, title_b = 0, 
		 title_e = 0, org_work_size, nb_p, dims_b = 0, 
		 dims_e = 0;
	int 	 ret = 0, in_title = 0, qtype = 0, is_img, is_footnote;
	hbuf 	 id;
	struct footnote_ref *fr;

	org_work_size = doc->work_bufs[BUFFER_SPAN].size;
	is_img = offset && data[-1] == '!' && 
		!is_escaped(data - offset, offset - 1);
	is_footnote = (doc->ext_flags & LOWDOWN_FOOTNOTES && 
			data[1] == '^');

	/* checking whether the correct renderer exists */

	if ((is_footnote && !doc->md.footnote_ref) || 
	    (is_img && !doc->md.image) || 
	    (!is_img && !is_footnote && !doc->md.link))
		goto cleanup;

	/* looking for the matching closing bracket */

	i += find_emph_char(data + i, size - i, ']');
	txt_e = i;

	if (i < size && data[i] == ']') 
		i++;
	else 
		goto cleanup;

	/* footnote link */

	if (is_footnote) {
		memset(&id, 0, sizeof(hbuf));

		if (txt_e < 3)
			goto cleanup;

		id.data = data + 2;
		id.size = txt_e - 2;

		fr = find_footnote_ref
			(&doc->footnotes_found, id.data, id.size);

		/* mark footnote used */
		if (fr && !fr->is_used) {
			add_footnote_ref(&doc->footnotes_used, fr);
			fr->is_used = 1;
			fr->num = doc->footnotes_used.count;

			/* render */
			if (doc->md.footnote_ref)
				ret = doc->md.footnote_ref
					(ob, fr->num, doc->data);
		}

		goto cleanup;
	}

	/*
	 * Skip any amount of spacing.
	 * (This is much more laxist than original markdown syntax.)
	 * Note that we're doing so.
	 */

	if (i < size && xisspace(data[i]))
		lmsg(doc->opts, LOWDOWN_ERR_SPACE_BEFORE_LINK, NULL);

	while (i < size && xisspace(data[i]))
		i++;

	/* inline style link */

	if (i < size && data[i] == '(') {
		/* skipping initial spacing */
		i++;

		while (i < size && xisspace(data[i]))
			i++;

		link_b = i;

		/* looking for link end: ' " ) */
		/* Count the number of open parenthesis */
		nb_p = 0;

		while (i < size) {
			if (data[i] == '\\')
				i += 2;
			else if (data[i] == '(' && i != 0) {
				nb_p++; 
				i++;
			} else if (data[i] == ')') {
				if (nb_p == 0) 
					break;
				else 
					nb_p--;
				i++;
			} else if (i >= 1 && xisspace(data[i-1]) && 
				   (data[i] == '\'' || 
				    data[i] == '=' ||
				    data[i] == '"')) 
				break;
			else 
				i++;
		}

		if (i >= size) 
			goto cleanup;

		link_e = i;

		/*
		 * We might be at the end of the link, or we might be at
		 * the title of the link.
		 * In the latter case, progress til link-end.
		 */
again:
		if (data[i] == '\'' || data[i] == '"') {
			/* 
			 * Looking for title end if present.
			 * This is a quoted part after the image.
			 */

			qtype = data[i];
			in_title = 1;
			i++;
			title_b = i;

			for ( ; i < size; i++)
				if (data[i] == '\\')
					i++;
				else if (data[i] == qtype)
					in_title = 0; 
				else if ((data[i] == '=') && !in_title)
					break;
				else if ((data[i] == ')') && !in_title)
					break;

			if (i >= size) 
				goto cleanup;

			assert(i < size && 
			       (')' == data[i] || '=' == data[i]));

			/* Skipping spacing after title. */

			title_e = i - 1;
			while (title_e > title_b && 
			       xisspace(data[title_e]))
				title_e--;

			/* Checking for closing quote presence. */

			if (data[title_e] != '\'' &&  
			    data[title_e] != '"') {
				title_b = title_e = 0;
				link_e = i;
			}

			/* 
			 * If we're followed by a dimension string, then
			 * jump back into the parsing engine for it.
			 */

			if ('=' == data[i])
				goto again;
		} else if (data[i] == '=') {
			dims_b = ++i;
			for ( ; i < size; i++) 
				if (data[i] == '\\')
					i++;
				else if ('\'' == data[i] || '"' == data[i])
					break;
				else if (data[i] == ')')
					break;

			if (i >= size)
				goto cleanup;

			assert(i < size && 
			       (')' == data[i] || '"' == data[i] || 
				'\'' == data[i]));

			/* Skipping spacing after dimensions. */

			dims_e = i;
			while (dims_e > dims_b && 
			       xisspace(data[dims_e]))
				dims_e--;

			/* 
			 * If we're followed by a title string, then
			 * jump back into the parsing engine for it.
			 */

			if ('"' == data[i] || '\'' == data[i])
				goto again;
		}

		/* Remove spacing at the end of the link. */

		while (link_e > link_b && xisspace(data[link_e - 1]))
			link_e--;

		/* Remove optional angle brackets around the link. */

		if (data[link_b] == '<' && data[link_e - 1] == '>') {
			link_b++;
			link_e--;
		}

		/* building escaped link and title */
		if (link_e > link_b) {
			link = newbuf(doc, BUFFER_SPAN);
			hbuf_put(link, data + link_b, link_e - link_b);
		}

		if (title_e > title_b) {
			title = newbuf(doc, BUFFER_SPAN);
			hbuf_put(title, data + title_b, title_e - title_b);
		}

		if (dims_e > dims_b) {
			dims = newbuf(doc, BUFFER_SPAN);
			hbuf_put(dims, data + dims_b, dims_e - dims_b);
		}

		i++;
	}

	/* reference style link */
	else if (i < size && data[i] == '[') {
		hbuf *id = newbuf(doc, BUFFER_SPAN);
		struct link_ref *lr;

		/* looking for the id */
		i++;
		link_b = i;
		while (i < size && data[i] != ']') i++;
		if (i >= size) goto cleanup;
		link_e = i;

		/* finding the link_ref */
		if (link_b == link_e)
			replace_spacing(id, data + 1, txt_e - 1);
		else
			hbuf_put(id, data + link_b, link_e - link_b);

		lr = find_link_ref(doc->refs, id->data, id->size);
		if (!lr)
			goto cleanup;

		/* keeping link and title from link_ref */
		link = lr->link;
		title = lr->title;
		i++;
	}

	/* shortcut reference style link */
	else {
		hbuf *id = newbuf(doc, BUFFER_SPAN);
		struct link_ref *lr;

		/* crafting the id */
		replace_spacing(id, data + 1, txt_e - 1);

		/* finding the link_ref */
		lr = find_link_ref(doc->refs, id->data, id->size);
		if (!lr)
			goto cleanup;

		/* keeping link and title from link_ref */
		link = lr->link;
		title = lr->title;

		/* rewinding the spacing */
		i = txt_e + 1;
	}

	/* building content: img alt is kept, only link content is parsed */
	if (txt_e > 1) {
		content = newbuf(doc, BUFFER_SPAN);
		if (is_img) {
			hbuf_put(content, data + 1, txt_e - 1);
		} else {
			/* disable autolinking when parsing inline the
			 * content of a link */
			doc->in_link_body = 1;
			parse_inline(content, doc, data + 1, txt_e - 1, nln);
			doc->in_link_body = 0;
		}
	}

	if (link) {
		u_link = newbuf(doc, BUFFER_SPAN);
		unscape_text(u_link, link);
	}

	/* calling the relevant rendering function */
	if (is_img) {
		ret = doc->md.image(ob, u_link, title, dims, content, doc->data);
	} else {
		ret = doc->md.link(ob, content, u_link, title, doc->data, nln);
	}

	/* cleanup */
cleanup:
	doc->work_bufs[BUFFER_SPAN].size = (int)org_work_size;
	return ret ? i : 0;
}

static size_t
char_superscript(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t offset, size_t size, int nln)
{
	size_t	 sup_start, sup_len;
	hbuf	*sup;

	if (!doc->md.superscript)
		return 0;

	if (size < 2)
		return 0;

	if (data[1] == '(') {
		sup_start = 2;
		sup_len = find_emph_char(data + 2, size - 2, ')') + 2;
		if (sup_len == size)
			return 0;
	} else {
		sup_start = sup_len = 1;
		while (sup_len < size && !xisspace(data[sup_len]))
			sup_len++;
	}

	if (sup_len - sup_start == 0)
		return (sup_start == 2) ? 3 : 0;

	sup = newbuf(doc, BUFFER_SPAN);
	parse_inline(sup, doc, data + sup_start, 
		sup_len - sup_start, nln);
	doc->md.superscript(ob, sup, doc->data, nln);
	popbuf(doc, BUFFER_SPAN);

	return (sup_start == 2) ? sup_len + 1 : sup_len;
}

static size_t
char_math(hbuf *ob, hdoc *doc, 
	uint8_t *data, size_t offset, size_t size, int nln)
{

	/* Double dollar. */

	if (size > 1 && data[1] == '$')
		return parse_math(ob, doc, data, offset, size, "$$", 2, 1);

	/* Single dollar allowed only with MATH_EXPLICIT flag. */

	if (doc->ext_flags & LOWDOWN_MATHEXP)
		return parse_math(ob, doc, data, offset, size, "$", 1, 0);

	return 0;
}

/* 
 * Returns the line length when it is empty, 0 otherwise.
 */
static size_t
is_empty(const uint8_t *data, size_t size)
{
	size_t	 i;

	for (i = 0; i < size && data[i] != '\n'; i++)
		if (data[i] != ' ')
			return 0;

	return i + 1;
}

/* is_hrule • returns whether a line is a horizontal rule */
static int
is_hrule(uint8_t *data, size_t size)
{
	size_t i = 0, n = 0;
	uint8_t c;

	/* skipping initial spaces */
	if (size < 3) return 0;
	if (data[0] == ' ') { i++;
	if (data[1] == ' ') { i++;
	if (data[2] == ' ') { i++; } } }

	/* looking at the hrule uint8_t */
	if (i + 2 >= size
	|| (data[i] != '*' && data[i] != '-' && data[i] != '_'))
		return 0;
	c = data[i];

	/* the whole line must be the char or space */
	while (i < size && data[i] != '\n') {
		if (data[i] == c) n++;
		else if (data[i] != ' ')
			return 0;

		i++;
	}

	return n >= 3;
}

/* check if a line is a code fence; return the
 * end of the code fence. if passed, width of
 * the fence rule and character will be returned */
static size_t
is_codefence(uint8_t *data, size_t size, size_t *width, uint8_t *chr)
{
	size_t i = 0, n = 1;
	uint8_t c;

	/* skipping initial spaces */
	if (size < 3)
		return 0;

	if (data[0] == ' ') { i++;
	if (data[1] == ' ') { i++;
	if (data[2] == ' ') { i++; } } }

	/* looking at the hrule uint8_t */
	c = data[i];
	if (i + 2 >= size || !(c=='~' || c=='`'))
		return 0;

	/* the fence must be that same character */
	while (++i < size && data[i] == c)
		++n;

	if (n < 3)
		return 0;

	if (width) *width = n;
	if (chr) *chr = c;
	return i;
}

/* expects single line, checks if it's a codefence and extracts language */
static size_t
parse_codefence(uint8_t *data, size_t size, hbuf *lang, size_t *width, uint8_t *chr)
{
	size_t i, w, lang_start;

	i = w = is_codefence(data, size, width, chr);
	if (i == 0)
		return 0;

	while (i < size && xisspace(data[i]))
		i++;

	lang_start = i;

	while (i < size && !xisspace(data[i]))
		i++;

	lang->data = data + lang_start;
	lang->size = i - lang_start;

	/* Avoid parsing a codespan as a fence */
	i = lang_start + 2;
	while (i < size && !(data[i] == *chr && data[i-1] == *chr && data[i-2] == *chr)) i++;
	if (i < size) return 0;

	return w;
}

/* is_atxheader • returns whether the line is a hash-prefixed header */
static int
is_atxheader(hdoc *doc, uint8_t *data, size_t size)
{
	if (data[0] != '#')
		return 0;

	if (doc->ext_flags & LOWDOWN_SPHD) {
		size_t level = 0;

		while (level < size && level < 6 && data[level] == '#')
			level++;

		if (level < size && data[level] != ' ')
			return 0;
	}

	return 1;
}

/* is_headerline • returns whether the line is a setext-style hdr underline */
static int
is_headerline(uint8_t *data, size_t size)
{
	size_t i = 0;

	/* test of level 1 header */
	if (data[i] == '=') {
		for (i = 1; i < size && data[i] == '='; i++);
		while (i < size && data[i] == ' ') i++;
		return (i >= size || data[i] == '\n') ? 1 : 0; }

	/* test of level 2 header */
	if (data[i] == '-') {
		for (i = 1; i < size && data[i] == '-'; i++);
		while (i < size && data[i] == ' ') i++;
		return (i >= size || data[i] == '\n') ? 2 : 0; }

	return 0;
}

static int
is_next_headerline(uint8_t *data, size_t size)
{
	size_t i = 0;

	while (i < size && data[i] != '\n')
		i++;

	if (++i >= size)
		return 0;

	return is_headerline(data + i, size - i);
}

/* prefix_quote • returns blockquote prefix length */
static size_t
prefix_quote(uint8_t *data, size_t size)
{
	size_t i = 0;
	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;

	if (i < size && data[i] == '>') {
		if (i + 1 < size && data[i + 1] == ' ')
			return i + 2;

		return i + 1;
	}

	return 0;
}

/* prefix_code • returns prefix length for block code*/
static size_t
prefix_code(uint8_t *data, size_t size)
{
	if (size > 3 && data[0] == ' ' && data[1] == ' '
		&& data[2] == ' ' && data[3] == ' ') return 4;

	return 0;
}

/* prefix_oli • returns ordered list item prefix */
static size_t
prefix_oli(uint8_t *data, size_t size, uint8_t **num, size_t *numsz)
{
	size_t i = 0, st;

	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;

	if (i >= size || data[i] < '0' || data[i] > '9')
		return 0;

	st = i;
	if (NULL != num)
		*num = &data[i];

	while (i < size && data[i] >= '0' && data[i] <= '9')
		i++;

	if (NULL != numsz)
		*numsz = i - st;

	if (i + 1 >= size || data[i] != '.' || data[i + 1] != ' ')
		return 0;

	if (is_next_headerline(data + i, size - i))
		return 0;

	return i + 2;
}

/* prefix_uli • returns ordered list item prefix */
static size_t
prefix_uli(uint8_t *data, size_t size)
{
	size_t i = 0;

	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;
	if (i < size && data[i] == ' ') i++;

	if (i + 1 >= size ||
		(data[i] != '*' && data[i] != '+' && data[i] != '-') ||
		data[i + 1] != ' ')
		return 0;

	if (is_next_headerline(data + i, size - i))
		return 0;

	return i + 2;
}



/* parse_blockquote • handles parsing of a blockquote fragment */
static size_t
parse_blockquote(hbuf *ob, hdoc *doc, uint8_t *data, size_t size)
{
	size_t beg, end = 0, pre, work_size = 0;
	uint8_t *work_data = NULL;
	hbuf *out = NULL;

	out = newbuf(doc, BUFFER_BLOCK);
	beg = 0;
	while (beg < size) {
		for (end = beg + 1; end < size && data[end - 1] != '\n'; end++);

		pre = prefix_quote(data + beg, end - beg);

		if (pre)
			beg += pre; /* skipping prefix */

		/* empty line followed by non-quote line */
		else if (is_empty(data + beg, end - beg) &&
				(end >= size || (prefix_quote(data + end, size - end) == 0 &&
				!is_empty(data + end, size - end))))
			break;

		if (beg < end) { /* copy into the in-place working buffer */
			/* hbuf_put(work, data + beg, end - beg); */
			if (!work_data)
				work_data = data + beg;
			else if (data + beg != work_data + work_size)
				memmove(work_data + work_size, data + beg, end - beg);
			work_size += end - beg;
		}
		beg = end;
	}

	parse_block(out, doc, work_data, work_size);
	if (doc->md.blockquote)
		doc->md.blockquote(ob, out, doc->data);
	popbuf(doc, BUFFER_BLOCK);
	return end;
}

static size_t
parse_htmlblock(hbuf *ob, hdoc *doc, uint8_t *data, size_t size, int do_render);

/*
 * handles parsing of a regular paragraph
 */
static size_t
parse_paragraph(hbuf *ob, hdoc *doc, uint8_t *data, size_t size)
{
	hbuf		 work;
	hbuf		*tmp, *header_work;
	size_t		 i = 0, end = 0, beg;
	int		 level = 0;
	const uint8_t	*sv;

	memset(&work, 0, sizeof(hbuf));

	work.data = data;

	while (i < size) {
		for (end = i + 1;
		     end < size && data[end - 1] != '\n';
		     end++)
			/* empty */;

		if (is_empty(data + i, size - i))
			break;

		if ((level = is_headerline(data + i, size - i)) != 0)
			break;

		if (is_atxheader(doc, data + i, size - i) ||
			is_hrule(data + i, size - i) ||
			prefix_quote(data + i, size - i)) {
			end = i;
			break;
		}

		i = end;
	}

	work.size = i;
	while (work.size && data[work.size - 1] == '\n')
		work.size--;

	if ( ! level) {
		tmp = newbuf(doc, BUFFER_BLOCK);

		sv = doc->start;
		doc->start = work.data;
		parse_inline(tmp, doc, work.data, work.size, 1);
		doc->start = sv;

		if (doc->md.paragraph)
			doc->md.paragraph(ob, tmp, doc->data, doc->cur_par);
		doc->cur_par++;
		popbuf(doc, BUFFER_BLOCK);
	} else {
		if (work.size) {
			i = work.size;
			work.size -= 1;

			while (work.size && data[work.size] != '\n')
				work.size -= 1;

			beg = work.size + 1;
			while (work.size && data[work.size - 1] == '\n')
				work.size -= 1;

			if (work.size > 0) {
				hbuf *tmp = newbuf(doc, BUFFER_BLOCK);
				parse_inline(tmp, doc, work.data, 
					work.size, 1);

				if (doc->md.paragraph)
					doc->md.paragraph(ob, tmp, doc->data, doc->cur_par);
				doc->cur_par++;

				popbuf(doc, BUFFER_BLOCK);
				work.data += beg;
				work.size = i - beg;
			}
			else work.size = i;
		}

		header_work = newbuf(doc, BUFFER_SPAN);

		sv = doc->start;
		doc->start = work.data;
		parse_inline(header_work, doc, 
			work.data, work.size, 1);
		doc->start = sv;

		if (doc->md.header)
			doc->md.header(ob, header_work, level, doc->data);

		popbuf(doc, BUFFER_SPAN);
	}

	return end;
}

/* parse_fencedcode • handles parsing of a block-level code fragment */
static size_t
parse_fencedcode(hbuf *ob, hdoc *doc, uint8_t *data, size_t size)
{
	hbuf text = { NULL, 0, 0, 0, 0 };
	hbuf lang = { NULL, 0, 0, 0, 0 };
	size_t i = 0, text_start, line_start;
	size_t w, w2;
	size_t width, width2;
	uint8_t chr, chr2;

	/* parse codefence line */
	while (i < size && data[i] != '\n')
		i++;

	w = parse_codefence(data, i, &lang, &width, &chr);
	if (!w)
		return 0;

	/* search for end */
	i++;
	text_start = i;
	while ((line_start = i) < size) {
		while (i < size && data[i] != '\n')
			i++;

		w2 = is_codefence(data + line_start, i - line_start, &width2, &chr2);
		if (w == w2 && width == width2 && chr == chr2 &&
		    is_empty(data + (line_start+w), i - (line_start+w)))
			break;

		i++;
	}

	text.data = data + text_start;
	text.size = line_start - text_start;

	if (doc->md.blockcode)
		doc->md.blockcode(ob, text.size ? &text : NULL, lang.size ? &lang : NULL, doc->data);

	return i;
}

static size_t
parse_blockcode(hbuf *ob, hdoc *doc, uint8_t *data, size_t size)
{
	size_t beg, end, pre;
	hbuf *work = NULL;

	work = newbuf(doc, BUFFER_BLOCK);

	beg = 0;
	while (beg < size) {
		for (end = beg + 1; end < size && data[end - 1] != '\n'; end++) {};
		pre = prefix_code(data + beg, end - beg);

		if (pre)
			beg += pre; /* skipping prefix */
		else if (!is_empty(data + beg, end - beg))
			/* non-empty non-prefixed line breaks the pre */
			break;

		if (beg < end) {
			/* verbatim copy to the working buffer,
				escaping entities */
			if (is_empty(data + beg, end - beg))
				hbuf_putc(work, '\n');
			else hbuf_put(work, data + beg, end - beg);
		}
		beg = end;
	}

	while (work->size && work->data[work->size - 1] == '\n')
		work->size -= 1;

	hbuf_putc(work, '\n');

	if (doc->md.blockcode)
		doc->md.blockcode(ob, work, NULL, doc->data);

	popbuf(doc, BUFFER_BLOCK);
	return beg;
}

/*
 * Parsing of a single list item assuming initial prefix is already
 * removed.
 */
static size_t
parse_listitem(hbuf *ob, hdoc *doc, uint8_t *data,
	size_t size, hlist_fl *flags, size_t num)
{
	hbuf		*work = NULL, *inter = NULL;
	size_t		 beg = 0, end, pre, sublist = 0, orgpre = 0, i;
	int		 in_empty = 0, has_inside_empty = 0,
			 in_fence = 0;
	const uint8_t	*sv;
	size_t 		 has_next_uli = 0, has_next_oli = 0;

	/* Keeping track of the first indentation prefix. */

	while (orgpre < 3 && orgpre < size && data[orgpre] == ' ')
		orgpre++;

	beg = prefix_uli(data, size);

	if ( ! beg)
		beg = prefix_oli(data, size, NULL, NULL);
	if ( ! beg)
		return 0;

	/* Skipping to the beginning of the following line. */

	end = beg;
	while (end < size && data[end - 1] != '\n')
		end++;

	/* Getting working buffers. */

	work = newbuf(doc, BUFFER_SPAN);
	inter = newbuf(doc, BUFFER_SPAN);

	/* Putting the first line into the working buffer. */

	hbuf_put(work, data + beg, end - beg);
	beg = end;

	/* Process the following lines. */

	while (beg < size) {
		has_next_uli = has_next_oli = 0;
		end++;

		while (end < size && data[end - 1] != '\n')
			end++;

		/* Process an empty line. */

		if (is_empty(data + beg, end - beg)) {
			in_empty = 1;
			beg = end;
			continue;
		}

		/* Calculating the indentation. */

		i = 0;
		while (i < 4 && beg + i < end && data[beg + i] == ' ')
			i++;
		pre = i;

		if (doc->ext_flags & LOWDOWN_FENCED)
			if (is_codefence(data + beg + i,
			    end - beg - i, NULL, NULL))
				in_fence = !in_fence;

		/*
		 * Only check for new list items if we are **not**
		 * inside a fenced code block.
		 */

		if (!in_fence) {
			has_next_uli = prefix_uli
				(data + beg + i, end - beg - i);
			has_next_oli = prefix_oli
				(data + beg + i, end - beg - i,
				 NULL, NULL);
		}

		/* Checking for a new item. */

		if ((has_next_uli &&
		     ! is_hrule(data + beg + i, end - beg - i)) ||
		    has_next_oli) {
			if (in_empty)
				has_inside_empty = 1;

			/* the following item must have the same (or less) indentation */
			if (pre <= orgpre) {
				/* if the following item has different list type, we end this list */
				if (in_empty && (
					((*flags & HLIST_ORDERED) && has_next_uli) ||
					(!(*flags & HLIST_ORDERED) && has_next_oli)))
					*flags |= HOEDOWN_LI_END;

				break;
			}

			if (!sublist)
				sublist = work->size;
		} else if (in_empty && pre == 0) {
			/* joining only indented stuff after empty lines;
			 * note that now we only require 1 space of indentation
			 * to continue a list */
			*flags |= HOEDOWN_LI_END;
			break;
		}

		if (in_empty) {
			hbuf_putc(work, '\n');
			has_inside_empty = 1;
			in_empty = 0;
		}

		/* adding the line without prefix into the working buffer */
		hbuf_put(work, data + beg + i, end - beg - i);
		beg = end;
	}

	/* render of li contents */
	if (has_inside_empty)
		*flags |= HLIST_BLOCK;

	sv = doc->start;
	doc->start = work->data;

	if (*flags & HLIST_BLOCK) {
		/* intermediate render of block li */
		if (sublist && sublist < work->size) {
			parse_block(inter, doc, work->data, sublist);
			parse_block(inter, doc, work->data + sublist, work->size - sublist);
		} else
			parse_block(inter, doc, work->data, work->size);
	} else {
		/* intermediate render of inline li */
		if (sublist && sublist < work->size) {
			parse_inline(inter, doc, work->data, sublist, buf_newln(ob));
			parse_block(inter, doc, work->data + sublist, work->size - sublist);
		}
		else
			parse_inline(inter, doc, work->data, work->size, buf_newln(ob));
	}

	doc->start = sv;

	/* render of li itself */
	if (doc->md.listitem)
		doc->md.listitem(ob, inter, *flags, doc->data, num);

	popbuf(doc, BUFFER_SPAN);
	popbuf(doc, BUFFER_SPAN);
	return beg;
}


/* parse_list • parsing ordered or unordered list block */
static size_t
parse_list(hbuf *ob, hdoc *doc, uint8_t *data, size_t size, hlist_fl flags)
{
	hbuf *work = NULL;
	size_t i = 0, j, k = 1;

	work = newbuf(doc, BUFFER_BLOCK);

	while (i < size) {
		j = parse_listitem(work, doc, data + i, size - i, &flags, k++);
		i += j;

		if (!j || (flags & HOEDOWN_LI_END))
			break;
	}

	if (doc->md.list)
		doc->md.list(ob, work, flags, doc->data);
	popbuf(doc, BUFFER_BLOCK);
	return i;
}

/* 
 * Parsing of atx-style headers.
 */
static size_t
parse_atxheader(hbuf *ob, hdoc *doc, uint8_t *data, size_t size)
{
	size_t 	 level = 0, i, end, skip;
	hbuf	*work;

	while (level < size && level < 6 && data[level] == '#')
		level++;

	for (i = level; i < size && data[i] == ' '; i++)
		continue;

	for (end = i; end < size && data[end] != '\n'; end++)
		continue;

	skip = end;

	while (end && data[end - 1] == '#')
		end--;

	while (end && data[end - 1] == ' ')
		end--;

	if (end > i) {
		work = newbuf(doc, BUFFER_SPAN);
		parse_inline(work, doc, data + i, end - i, buf_newln(ob));
		if (doc->md.header)
			doc->md.header(ob, work, (int)level, doc->data);
		popbuf(doc, BUFFER_SPAN);
	}

	return skip;
}

/* parse_footnote_def • parse a single footnote definition */
static void
parse_footnote_def(hbuf *ob, hdoc *doc, unsigned int num, uint8_t *data, size_t size)
{
	hbuf *work = NULL;
	work = newbuf(doc, BUFFER_SPAN);

	parse_block(work, doc, data, size);

	if (doc->md.footnote_def)
	doc->md.footnote_def(ob, work, num, doc->data);
	popbuf(doc, BUFFER_SPAN);
}

/* parse_footnote_list • render the contents of the footnotes */
static void
parse_footnote_list(hbuf *ob, hdoc *doc, struct footnote_list *footnotes)
{
	hbuf			*work = NULL;
	struct footnote_item	*item;
	struct footnote_ref	*ref;
	const uint8_t		*sv;

	if (footnotes->count == 0)
		return;

	work = newbuf(doc, BUFFER_BLOCK);

	item = footnotes->head;
	while (item) {
		ref = item->ref;
		sv = doc->start;
		doc->start = ref->contents->data;
		parse_footnote_def(work, doc, ref->num,
			ref->contents->data, ref->contents->size);
		doc->start = sv;
		item = item->next;
	}

	if (doc->md.footnotes)
		doc->md.footnotes(ob, work, doc->data);
	popbuf(doc, BUFFER_BLOCK);
}

/* htmlblock_is_end • check for end of HTML block : </tag>( *)\n */
/*	returns tag length on match, 0 otherwise */
/*	assumes data starts with "<" */
static size_t
htmlblock_is_end(
	const char *tag,
	size_t tag_len,
	hdoc *doc,
	uint8_t *data,
	size_t size)
{
	size_t i = tag_len + 3, w;

	/* try to match the end tag */
	/* note: we're not considering tags like "</tag >" which are still valid */
	if (i > size ||
		data[1] != '/' ||
		strncasecmp((char *)data + 2, tag, tag_len) != 0 ||
		data[tag_len + 2] != '>')
		return 0;

	/* rest of the line must be empty */
	if ((w = is_empty(data + i, size - i)) == 0 && i < size)
		return 0;

	return i + w;
}

/* htmlblock_find_end • try to find HTML block ending tag */
/*	returns the length on match, 0 otherwise */
static size_t
htmlblock_find_end(
	const char *tag,
	size_t tag_len,
	hdoc *doc,
	uint8_t *data,
	size_t size)
{
	size_t i = 0, w;

	while (1) {
		while (i < size && data[i] != '<') i++;
		if (i >= size) return 0;

		w = htmlblock_is_end(tag, tag_len, doc, data + i, size - i);
		if (w) return i + w;
		i++;
	}
}

/* htmlblock_find_end_strict • try to find end of HTML block in strict mode */
/*	(it must be an unindented line, and have a blank line afterwads) */
/*	returns the length on match, 0 otherwise */
static size_t
htmlblock_find_end_strict(
	const char *tag,
	size_t tag_len,
	hdoc *doc,
	uint8_t *data,
	size_t size)
{
	size_t i = 0, mark;

	while (1) {
		mark = i;
		while (i < size && data[i] != '\n') i++;
		if (i < size) i++;
		if (i == mark) return 0;

		if (data[mark] == ' ' && mark > 0) continue;
		mark += htmlblock_find_end(tag, tag_len, doc, data + mark, i - mark);
		if (mark == i && (is_empty(data + i, size - i) || i >= size)) break;
	}

	return i;
}

/*
 * Canonicalise a sequence of length "len" bytes in "str".
 * This returns NULL if the sequence is not recognised, or a
 * nil-terminated string of the sequence otherwise.
 */
static const char *
hhtml_find_block(const char *str, size_t len)
{
	static const char	*tags[] = {
		"blockquote",
		"del",
		"div",
		"dl",
		"fieldset",
		"figure",
		"form",
		"h1",
		"h2",
		"h3",
		"h4",
		"h5",
		"h6",
		"iframe",
		"ins",
		"math",
		"noscript",
		"ol",
		"p",
		"pre",
		"script",
		"style",
		"table",
		"ul",
		NULL,
	};
	size_t			 i;

	for (i = 0; NULL != tags[i]; i++)
		if (0 == strncasecmp(tags[i], str, len))
			return tags[i];

	return NULL;
}

/* 
 * Parsing of inline HTML block.
 */
static size_t
parse_htmlblock(hbuf *ob, hdoc *doc, uint8_t *data, size_t size, int do_render)
{
	hbuf work = { NULL, 0, 0, 0, 0 };
	size_t i, j = 0, tag_len, tag_end;
	const char *curtag = NULL;

	work.data = data;

	/* identification of the opening tag */
	if (size < 2 || data[0] != '<')
		return 0;

	i = 1;
	while (i < size && data[i] != '>' && data[i] != ' ')
		i++;

	if (i < size)
		curtag = hhtml_find_block((char *)data + 1, i - 1);

	/* handling of special cases */
	if (!curtag) {

		/* HTML comment, laxist form */
		if (size > 5 && data[1] == '!' && data[2] == '-' && data[3] == '-') {
			i = 5;

			while (i < size && !(data[i - 2] == '-' && data[i - 1] == '-' && data[i] == '>'))
				i++;

			i++;

			if (i < size)
				j = is_empty(data + i, size - i);

			if (j) {
				work.size = i + j;
				if (do_render && doc->md.blockhtml)
					doc->md.blockhtml(ob, &work, doc->data);
				return work.size;
			}
		}

		/* HR, which is the only self-closing block tag considered */
		if (size > 4 && (data[1] == 'h' || data[1] == 'H') && (data[2] == 'r' || data[2] == 'R')) {
			i = 3;
			while (i < size && data[i] != '>')
				i++;

			if (i + 1 < size) {
				i++;
				j = is_empty(data + i, size - i);
				if (j) {
					work.size = i + j;
					if (do_render && doc->md.blockhtml)
						doc->md.blockhtml(ob, &work, doc->data);
					return work.size;
				}
			}
		}

		/* no special case recognised */
		return 0;
	}

	/* looking for a matching closing tag in strict mode */
	tag_len = strlen(curtag);
	tag_end = htmlblock_find_end_strict(curtag, tag_len, doc, data, size);

	/* if not found, trying a second pass looking for indented match */
	/* but not if tag is "ins" or "del" (following original Markdown.pl) */
	if (!tag_end && strcmp(curtag, "ins") != 0 && strcmp(curtag, "del") != 0)
		tag_end = htmlblock_find_end(curtag, tag_len, doc, data, size);

	if (!tag_end)
		return 0;

	/* the end of the block has been found */
	work.size = tag_end;
	if (do_render && doc->md.blockhtml)
		doc->md.blockhtml(ob, &work, doc->data);

	return tag_end;
}

static void
parse_table_row(hbuf *ob, hdoc *doc, uint8_t *data, 
	size_t size, size_t columns, htbl_flags 
	*col_data, htbl_flags header_flag)
{
	size_t	 i = 0, col, len, cell_start, cell_end;
	hbuf 	*cell_work, *row_work = NULL;
	hbuf 	 empty_cell;

	if (!doc->md.table_cell || !doc->md.table_row)
		return;

	row_work = newbuf(doc, BUFFER_SPAN);

	if (i < size && data[i] == '|')
		i++;

	for (col = 0; col < columns && i < size; ++col) {
		cell_work = newbuf(doc, BUFFER_SPAN);

		while (i < size && xisspace(data[i]))
			i++;

		cell_start = i;

		len = find_emph_char(data + i, size - i, '|');

		/* 
		 * Two possibilities for len == 0:
		 * (1) No more pipe char found in the current line.
		 * (2) The next pipe is right after the current one,
		 * i.e. empty cell.
		 * For case 1, we skip to the end of line; for case 2 we
		 * just continue.
		 */

		if (len == 0 && i < size && data[i] != '|')
			len = size - i;
		i += len;

		cell_end = i - 1;

		while (cell_end > cell_start && 
		       xisspace(data[cell_end]))
			cell_end--;

		parse_inline(cell_work, doc, data + cell_start, 
			1 + cell_end - cell_start, buf_newln(ob));
		doc->md.table_cell(row_work, cell_work, 
			col_data[col] | header_flag, 
			doc->data, col, columns);

		popbuf(doc, BUFFER_SPAN);
		i++;
	}

	for ( ; col < columns; ++col) {
		memset(&empty_cell, 0, sizeof(hbuf));
		doc->md.table_cell(row_work, &empty_cell, 
			col_data[col] | header_flag, 
			doc->data, col, columns);
	}

	doc->md.table_row(ob, row_work, doc->data);
	popbuf(doc, BUFFER_SPAN);
}

static size_t
parse_table_header(hbuf *ob, hdoc *doc, uint8_t *data, 
	size_t size, size_t *columns, htbl_flags **column_data)
{
	size_t	 i = 0, col, header_end, under_end, dashes;
	ssize_t	 pipes = 0;

	while (i < size && data[i] != '\n')
		if (data[i++] == '|')
			pipes++;

	if (i == size || pipes == 0)
		return 0;

	header_end = i;

	while (header_end > 0 && xisspace(data[header_end - 1]))
		header_end--;

	if (data[0] == '|')
		pipes--;

	if (header_end && data[header_end - 1] == '|')
		pipes--;

	if (pipes < 0)
		return 0;

	*columns = pipes + 1;
	*column_data = xcalloc(*columns, sizeof(htbl_flags));

	/* Parse the header underline */

	i++;
	if (i < size && data[i] == '|')
		i++;

	under_end = i;
	while (under_end < size && data[under_end] != '\n')
		under_end++;

	for (col = 0; col < *columns && i < under_end; ++col) {
		dashes = 0;

		while (i < under_end && data[i] == ' ')
			i++;

		if (data[i] == ':') {
			i++; 
			(*column_data)[col] |= HTBL_ALIGN_LEFT;
			dashes++;
		}

		while (i < under_end && data[i] == '-') {
			i++; 
			dashes++;
		}

		if (i < under_end && data[i] == ':') {
			i++; 
			(*column_data)[col] |= HTBL_ALIGN_RIGHT;
			dashes++;
		}

		while (i < under_end && data[i] == ' ')
			i++;

		if (i < under_end && data[i] != '|' && data[i] != '+')
			break;

		if (dashes < 3)
			break;

		i++;
	}

	if (col < *columns)
		return 0;

	parse_table_row(ob, doc, data, header_end, 
		*columns, *column_data, HTBL_HEADER);
	return under_end + 1;
}

static size_t
parse_table(hbuf *ob, hdoc *doc, uint8_t *data, size_t size)
{
	size_t		 i, columns, row_start, pipes;
	hbuf		 *work = NULL, *header_work = NULL, 
			 *body_work = NULL;
	htbl_flags	*col_data = NULL;

	work = newbuf(doc, BUFFER_BLOCK);
	header_work = newbuf(doc, BUFFER_SPAN);
	body_work = newbuf(doc, BUFFER_BLOCK);

	i = parse_table_header(header_work, 
		doc, data, size, &columns, &col_data);

	if (i > 0) {
		while (i < size) {
			pipes = 0;
			row_start = i;

			while (i < size && data[i] != '\n')
				if (data[i++] == '|')
					pipes++;

			if (pipes == 0 || i == size) {
				i = row_start;
				break;
			}

			parse_table_row(
				body_work,
				doc,
				data + row_start,
				i - row_start,
				columns,
				col_data, 0
			);

			i++;
		}

		if (doc->md.table_header)
			doc->md.table_header(work, header_work, 
				doc->data, col_data, columns);
		if (doc->md.table_body)
			doc->md.table_body(work, body_work, doc->data);
		if (doc->md.table)
			doc->md.table(ob, work, doc->data);
	}

	free(col_data);
	popbuf(doc, BUFFER_SPAN);
	popbuf(doc, BUFFER_BLOCK);
	popbuf(doc, BUFFER_BLOCK);
	return i;
}

/* 
 * Parsing of one block, returning next uint8_t to parse.
 * We can assume, entering the block, that our output is newline
 * aligned.
 */
static void
parse_block(hbuf *ob, hdoc *doc, uint8_t *data, size_t size)
{
	size_t	 beg = 0, end, i;
	uint8_t	*txt_data;

	if (doc->work_bufs[BUFFER_SPAN].size +
	    doc->work_bufs[BUFFER_BLOCK].size > doc->max_nesting)
		return;

	/* 
	 * What kind of block are we?
	 * Go through all types of blocks, one by one.
	 */

	while (beg < size) {
		txt_data = data + beg;
		end = size - beg;

		/* We are at a #header. */

		if (is_atxheader(doc, txt_data, end)) {
			beg += parse_atxheader(ob, doc, txt_data, end);
			continue;
		}

		/* We have some <HTML>. */

		if (data[beg] == '<' && 
		    doc->md.blockhtml &&
		    (i = parse_htmlblock
		     (ob, doc, txt_data, end, 1)) != 0) {
			beg += i;
			continue;
		}

		/* Empty line. */

		if ((i = is_empty(txt_data, end)) != 0) {
			beg += i;
			continue;
		}

		/* Horizontal rule. */

		if (is_hrule(txt_data, end)) {
			if (doc->md.hrule)
				doc->md.hrule(ob, doc->data);
			while (beg < size && data[beg] != '\n')
				beg++;
			beg++;
			continue;
		} 

		/* Fenced code. */
		
		if ((doc->ext_flags & LOWDOWN_FENCED) != 0 &&
		    (i = parse_fencedcode
		     (ob, doc, txt_data, end)) != 0) {
			beg += i;
			continue;
		}

		/* Table parsing. */

		if ((doc->ext_flags & LOWDOWN_TABLES) != 0 &&
		    (i = parse_table(ob, doc, txt_data, end)) != 0) {
			beg += i;
			continue;
		}

		/* We're a > block quote. */

		if (prefix_quote(txt_data, end)) {
			beg += parse_blockquote
				(ob, doc, txt_data, end);
			continue;
		}

		/* Prefixed code (like block-quotes). */

		if ( ! (doc->ext_flags & LOWDOWN_NOCODEIND) && 
		    prefix_code(txt_data, end)) {
			beg += parse_blockcode(ob, doc, txt_data, end);
			continue;
		}

		/* Some sort of unordered list. */

		if (prefix_uli(txt_data, end)) {
			beg += parse_list(ob, doc, txt_data, end, 0);
			continue;
		}

		/* An ordered list. */

		if (prefix_oli(txt_data, end, NULL, NULL)) {
			beg += parse_list(ob, doc, 
				txt_data, end, HLIST_ORDERED);
			continue;
		}

		/* No match: just a regular paragraph. */

		beg += parse_paragraph(ob, doc, txt_data, end);
	}
}

/* 
 * Returns whether a line is a footnote definition or not.
 */
static int
is_footnote(const uint8_t *data, size_t beg, 
	size_t end, size_t *last, struct footnote_list *list)
{
	size_t	 i = 0, ind = 0, start = 0, id_offset, id_end;
	hbuf	*contents = NULL;
	int 	 in_empty = 0;

	/* up to 3 optional leading spaces */
	if (beg + 3 >= end) return 0;
	if (data[beg] == ' ') { i = 1;
	if (data[beg + 1] == ' ') { i = 2;
	if (data[beg + 2] == ' ') { i = 3;
	if (data[beg + 3] == ' ') return 0; } } }
	i += beg;

	/* id part: caret followed by anything between brackets */
	if (data[i] != '[') return 0;
	i++;
	if (i >= end || data[i] != '^') return 0;
	i++;
	id_offset = i;
	while (i < end && data[i] != '\n' && data[i] != '\r' && data[i] != ']')
		i++;
	if (i >= end || data[i] != ']') return 0;
	id_end = i;

	/* spacer: colon (space | tab)* newline? (space | tab)* */
	i++;
	if (i >= end || data[i] != ':') return 0;
	i++;

	/* getting content buffer */
	contents = hbuf_new(64);

	start = i;

	/* process lines similar to a list item */
	while (i < end) {
		while (i < end && data[i] != '\n' && data[i] != '\r') i++;

		/* process an empty line */
		if (is_empty(data + start, i - start)) {
			in_empty = 1;
			if (i < end && (data[i] == '\n' || data[i] == '\r')) {
				i++;
				if (i < end && data[i] == '\n' && data[i - 1] == '\r') i++;
			}
			start = i;
			continue;
		}

		/* calculating the indentation */
		ind = 0;
		while (ind < 4 && start + ind < end && data[start + ind] == ' ')
			ind++;

		/* joining only indented stuff after empty lines;
		 * note that now we only require 1 space of indentation
		 * to continue, just like lists */
		if (ind == 0) {
			if (start == id_end + 2 && data[start] == '\t') {}
			else break;
		}
		else if (in_empty) {
			hbuf_putc(contents, '\n');
		}

		in_empty = 0;

		/* adding the line into the content buffer */
		hbuf_put(contents, data + start + ind, i - start - ind);
		/* add carriage return */
		if (i < end) {
			hbuf_putc(contents, '\n');
			if (i < end && (data[i] == '\n' || data[i] == '\r')) {
				i++;
				if (i < end && data[i] == '\n' && data[i - 1] == '\r') i++;
			}
		}
		start = i;
	}

	if (last)
		*last = start;

	if (list) {
		struct footnote_ref *ref;
		ref = create_footnote_ref(list, data + id_offset, id_end - id_offset);
		if (!ref) {
			hbuf_free(contents);
			return 0;
		}
		add_footnote_ref(list, ref);
		ref->contents = contents;
	} else
		hbuf_free(contents);

	return 1;
}

/* 
 * Returns whether a line is a reference or not.
 */
static int
is_ref(const uint8_t *data, size_t beg, 
	size_t end, size_t *last, struct link_ref **refs)
{
	size_t	 i = 0, id_offset, id_end, link_offset, 
		 link_end, title_offset, title_end, line_end;

	/* up to 3 optional leading spaces */
	if (beg + 3 >= end) return 0;
	if (data[beg] == ' ') { i = 1;
	if (data[beg + 1] == ' ') { i = 2;
	if (data[beg + 2] == ' ') { i = 3;
	if (data[beg + 3] == ' ') return 0; } } }
	i += beg;

	/* id part: anything but a newline between brackets */
	if (data[i] != '[') return 0;
	i++;
	id_offset = i;
	while (i < end && data[i] != '\n' && data[i] != '\r' && data[i] != ']')
		i++;
	if (i >= end || data[i] != ']') return 0;
	id_end = i;

	/* spacer: colon (space | tab)* newline? (space | tab)* */
	i++;
	if (i >= end || data[i] != ':') return 0;
	i++;
	while (i < end && data[i] == ' ') i++;
	if (i < end && (data[i] == '\n' || data[i] == '\r')) {
		i++;
		if (i < end && data[i] == '\r' && data[i - 1] == '\n') i++; }
	while (i < end && data[i] == ' ') i++;
	if (i >= end) return 0;

	/* link: spacing-free sequence, optionally between angle brackets */
	if (data[i] == '<')
		i++;

	link_offset = i;

	while (i < end && data[i] != ' ' && data[i] != '\n' && data[i] != '\r')
		i++;

	if (data[i - 1] == '>') link_end = i - 1;
	else link_end = i;

	/* optional spacer: (space | tab)* (newline | '\'' | '"' | '(' ) */
	while (i < end && data[i] == ' ') i++;
	if (i < end && data[i] != '\n' && data[i] != '\r'
			&& data[i] != '\'' && data[i] != '"' && data[i] != '(')
		return 0;
	line_end = 0;
	/* computing end-of-line */
	if (i >= end || data[i] == '\r' || data[i] == '\n') line_end = i;
	if (i + 1 < end && data[i] == '\n' && data[i + 1] == '\r')
		line_end = i + 1;

	/* optional (space|tab)* spacer after a newline */
	if (line_end) {
		i = line_end + 1;
		while (i < end && data[i] == ' ') i++; }

	/* optional title: any non-newline sequence enclosed in '"()
					alone on its line */
	title_offset = title_end = 0;
	if (i + 1 < end
	&& (data[i] == '\'' || data[i] == '"' || data[i] == '(')) {
		i++;
		title_offset = i;
		/* looking for EOL */
		while (i < end && data[i] != '\n' && data[i] != '\r') i++;
		if (i + 1 < end && data[i] == '\n' && data[i + 1] == '\r')
			title_end = i + 1;
		else	title_end = i;
		/* stepping back */
		i -= 1;
		while (i > title_offset && data[i] == ' ')
			i -= 1;
		if (i > title_offset
		&& (data[i] == '\'' || data[i] == '"' || data[i] == ')')) {
			line_end = title_end;
			title_end = i; } }

	if (!line_end || link_end == link_offset)
		return 0; /* garbage after the link empty link */

	/* a valid ref has been found, filling-in return structures */
	if (last)
		*last = line_end;

	if (refs) {
		struct link_ref *ref;

		ref = add_link_ref(refs, data + id_offset, id_end - id_offset);
		if (!ref)
			return 0;

		ref->link = hbuf_new(link_end - link_offset);
		hbuf_put(ref->link, data + link_offset, link_end - link_offset);

		if (title_end > title_offset) {
			ref->title = hbuf_new(title_end - title_offset);
			hbuf_put(ref->title, data + title_offset, title_end - title_offset);
		}
	}

	return 1;
}

static void 
expand_tabs(hbuf *ob, const uint8_t *line, size_t size)
{
	size_t  i = 0, tab = 0, org;

	/* 
	 * This code makes two assumptions:
	 *
	 * (1) Input is valid UTF-8.  (Any byte with top two bits 10 is
	 * skipped, whether or not it is a valid UTF-8 continuation
	 * byte.)
	 * (2) Input contains no combining characters.  (Combining
	 * characters should be skipped but are not.)
	 */

	while (i < size) {
		org = i;

		while (i < size && line[i] != '\t') {
			/* ignore UTF-8 continuation bytes */
			if ((line[i] & 0xc0) != 0x80)
				tab++;
			i++;
		}

		if (i > org)
			hbuf_put(ob, line + org, i - org);

		if (i >= size)
			break;

		do {
			hbuf_putc(ob, ' '); 
			tab++;
		} while (tab % 4);

		i++;
	}
}

/*
 * Allocate a new document processor instance.
 */
hdoc *
hdoc_new(const hrend *renderer, const struct lowdown_opts *opts,
	unsigned int extensions, size_t max_nesting, int link_nospace)
{
	hdoc *doc = NULL;

	assert(max_nesting > 0 && renderer);

	doc = xmalloc(sizeof(hdoc));
	memcpy(&doc->md, renderer, sizeof(hrend));

	doc->opts = opts;
	doc->data = renderer->opaque;
	doc->link_nospace = link_nospace;

	hstack_init(&doc->work_bufs[BUFFER_BLOCK], 4);
	hstack_init(&doc->work_bufs[BUFFER_SPAN], 8);

	memset(doc->active_char, 0x0, 256);

	if (doc->md.emphasis || 
	    doc->md.double_emphasis || 
	    doc->md.triple_emphasis) {
		doc->active_char['*'] = MD_CHAR_EMPHASIS;
		doc->active_char['_'] = MD_CHAR_EMPHASIS;
		if (extensions & LOWDOWN_STRIKE)
			doc->active_char['~'] = MD_CHAR_EMPHASIS;
		if (extensions & LOWDOWN_HILITE)
			doc->active_char['='] = MD_CHAR_EMPHASIS;
	}

	if (doc->md.codespan)
		doc->active_char['`'] = MD_CHAR_CODESPAN;

	if (doc->md.linebreak)
		doc->active_char['\n'] = MD_CHAR_LINEBREAK;

	if (doc->md.image || doc->md.link || doc->md.footnotes || doc->md.footnote_ref) {
		doc->active_char['['] = MD_CHAR_LINK;
		doc->active_char['!'] = MD_CHAR_IMAGE;
	}

	doc->active_char['<'] = MD_CHAR_LANGLE;
	doc->active_char['\\'] = MD_CHAR_ESCAPE;
	doc->active_char['&'] = MD_CHAR_ENTITY;

	if (extensions & LOWDOWN_AUTOLINK) {
		doc->active_char[':'] = MD_CHAR_AUTOLINK_URL;
		doc->active_char['@'] = MD_CHAR_AUTOLINK_EMAIL;
		doc->active_char['w'] = MD_CHAR_AUTOLINK_WWW;
	}

	if (extensions & LOWDOWN_SUPER)
		doc->active_char['^'] = MD_CHAR_SUPERSCRIPT;

	if (extensions & LOWDOWN_MATH)
		doc->active_char['$'] = MD_CHAR_MATH;

	/* Extension data */
	doc->ext_flags = extensions;
	doc->max_nesting = max_nesting;
	doc->in_link_body = 0;

	return doc;
}

/*
 * Parse MMD meta-data.
 * This consists of key-value pairs.
 * Returns zero if this is not metadata, non-zero of it is.
 */
static int
parse_metadata(hdoc *doc, const uint8_t *data, size_t sz,
	struct lowdown_meta **meta, size_t *metasz)
{
	size_t	 	 i, len, pos = 0;
	const uint8_t	*key, *val;
	struct lowdown_meta *m;
	char		*cp;
	
	if (0 == sz || '\n' != data[sz - 1])
		return(0);

	/* 
	 * Check the first line for a colon to see if we should do
	 * metadata parsing at all.
	 * This is a convenience for regular markdown so that initial
	 * lines (not headers) don't get sucked into metadata.
	 */

	for (pos = 0; pos < sz; pos++)
		if ('\n' == data[pos] || ':' == data[pos])
			break;

	if (pos == sz || '\n' == data[pos])
		return(0);

	for (pos = 0; pos < sz; ) {
		key = &data[pos];
		for (i = pos; i < sz; i++)
			if (':' == data[i])
				break;

		*meta = xreallocarray
			(*meta, *metasz + 1,
			 sizeof(struct lowdown_meta));
		m = &(*meta)[(*metasz)++];
		memset(m, 0, sizeof(struct lowdown_meta));

		m->key = xstrndup((char *)key, i - pos);
		if (i == sz) {
			m->value = xstrndup((char *)key, 0);
			break;
		}

		val = &data[++i];
		pos = i;

		for ( ; i < sz; i++)
			if ('\n' == data[i] &&
			    (i == sz - 1 || ! isspace((int)data[i + 1])))
				break;

		assert(i < sz);
		m->value = xstrndup((char *)val, i - pos);
		pos = i + 1;
	}

	/*
	 * Convert metadata keys into normalised form: lowercase
	 * alphanumerics, hyphen, underscore, with spaces stripped.
	 */

	for (i = 0; i < *metasz; i++) {
		cp = (*meta)[i].key;
		while ('\0' != *cp) {
			if (isalnum((int)*cp) ||
			    '-' == *cp || '_' == *cp) {
				*cp = tolower((int)*cp);
				cp++;
				continue;
			} else if (isspace((int)*cp)) {
				len = strlen(cp + 1) + 1;
				memmove(cp, cp + 1, len);
				continue;
			} 
			lmsg(doc->opts, 
				LOWDOWN_ERR_METADATA_BAD_CHAR, 
				NULL);
			*cp++ = '?';
		}
	}

	return(1);
}

/*
 * Render regular Markdown using the document processor.
 * If both mp and mszp are not NULL, set them with the meta information
 * instead of locally destroying it.
 * (Obviously only applicable if LOWDOWN_METADATA has been set.)
 */
void
hdoc_render(hdoc *doc, hbuf *ob, const uint8_t *data,
	size_t size, struct lowdown_meta **mp, size_t *mszp)
{
	static const uint8_t UTF8_BOM[] = {0xEF, 0xBB, 0xBF};
	hbuf		*text;
	size_t		 beg, end;
	int		 footnotes_enabled;
	const uint8_t	*sv;
	struct lowdown_meta *m = NULL;
	size_t		 i, msz = 0;

	text = hbuf_new(64);

	/*
	 * Preallocate enough space for our buffer to avoid expanding
	 * while copying.
	 */

	hbuf_grow(text, size);

	/* Reset the references table. */

	memset(&doc->refs, 0x0, REF_TABLE_SIZE * sizeof(void *));

	footnotes_enabled = doc->ext_flags & LOWDOWN_FOOTNOTES;

	/* Reset the footnotes lists. */

	if (footnotes_enabled) {
		memset(&doc->footnotes_found, 0x0,
			sizeof(doc->footnotes_found));
		memset(&doc->footnotes_used, 0x0,
			sizeof(doc->footnotes_used));
	}

	/*
	 * Skip a possible UTF-8 BOM, even though the Unicode standard
	 * discourages having these in UTF-8 documents.
	 */

	beg = 0;
	if (size >= 3 && memcmp(data, UTF8_BOM, 3) == 0)
		beg += 3;

	/*
	 * Zeroth pass: see if we should collect metadata.
	 * Only do so if we're toggled to look for metadata.
	 * (Only parse if we must.)
	 */

	if (LOWDOWN_METADATA & doc->ext_flags &&
	    beg < size - 1 && isalnum((int)data[beg])) {
		sv = &data[beg];
		for (end = beg + 1; end < size; end++) {
			if ('\n' == data[end] &&
			    '\n' == data[end - 1])
				break;
		}
		if (parse_metadata(doc, sv, end - beg, &m, &msz))
			beg = end + 1;
	}

	/* First pass: looking for references, copying everything else. */

	while (beg < size)
		if (footnotes_enabled &&
		    is_footnote(data, beg, size, &end, &doc->footnotes_found))
			beg = end;
		else if (is_ref(data, beg, size, &end, doc->refs))
			beg = end;
		else {
			/* Skipping to the next line. */
			end = beg;
			while (end < size && data[end] != '\n' &&
			       data[end] != '\r')
				end++;

			/* Adding the line body if present. */
			if (end > beg)
				expand_tabs(text, data + beg, end - beg);

			while (end < size && (data[end] == '\n' ||
			       data[end] == '\r')) {
				/* Add one \n per newline. */
				if (data[end] == '\n' ||
				    (end + 1 < size && data[end + 1] != '\n'))
					hbuf_putc(text, '\n');
				end++;
			}

			beg = end;
		}

	/* Pre-grow the output buffer to minimize allocations. */

	hbuf_grow(ob, text->size + (text->size >> 1));

	/* Second pass: actual rendering. */

	if (doc->md.doc_header)
		doc->md.doc_header(ob, 0, doc->data);

	doc->start = text->data;

	if (text->size) {
		/* Adding a final newline if not already present. */
		if (text->data[text->size - 1] != '\n' &&
		    text->data[text->size - 1] != '\r')
			hbuf_putc(text, '\n');

		sv = doc->start;
		doc->start = text->data;
		parse_block(ob, doc, text->data, text->size);
		doc->start = sv;
	}

	/* Footnotes. */

	if (footnotes_enabled)
		parse_footnote_list(ob, doc, &doc->footnotes_used);

	if (doc->md.doc_footer)
		doc->md.doc_footer(ob, 0, doc->data);

	/* Clean-up. */

	hbuf_free(text);
	free_link_refs(doc->refs);
	if (footnotes_enabled) {
		free_footnote_list(&doc->footnotes_found, 1);
		free_footnote_list(&doc->footnotes_used, 0);
	}

	assert(doc->work_bufs[BUFFER_SPAN].size == 0);
	assert(doc->work_bufs[BUFFER_BLOCK].size == 0);

	/* Only if both. */

	if (NULL != mp && NULL != mszp) {
		*mp = m;
		*mszp = msz;
		m = NULL;
		msz = 0;
	}

	for (i = 0; i < msz; i++) {
		free(m[i].key);
		free(m[i].value);
	}

	free(m);
}

/*
 * Deallocate a document processor instance.
 */
void
hdoc_free(hdoc *doc)
{
	size_t	 i;

	for (i = 0; i < (size_t)doc->work_bufs[BUFFER_SPAN].asize; ++i)
		hbuf_free(doc->work_bufs[BUFFER_SPAN].item[i]);

	for (i = 0; i < (size_t)doc->work_bufs[BUFFER_BLOCK].asize; ++i)
		hbuf_free(doc->work_bufs[BUFFER_BLOCK].item[i]);

	hstack_uninit(&doc->work_bufs[BUFFER_SPAN]);
	hstack_uninit(&doc->work_bufs[BUFFER_BLOCK]);
	free(doc);
}
