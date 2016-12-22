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
#include <err.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

static void
hbuf_init(hbuf *buf, size_t unit, int buffer_free)
{
	assert(buf);

	buf->data = NULL;
	buf->size = buf->asize = 0;
	buf->unit = unit;
	buf->buffer_free = buffer_free;
}

/* allocate a new buffer */
hbuf *
hbuf_new(size_t unit)
{
	hbuf *ret = xmalloc(sizeof (hbuf));
	hbuf_init(ret, unit, 1);
	return ret;
}

void
hoedown_buffer_free(hbuf *buf)
{
	if (!buf) return;
	assert(buf && buf->unit);

	free(buf->data);

	if (buf->buffer_free)
		free(buf);
}

/* increase the allocated size to the given value */
void
hbuf_grow(hbuf *buf, size_t neosz)
{
	size_t neoasz;
	assert(buf && buf->unit);

	if (buf->asize >= neosz)
		return;

	neoasz = buf->asize + buf->unit;
	while (neoasz < neosz)
		neoasz += buf->unit;

	buf->data = xrealloc(buf->data, neoasz);
	buf->asize = neoasz;
}

/* append raw data to a buffer */
void
hbuf_put(hbuf *buf, const uint8_t *data, size_t size)
{
	assert(buf && buf->unit);

	if (buf->size + size > buf->asize)
		hbuf_grow(buf, buf->size + size);

	memcpy(buf->data + buf->size, data, size);
	buf->size += size;
}

/* append a nil-terminated string to a buffer */
void
hbuf_puts(hbuf *buf, const char *str)
{

	hbuf_put(buf, (const uint8_t *)str, strlen(str));
}

/* append a single char to a buffer */
void
hbuf_putc(hbuf *buf, uint8_t c)
{
	assert(buf && buf->unit);

	if (buf->size >= buf->asize)
		hbuf_grow(buf, buf->size + 1);

	buf->data[buf->size] = c;
	buf->size += 1;
}

int
hoedown_buffer_putf(hbuf *buf, FILE *file)
{
	assert(buf && buf->unit);

	while (!(feof(file) || ferror(file))) {
		hbuf_grow(buf, buf->size + buf->unit);
		buf->size += fread(buf->data + buf->size, 1, buf->unit, file);
	}

	return ferror(file);
}

void
hoedown_buffer_set(hbuf *buf, const uint8_t *data, size_t size)
{
	assert(buf && buf->unit);

	if (size > buf->asize)
		hbuf_grow(buf, size);

	memcpy(buf->data, data, size);
	buf->size = size;
}

void
hoedown_buffer_sets(hbuf *buf, const char *str)
{
	hoedown_buffer_set(buf, (const uint8_t *)str, strlen(str));
}

int
hoedown_buffer_eq(const hbuf *buf, const uint8_t *data, size_t size)
{
	if (buf->size != size) return 0;
	return memcmp(buf->data, data, size) == 0;
}

int
hoedown_buffer_eqs(const hbuf *buf, const char *str)
{
	return hoedown_buffer_eq(buf, (const uint8_t *)str, strlen(str));
}

int
hoedown_buffer_prefix(const hbuf *buf, const char *prefix)
{
	size_t i;

	for (i = 0; i < buf->size; ++i) {
		if (prefix[i] == 0)
			return 0;

		if (buf->data[i] != prefix[i])
			return buf->data[i] - prefix[i];
	}

	return 0;
}

void
hoedown_buffer_slurp(hbuf *buf, size_t size)
{
	assert(buf && buf->unit);

	if (size >= buf->size) {
		buf->size = 0;
		return;
	}

	buf->size -= size;
	memmove(buf->data, buf->data + size, buf->size);
}

const char *
hoedown_buffer_cstr(hbuf *buf)
{
	assert(buf && buf->unit);

	if (buf->size < buf->asize && buf->data[buf->size] == 0)
		return (char *)buf->data;

	hbuf_grow(buf, buf->size + 1);
	buf->data[buf->size] = 0;

	return (char *)buf->data;
}

void
hoedown_buffer_printf(hbuf *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	assert(buf && buf->unit);

	if (buf->size >= buf->asize)
		hbuf_grow(buf, buf->size + 1);

	va_start(ap, fmt);
	n = vsnprintf((char *)buf->data + buf->size, buf->asize - buf->size, fmt, ap);
	va_end(ap);

	if (n < 0) 
		return;

	if ((size_t)n >= buf->asize - buf->size) {
		hbuf_grow(buf, buf->size + n + 1);

		va_start(ap, fmt);
		n = vsnprintf((char *)buf->data + buf->size, buf->asize - buf->size, fmt, ap);
		va_end(ap);
	}

	if (n < 0)
		return;

	buf->size += n;
}

void hoedown_buffer_put_utf8(hbuf *buf, unsigned int c) {
	unsigned char unichar[4];

	assert(buf && buf->unit);

	if (c < 0x80) {
		hbuf_putc(buf, c);
	}
	else if (c < 0x800) {
		unichar[0] = 192 + (c / 64);
		unichar[1] = 128 + (c % 64);
		hbuf_put(buf, unichar, 2);
	}
	else if (c - 0xd800u < 0x800) {
		HOEDOWN_BUFPUTSL(buf, "\xef\xbf\xbd");
	}
	else if (c < 0x10000) {
		unichar[0] = 224 + (c / 4096);
		unichar[1] = 128 + (c / 64) % 64;
		unichar[2] = 128 + (c % 64);
		hbuf_put(buf, unichar, 3);
	}
	else if (c < 0x110000) {
		unichar[0] = 240 + (c / 262144);
		unichar[1] = 128 + (c / 4096) % 64;
		unichar[2] = 128 + (c / 64) % 64;
		unichar[3] = 128 + (c % 64);
		hbuf_put(buf, unichar, 4);
	}
	else {
		HOEDOWN_BUFPUTSL(buf, "\xef\xbf\xbd");
	}
}
