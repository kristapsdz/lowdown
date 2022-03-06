/*	$Id$ */
/*
 * Copyright (c) 2020, Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lowdown.h"
#include "extern.h"

struct ent {
	const char 	*iso;
	uint32_t	 unicode;
	const char	*roff;
	const char	*tex;
	unsigned char	 texflags;
};

static const struct ent ents[] = {
	{ "AElig", 	198,	NULL,	"AE{}",		0 },
	{ "Aacute", 	193,	NULL,	"'{A}",		0 },
	{ "Acirc", 	194,	NULL,	"^{A}",		0 },
	{ "Agrave", 	192,	NULL,	"`{A}",		0 },
	{ "Alpha",	913,	NULL,	"A",		TEX_ENT_ASCII },
	{ "Aring", 	197,	NULL,	"AA{}",		0 },
	{ "Atilde", 	195,	NULL,	"~{A}",		0 },
	{ "Auml", 	196,	NULL,	"\"{A}",	0 },
	{ "Beta",	914,	NULL,	"B",		TEX_ENT_ASCII },
	{ "Ccedil", 	199,	NULL,	"c{C}",		0 },
	{ "Chi",	935,	NULL,	"X",		TEX_ENT_ASCII },
	{ "Dagger",	8225,	NULL,	"ddag{}",	0 },
	{ "Delta",	916,	NULL,	"Delta",	TEX_ENT_MATH },
	{ "ETH", 	208,	NULL,	"DH{}",		0 },
	{ "Eacute", 	201,	NULL,	"'{E}",		0 },
	{ "Ecirc", 	202,	NULL,	"^{E}",		0 },
	{ "Egrave", 	200,	NULL,	"`{E}",		0 },
	{ "Epsilon",	917,	NULL,	"E",		TEX_ENT_ASCII },
	{ "Eta",	919,	NULL,	"E",		TEX_ENT_ASCII },
	{ "Euml", 	203,	NULL,	"\"{E}",	0 },
	{ "Gamma",	915,	NULL,	"Gamma",	TEX_ENT_MATH },
	{ "Iacute", 	205,	NULL,	"'{I}",		0 },
	{ "Icirc", 	206,	NULL,	"^{I}",		0 },
	{ "Igrave", 	204,	NULL,	"`{I}",		0 },
	{ "Iota",	921,	NULL,	"I",		TEX_ENT_ASCII },
	{ "Iuml", 	207,	NULL,	"\"{I}",	0 },
	{ "Kappa",	922,	NULL,	"K",		TEX_ENT_ASCII },
	{ "Lambda",	923,	NULL,	"Lambda",	TEX_ENT_MATH },
	{ "Mu",		924,	NULL,	"M",		TEX_ENT_ASCII },
	{ "Ntilde", 	209,	NULL,	"~{N}",		0 },
	{ "Nu",		925,	NULL,	"N",		TEX_ENT_ASCII },
	{ "OElig",	338,	NULL,	"OE{}",		0 },
	{ "Oacute", 	211,	NULL,	"'{O}",		0 },
	{ "Ocirc", 	212,	NULL,	"^{O}",		0 },
	{ "Ograve", 	210,	NULL,	"`{O}",		0 },
	{ "Omega",	937,	NULL,	"Omega",	TEX_ENT_MATH },
	{ "Omicron",	927,	NULL,	"O",		TEX_ENT_ASCII },
	{ "Oslash", 	216,	NULL,	"O{}",		0 },
	{ "Otilde", 	213,	NULL,	"~{O}",		0 },
	{ "Ouml", 	214,	NULL,	"\"{O}",	0 },
	{ "Phi",	934,	NULL,	"Phi",		TEX_ENT_MATH },
	{ "Pi",		928,	NULL,	"Pi",		TEX_ENT_MATH },
	{ "Prime",	8243,	NULL,	"{``}",		TEX_ENT_ASCII },
	{ "Psi",	936,	NULL,	"Psi",		TEX_ENT_MATH },
	{ "Rho",	929,	NULL,	"R",		TEX_ENT_ASCII },
	{ "Scaron",	352,	NULL,	"v{S}",		0 },
	{ "Sigma",	931,	NULL,	"Sigma",	TEX_ENT_MATH },
	{ "THORN", 	222,	NULL,	"TH{}",		0 },
	{ "Tau",	932,	NULL,	"T",		TEX_ENT_ASCII },
	{ "Theta",	920,	NULL,	"Theta",	TEX_ENT_MATH },
	{ "Uacute", 	218,	NULL,	"'{U}",		0 },
	{ "Ucirc", 	219,	NULL,	"^{U}",		0 },
	{ "Ugrave", 	217,	NULL,	"`{U}",		0 },
	{ "Upsilon",	933,	NULL,	"Upsilon",	TEX_ENT_MATH },
	{ "Uuml", 	220,	NULL,	"\"{U}",	0 },
	{ "Xi",		926,	NULL,	"Xi",		TEX_ENT_MATH },
	{ "Yacute", 	221,	NULL,	"'{Y}",		0 },
	{ "Yuml",	376,	NULL,	"\"{Y}",	0 },
	{ "Zeta",	918,	NULL,	"Z",		TEX_ENT_ASCII },
	{ "aacute", 	225,	NULL,	"'{a}",		0 },
	{ "acirc", 	226,	NULL,	"^{a}",		0 },
	{ "acute", 	180,	NULL,	"'{}",		0 },
	{ "aelig", 	230,	NULL,	"ae{}",		0 },
	{ "agrave", 	224,	NULL,	"`{a}",		0 },
	{ "alefsym",	8501,	NULL,	"aleph",	TEX_ENT_MATH },
	{ "alpha",	945,	NULL,	"alpha",	TEX_ENT_MATH },
	{ "amp",	38,	NULL,	"&{}",		0 },
	{ "and",	8743,	NULL,	"wedge",	TEX_ENT_MATH },
	{ "ang",	8736,	NULL,	"angle",	TEX_ENT_MATH },
	{ "aring", 	229,	NULL,	"aa{}",		0 },
	{ "asymp",	8776,	NULL,	"asymp",	TEX_ENT_MATH },
	{ "atilde", 	227,	NULL,	"~{a}",		0 },
	{ "auml", 	228,	NULL,	"\"{a}",	0 },
	{ "bdquo",	8222,	NULL,	NULL,		0 }, /* XXX */
	{ "beta",	946,	NULL,	"beta",		TEX_ENT_MATH },
	{ "brvbar", 	166,	NULL,	"textbrokenbar{}",	0 },
	{ "bull",	8226,	NULL,	"textbullet{}",	0 },
	{ "cap",	8745,	NULL,	"cap",		TEX_ENT_MATH },
	{ "ccedil", 	231,	NULL,	"c{c}",		0 },
	{ "cedil", 	184,	NULL,	"c{}",		0 },
	{ "cent", 	162,	NULL,	"textcent{}",	0 },
	{ "chi",	967,	NULL,	"chi",		TEX_ENT_MATH },
	{ "circ",	710,	NULL,	"^{}",		0 },
	{ "cong",	8773,	NULL,	"cong",		TEX_ENT_MATH },
	{ "copy", 	169,	NULL,	"copyright{}",	0 },
	{ "crarr",	8629,	NULL,	NULL,		0 }, /* XXX */
	{ "cup",	8746,	NULL,	"cup",		TEX_ENT_MATH },
	{ "curren", 	164,	NULL,	"textcurrency{}", 0 },
	{ "dArr",	8659,	NULL,	"Downarrow",	TEX_ENT_MATH },
	{ "dagger",	8224,	NULL,	"dag{}",	0 },
	{ "darr",	8595,	NULL,	"downarrow",	TEX_ENT_MATH },
	{ "deg", 	176,	NULL,	"textdegree{}",	0 },
	{ "delta",	948,	NULL,	"delta",	TEX_ENT_MATH },
	{ "divide", 	247,	NULL,	"div",		TEX_ENT_MATH },
	{ "eacute", 	233,	NULL,	"'{e}",		0 },
	{ "ecirc", 	234,	NULL,	"^{e}",		0 },
	{ "egrave", 	232,	NULL,	"`{e}",		0 },
	{ "empty",	8709,	NULL,	"emptyset",	TEX_ENT_MATH },
	{ "emsp",	8195,	NULL,	"hspace{1em}",	0 },
	{ "ensp",	8194,	NULL,	"hspace{0.5em}", 0 },
	{ "epsilon",	949,	NULL,	"epsilon",	TEX_ENT_MATH },
	{ "equiv",	8801,	NULL,	"equiv",	TEX_ENT_MATH },
	{ "eta",	951,	NULL,	"eta",		TEX_ENT_MATH },
	{ "eth", 	240,	NULL,	"dh{}",		0 },
	{ "euml", 	235,	NULL,	"\"{e}",	0 },
	{ "euro",	8364,	NULL,	"texteuro{}",	0 },
	{ "exist",	8707,	NULL,	"exists",	TEX_ENT_MATH },
	{ "fnof",	402,	NULL,	"f",		TEX_ENT_MATH },
	{ "forall",	8704,	NULL,	"forall",	TEX_ENT_MATH },
	{ "frac12", 	189,	NULL,	"sfrac{1}{2}",	TEX_ENT_MATH },
	{ "frac14", 	188,	NULL,	"sfrac{1}{4}",	TEX_ENT_MATH },
	{ "frac34", 	190,	NULL,	"sfrac{3}{4}",	TEX_ENT_MATH },
	{ "frasl",	8260,	NULL,	NULL,		0 }, /* XXX */
	{ "gamma",	947,	NULL,	"gamma",	TEX_ENT_MATH },
	{ "ge",		8805,	NULL,	"geq",		TEX_ENT_MATH },
	{ "gt",		62,	NULL,	"textgreater{}", 0 },
	{ "hArr",	8660,	NULL,	"Leftrightarrow", TEX_ENT_MATH },
	{ "harr",	8596,	NULL,	"leftrightarrow", TEX_ENT_MATH },
	{ "hellip",	8230,	NULL,	"ldots{}",	0 },
	{ "iacute", 	237,	NULL,	"'{i}",		0 },
	{ "icirc", 	238,	NULL,	"^{i}",		0 },
	{ "iexcl", 	161,	NULL,	"textexclamdown{}", 0 },
	{ "igrave", 	236,	NULL,	"`{i}",		0 },
	{ "image",	8465,	NULL,	"Im",		TEX_ENT_MATH },
	{ "infin",	8734,	NULL,	"infty",	TEX_ENT_MATH },
	{ "int",	8747,	NULL,	"int",		TEX_ENT_MATH },
	{ "iota",	953,	NULL,	"iota",		TEX_ENT_MATH },
	{ "iquest", 	191,	NULL,	"textquestiondown{}", 0 },
	{ "isin",	8712,	NULL,	"in",		TEX_ENT_MATH },
	{ "iuml", 	239,	NULL,	"\"{i}",	0 },
	{ "kappa",	954,	NULL,	"kappa",	TEX_ENT_MATH },
	{ "lArr",	8656,	NULL,	"Leftarrow",	TEX_ENT_MATH },
	{ "lambda",	955,	NULL,	"lambda",	TEX_ENT_MATH },
	{ "lang",	9001,	NULL,	"langle",	TEX_ENT_MATH },
	{ "laquo", 	171,	NULL,	"guillemetleft{}", 0 },
	{ "larr",	8592,	NULL,	"leftarrow",	TEX_ENT_MATH },
	{ "lceil",	8968,	NULL,	"lceil",	TEX_ENT_MATH },
	{ "ldquo",	8220,	NULL,	"``",		TEX_ENT_ASCII },
	{ "le",		8804,	NULL,	"leq",		TEX_ENT_MATH },
	{ "lfloor",	8970,	NULL,	"lfloor",	TEX_ENT_MATH },
	{ "lowast",	8727,	NULL,	"_\\ast",	TEX_ENT_MATH },
	{ "lrm",	8206,	NULL,	NULL,		0 }, /* XXX */
	{ "lsaquo",	8249,	NULL,	NULL,		0 },
	{ "lsquo",	8216,	NULL,	"`",		TEX_ENT_ASCII },
	{ "lt",		60,	NULL,	"textless{}",	0 },
	{ "macr", 	175,	NULL,	"={}",		0 },
	{ "mdash",	8212,	"em",	"---",		TEX_ENT_ASCII },
	{ "micro", 	181,	NULL,	"textmu{}",	0 },
	{ "middot", 	183,	NULL,	"textperiodcentered{}", 0 },
	{ "minus",	8722,	NULL,	"-{}",		0 },
	{ "mu",		956,	NULL,	"mu",		TEX_ENT_MATH },
	{ "nabla",	8711,	NULL,	"nabla",	TEX_ENT_MATH },
	{ "nbsp", 	160,	NULL,	"~",		TEX_ENT_ASCII },
	{ "ndash",	8211,	NULL,	"--",		TEX_ENT_ASCII },
	{ "ne",		8800,	NULL,	"not=",		TEX_ENT_MATH },
	{ "ni",		8715,	NULL,	"ni",		TEX_ENT_MATH },
	{ "not", 	172,	NULL,	"lnot",		TEX_ENT_MATH },
	{ "notin",	8713,	NULL,	"not\\in",	TEX_ENT_MATH },
	{ "nsub",	8836,	NULL,	"not\\subset",	TEX_ENT_MATH },
	{ "ntilde", 	241,	NULL,	"~{n}",		0 },
	{ "nu",		957,	NULL,	"nu",		TEX_ENT_MATH },
	{ "oacute", 	243,	NULL,	"'{o}",		0 },
	{ "ocirc", 	244,	NULL,	"^{o}",		0 },
	{ "oelig",	339,	NULL,	"oe{}",		0 },
	{ "ograve", 	242,	NULL,	"`{o}",		0 },
	{ "oline",	8254,	NULL,	"ominus",	TEX_ENT_MATH },
	{ "omega",	969,	NULL,	"omega",	TEX_ENT_MATH },
	{ "omicron",	959,	NULL,	"omicron",	TEX_ENT_MATH },
	{ "oplus",	8853,	NULL,	"oplus",	TEX_ENT_MATH },
	{ "or",		8744,	NULL,	"vee",		TEX_ENT_MATH },
	{ "ordf", 	170,	NULL,	"textordfeminine{}", 0 },
	{ "ordm", 	186,	NULL,	"textordmasculine{}", 0 },
	{ "oslash", 	248,	NULL,	"oslash",	TEX_ENT_MATH },
	{ "otilde", 	245,	NULL,	"~{o}",		0 },
	{ "otimes",	8855,	NULL,	"otimes",	TEX_ENT_MATH },
	{ "ouml", 	246,	NULL,	"\"{o}",	0 },
	{ "para", 	182,	NULL,	"P{}",		0 },
	{ "part",	8706,	NULL,	"partial",	TEX_ENT_MATH },
	{ "permil",	8240,	NULL,	"textperthousand{}", 0 },
	{ "perp",	8869,	NULL,	"perp",		TEX_ENT_MATH },
	{ "phi",	966,	NULL,	"phi",		TEX_ENT_MATH },
	{ "pi",		960,	NULL,	"pi",		TEX_ENT_MATH },
	{ "piv",	982,	NULL,	"varpi",	TEX_ENT_MATH },
	{ "plusmn", 	177,	NULL,	"pm",		TEX_ENT_MATH },
	{ "pound", 	163,	NULL,	"pounds{}",	0 },
	{ "prime",	8242,	NULL,	"^\\prime{}",	TEX_ENT_ASCII },
	{ "prod",	8719,	NULL,	"prod",		TEX_ENT_MATH },
	{ "prop",	8733,	NULL,	"propto",	TEX_ENT_MATH },
	{ "psi",	968,	NULL,	"psi",		TEX_ENT_MATH },
	{ "quot",	34,	NULL,	"\"",		TEX_ENT_ASCII },
	{ "rArr",	8658,	NULL,	"Rightarrow",	TEX_ENT_MATH },
	{ "radic",	8730,	NULL,	"surd",		TEX_ENT_MATH },
	{ "rang",	9002,	NULL,	"rangle",	TEX_ENT_MATH },
	{ "raquo", 	187,	NULL,	"guillemotright{}", 0 },
	{ "rarr",	8594,	NULL,	"rightarrow",	TEX_ENT_MATH },
	{ "rceil",	8969,	NULL,	"rceil",	TEX_ENT_MATH },
	{ "rdquo",	8221,	NULL,	"''",		TEX_ENT_ASCII },
	{ "real",	8476,	NULL,	"Re",		TEX_ENT_MATH },
	{ "reg", 	174,	NULL,	"textregistered{}", 0 },
	{ "rfloor",	8971,	NULL,	"rfloor",	TEX_ENT_MATH },
	{ "rho",	961,	NULL,	"rho",		TEX_ENT_MATH },
	{ "rlm",	8207,	NULL,	NULL,		0 }, /* XXX */
	{ "rsaquo",	8250,	NULL,	NULL,		0 }, /* XXX */
	{ "rsquo",	8217,	NULL,	"'",		TEX_ENT_ASCII },
	{ "sbquo",	8218,	NULL,	NULL,		0 }, /* XXX */
	{ "scaron",	353,	NULL,	"v{s}",		0 },
	{ "sdot",	8901,	NULL,	"cdot",		TEX_ENT_MATH },
	{ "sect", 	167,	NULL,	"S{}",		0 },
	{ "shy", 	173,	NULL,	"-{}",		0 },
	{ "sigma",	963,	NULL,	"sigma",	TEX_ENT_MATH },
	{ "sigmaf",	962,	NULL,	"sigmav",	TEX_ENT_MATH }, /* XXX?? */
	{ "sim",	8764,	NULL,	"sim",		TEX_ENT_MATH },
	{ "sub",	8834,	NULL,	"subset",	TEX_ENT_MATH },
	{ "sube",	8838,	NULL,	"subseteq",	TEX_ENT_MATH },
	{ "sum",	8721,	NULL,	"sum",		TEX_ENT_MATH },
	{ "sup",	8835,	NULL,	"supset",	TEX_ENT_MATH },
	{ "sup1", 	185,	NULL,	"$^1$",		TEX_ENT_ASCII },
	{ "sup2", 	178,	NULL,	"$^2$",		TEX_ENT_ASCII },
	{ "sup3", 	179,	NULL,	"$^3$",		TEX_ENT_ASCII },
	{ "supe",	8839,	NULL,	"supseteq",	TEX_ENT_MATH },
	{ "szlig", 	223,	NULL,	"ss{}",		0 },
	{ "tau",	964,	NULL,	"tau",		TEX_ENT_MATH },
	{ "there4",	8756,	NULL,	"therefore",	TEX_ENT_MATH },
	{ "theta",	952,	NULL,	"theta",	TEX_ENT_MATH },
	{ "thetasym",	977,	NULL,	"vartheta",	TEX_ENT_MATH }, /* XXX?? */
	{ "thinsp",	8201,	NULL,	"hspace{0.167em}", 0 },
	{ "thorn", 	254,	NULL,	"th{}",		0 },
	{ "tilde",	732,	NULL,	"~{}",		0 },
	{ "times", 	215,	NULL,	"times",	TEX_ENT_MATH },
	{ "trade",	8482,	NULL,	"texttrademark{}", 0 },
	{ "uArr",	8657,	NULL,	"Uparrow",	TEX_ENT_MATH },
	{ "uacute", 	250,	NULL,	"'{u}",		0 },
	{ "uarr",	8593,	NULL,	"uparrow",	TEX_ENT_MATH },
	{ "ucirc", 	251,	NULL,	"^{u}",		0 },
	{ "ugrave", 	249,	NULL,	"`{u}",		0 },
	{ "uml", 	168,	NULL,	"\"{}",		0 },
	{ "upsih",	978,	NULL,	NULL,		0 }, /* XXX */
	{ "upsilon",	965,	NULL,	"upsilon",	TEX_ENT_MATH },
	{ "uuml", 	252,	NULL,	"\"{u}",	0 },
	{ "weierp",	8472,	NULL,	"wp",		TEX_ENT_MATH },
	{ "xi",		958,	NULL,	"xi",		TEX_ENT_MATH },
	{ "yacute", 	253,	NULL,	"'{y}",		0 },
	{ "yen", 	165,	NULL,	"textyen{}",	0 },
	{ "yuml", 	255,	NULL,	"\"{y}",	0 },
	{ "zeta",	950,	NULL,	"zeta",		TEX_ENT_MATH },
	{ "zwj",	8205,	NULL,	NULL,		0 }, /* XXX */
	{ "zwnj",	8204,	NULL,	NULL,		0 }, /* XXX */
	{ NULL, 	0,	NULL,	NULL,		0 }
};

