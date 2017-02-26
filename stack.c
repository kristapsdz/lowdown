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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

/* initialize a stack */
void
hstack_init(hstack *st, size_t initial_size)
{

	assert(st);

	st->item = NULL;
	st->size = st->asize = 0;

	if (!initial_size)
		initial_size = 8;

	hstack_grow(st, initial_size);
}

/* free internal data of the stack */
void
hstack_uninit(hstack *st)
{
	assert(st);

	free(st->item);
}

/* increase the allocated size to the given value */
void
hstack_grow(hstack *st, size_t neosz)
{
	assert(st);

	if (st->asize >= neosz)
		return;

	st->item = xrealloc(st->item, neosz * sizeof(void *));
	memset(st->item + st->asize, 0x0, (neosz - st->asize) * sizeof(void *));

	st->asize = neosz;

	if (st->size > neosz)
		st->size = neosz;
}

/* push an item to the top of the stack */
void
hstack_push(hstack *st, void *item)
{
	assert(st);

	if (st->size >= st->asize)
		hstack_grow(st, st->size * 2);

	st->item[st->size++] = item;
}

/* retrieve the item at the top of the stack */
void *
hstack_top(const hstack *st)
{
	assert(st);

	if (!st->size)
		return NULL;

	return st->item[st->size - 1];
}