static int32_t
entity_find_num(const struct lowdown_buf *buf)
{
	char			 b[32];
	char			*ep;
	unsigned long long	 ulval;
	int			 base;

	if (buf->size < 4)
		return -1;

	/* Copy a hex or decimal value. */

	if (buf->data[2] == 'x' || buf->data[2] == 'X') {
		if (buf->size < 5)
			return -1;
		if (buf->size - 4 > sizeof(b) - 1)
			return -1;
		memcpy(b, buf->data + 3, buf->size - 4);
		b[buf->size - 4] = '\0';
		base = 16;
	} else {
		if (buf->size - 3 > sizeof(b) - 1)
			return -1;
		memcpy(b, buf->data + 2, buf->size - 3);
		b[buf->size - 3] = '\0';
		base = 10;
	}

	/* 
	 * Convert within the given base.
	 * This calling syntax is from OpenBSD's strtoull(3).
	 */

	errno = 0;
	ulval = strtoull(b, &ep, base);
	if (b[0] == '\0' || *ep != '\0')
		return -1;
	if (errno == ERANGE && ulval == ULLONG_MAX)
		return -1;
	if (ulval > INT32_MAX)
		return -1;

	return (int32_t)ulval;
}

/*
 * Convert a named entity to a unicode codepoint.
 * Return -1 on failure.
 */
static const struct ent *
entity_find_named(const struct lowdown_buf *buf)
{
	char	 b[32];
	size_t	 i;

	/* 
	 * Copy into NUL-terminated buffer for easy strcmp().
	 * We omit the leading '&' and trailing ';'.
	 */

	if (buf->size - 2 > sizeof(b) - 1)
		return NULL;
	memcpy(b, buf->data + 1, buf->size - 2);
	b[buf->size - 2] = '\0';

	/* TODO: can be trivially sped up by using a binary search. */

	for (i = 0; ents[i].iso != NULL; i++)
		if (strcmp(b, ents[i].iso) == 0)
			return &ents[i];

	return NULL;
}

/*
 * Basic sanity of HTML entity.
 * Needs to be &xyz;
 * Return zero on failure, non-zero on success.
 */
static int
entity_sane(const struct lowdown_buf *buf)
{

	if (buf->size < 3 ||
	    buf->data[0] != '&' ||
	    buf->data[buf->size - 1] != ';')
		return 0;
	return 1;
}

/*
 * Look up an entity and return its decimal value or -1 on failure (bad
 * formatting or couldn't find entity).
 * Handles both numeric (decimal and hex) and common named ones.
 */
int32_t
entity_find_iso(const struct lowdown_buf *buf)
{
	const struct ent *e;

	if (!entity_sane(buf))
		return -1;

	if (buf->data[1] == '#')
		return entity_find_num(buf);

	if ((e = entity_find_named(buf)) == NULL)
		return -1;

	assert(e->unicode < INT32_MAX);
	return e->unicode;
}

#if 0
const char *
entity_find_roff(const struct lowdown_buf *buf, int *iso)
{
	const struct ent	*e;
	int32_t			 unicode;
	size_t			 i;

	if (!entity_sane(buf))
		return NULL;

	if (buf->data[1] == '#') {
		if ((unicode = entity_find_num(buf)) == -1)
			return NULL;
		for (i = 0; ents[i].iso != NULL; i++)
			if ((int32_t)ents[i].unicode == unicode) {
				*fl = ents[i].texflags;
				return ents[i].tex;
			}
		return NULL;
	}

	if ((e = entity_find_named(buf)) == NULL)
		return NULL;

	assert(e->unicode < INT32_MAX);
	*fl = e->texflags;
	return e->tex;
}
#endif

/*
 * Looks for the TeX entity corresponding to "buf".
 * If "buf" is a numerical code, looks it up by number; if an HTML (ISO)
 * code, looks it up by that.
 * Returns the entity or NULL on failure.
 * On success, sets the TeX flags.
 */
const char *
entity_find_tex(const struct lowdown_buf *buf, unsigned char *fl)
{
	const struct ent	*e;
	int32_t			 unicode;
	size_t			 i;

	if (!entity_sane(buf))
		return NULL;

	if (buf->data[1] == '#') {
		if ((unicode = entity_find_num(buf)) == -1)
			return NULL;
		for (i = 0; ents[i].iso != NULL; i++)
			if ((int32_t)ents[i].unicode == unicode) {
				*fl = ents[i].texflags;
				return ents[i].tex;
			}
		return NULL;
	}

	if ((e = entity_find_named(buf)) == NULL)
		return NULL;

	assert(e->unicode < INT32_MAX);
	*fl = e->texflags;
	return e->tex;
}
