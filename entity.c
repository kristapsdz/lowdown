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
	const char	*tex;
	unsigned char	 texflags;
};

static const struct ent ents[] = {
	{ "AElig", 	198,	"AE{}",		0 },
	{ "Aacute", 	193,	"'{A}",		0 },
	{ "Acirc", 	194,	"^{A}",		0 },
	{ "Agrave", 	192,	"`{A}",		0 },
	{ "Alpha",	913,	"A",		TEX_ENT_ASCII },
	{ "Aring", 	197,	"AA{}",		0 },
	{ "Atilde", 	195,	"~{A}",		0 },
	{ "Auml", 	196,	"\"{A}",	0 },
	{ "Beta",	914,	"B",		TEX_ENT_ASCII },
	{ "Ccedil", 	199,	"c{C}",		0 },
	{ "Chi",	935,	"X",		TEX_ENT_ASCII },
	{ "Dagger",	8225,	"ddag{}",	0 },
	{ "Delta",	916,	"Delta",	TEX_ENT_MATH },
	{ "ETH", 	208,	"DH{}",		0 },
	{ "Eacute", 	201,	"'{E}",		0 },
	{ "Ecirc", 	202,	"^{E}",		0 },
	{ "Egrave", 	200,	"`{E}",		0 },
	{ "Epsilon",	917,	"E",		TEX_ENT_ASCII },
	{ "Eta",	919,	"E",		TEX_ENT_ASCII },
	{ "Euml", 	203,	"\"{E}",	0 },
	{ "Gamma",	915,	"Gamma",	TEX_ENT_MATH },
	{ "Iacute", 	205,	"'{I}",		0 },
	{ "Icirc", 	206,	"^{I}",		0 },
	{ "Igrave", 	204,	"`{I}",		0 },
	{ "Iota",	921,	"I",		TEX_ENT_ASCII },
	{ "Iuml", 	207,	"\"{I}",	0 },
	{ "Kappa",	922,	"K",		TEX_ENT_ASCII },
	{ "Lambda",	923,	"Lambda",	TEX_ENT_MATH },
	{ "Mu",		924,	"M",		TEX_ENT_ASCII },
	{ "Ntilde", 	209,	"~{N}",		0 },
	{ "Nu",		925,	"N",		TEX_ENT_ASCII },
	{ "OElig",	338,	"OE{}",		0 },
	{ "Oacute", 	211,	"'{O}",		0 },
	{ "Ocirc", 	212,	"^{O}",		0 },
	{ "Ograve", 	210,	"`{O}",		0 },
	{ "Omega",	937,	"Omega",	TEX_ENT_MATH },
	{ "Omicron",	927,	"O",		TEX_ENT_ASCII },
	{ "Oslash", 	216,	"O{}",		0 },
	{ "Otilde", 	213,	"~{O}",		0 },
	{ "Ouml", 	214,	"\"{O}",	0 },
	{ "Phi",	934,	"Phi",		TEX_ENT_MATH },
	{ "Pi",		928,	"Pi",		TEX_ENT_MATH },
	{ "Prime",	8243,	"{``}",		TEX_ENT_ASCII },
	{ "Psi",	936,	"Psi",		TEX_ENT_MATH },
	{ "Rho",	929,	"R",		TEX_ENT_ASCII },
	{ "Scaron",	352,	"v{S}",		0 },
	{ "Sigma",	931,	"Sigma",	TEX_ENT_MATH },
	{ "THORN", 	222,	"TH{}",		0 },
	{ "Tau",	932,	"T",		TEX_ENT_ASCII },
	{ "Theta",	920,	"Theta",	TEX_ENT_MATH },
	{ "Uacute", 	218,	"'{U}",		0 },
	{ "Ucirc", 	219,	"^{U}",		0 },
	{ "Ugrave", 	217,	"`{U}",		0 },
	{ "Upsilon",	933,	"Upsilon",	TEX_ENT_MATH },
	{ "Uuml", 	220,	"\"{U}",	0 },
	{ "Xi",		926,	"Xi",		TEX_ENT_MATH },
	{ "Yacute", 	221,	"'{Y}",		0 },
	{ "Yuml",	376,	"\"{Y}",	0 },
	{ "Zeta",	918,	"Z",		TEX_ENT_ASCII },
	{ "aacute", 	225,	"'{a}",		0 },
	{ "acirc", 	226,	"^{a}",		0 },
	{ "acute", 	180,	"'{}",		0 },
	{ "aelig", 	230,	"ae{}",		0 },
	{ "agrave", 	224,	"`{a}",		0 },
	{ "alefsym",	8501,	"aleph",	TEX_ENT_MATH },
	{ "alpha",	945,	"alpha",	TEX_ENT_MATH },
	{ "amp",	38,	"&{}",		0 },
	{ "and",	8743,	"wedge",	TEX_ENT_MATH },
	{ "ang",	8736,	"angle",	TEX_ENT_MATH },
	{ "aring", 	229,	"aa{}",		0 },
	{ "asymp",	8776,	"asymp",	TEX_ENT_MATH },
	{ "atilde", 	227,	"~{a}",		0 },
	{ "auml", 	228,	"\"{a}",	0 },
	{ "bdquo",	8222,	NULL,		0 }, /* XXX */
	{ "beta",	946,	"beta",		TEX_ENT_MATH },
	{ "brvbar", 	166,	"textbrokenbar{}",	0 },
	{ "bull",	8226,	"textbullet{}",	0 },
	{ "cap",	8745,	"cap",		TEX_ENT_MATH },
	{ "ccedil", 	231,	"c{c}",		0 },
	{ "cedil", 	184,	"c{}",		0 },
	{ "cent", 	162,	"textcent{}",	0 },
	{ "chi",	967,	"chi",		TEX_ENT_MATH },
	{ "circ",	710,	"^{}",		0 },
	{ "cong",	8773,	"cong",		TEX_ENT_MATH },
	{ "copy", 	169,	"copyright{}",	0 },
	{ "crarr",	8629,	NULL,		0 }, /* XXX */
	{ "cup",	8746,	"cup",		TEX_ENT_MATH },
	{ "curren", 	164,	"textcurrency{}", 0 },
	{ "dArr",	8659,	"Downarrow",	TEX_ENT_MATH },
	{ "dagger",	8224,	"dag{}",	0 },
	{ "darr",	8595,	"downarrow",	TEX_ENT_MATH },
	{ "deg", 	176,	"textdegree{}",	0 },
	{ "delta",	948,	"delta",	TEX_ENT_MATH },
	{ "divide", 	247,	"div",		TEX_ENT_MATH },
	{ "eacute", 	233,	"'{e}",		0 },
	{ "ecirc", 	234,	"^{e}",		0 },
	{ "egrave", 	232,	"`{e}",		0 },
	{ "empty",	8709,	"emptyset",	TEX_ENT_MATH },
	{ "emsp",	8195,	"hspace{1em}",	0 },
	{ "ensp",	8194,	"hspace{0.5em}", 0 },
	{ "epsilon",	949,	"epsilon",	TEX_ENT_MATH },
	{ "equiv",	8801,	"equiv",	TEX_ENT_MATH },
	{ "eta",	951,	"eta",		TEX_ENT_MATH },
	{ "eth", 	240,	"dh{}",		0 },
	{ "euml", 	235,	"\"{e}",	0 },
	{ "euro",	8364,	"texteuro{}",	0 },
	{ "exist",	8707,	"exists",	TEX_ENT_MATH },
	{ "fnof",	402,	"f",		TEX_ENT_MATH },
	{ "forall",	8704,	"forall",	TEX_ENT_MATH },
	{ "frac12", 	189,	"sfrac{1}{2}",	TEX_ENT_MATH },
	{ "frac14", 	188,	"sfrac{1}{4}",	TEX_ENT_MATH },
	{ "frac34", 	190,	"sfrac{3}{4}",	TEX_ENT_MATH },
	{ "frasl",	8260,	NULL,		0 }, /* XXX */
	{ "gamma",	947,	"gamma",	TEX_ENT_MATH },
	{ "ge",		8805,	"geq",		TEX_ENT_MATH },
	{ "gt",		62,	"textgreater{}", 0 },
	{ "hArr",	8660,	"Leftrightarrow", TEX_ENT_MATH },
	{ "harr",	8596,	"leftrightarrow", TEX_ENT_MATH },
	{ "hellip",	8230,	"ldots{}",	0 },
	{ "iacute", 	237,	"'{i}",		0 },
	{ "icirc", 	238,	"^{i}",		0 },
	{ "iexcl", 	161,	"textexclamdown{}", 0 },
	{ "igrave", 	236,	"`{i}",		0 },
	{ "image",	8465,	"Im",		TEX_ENT_MATH },
	{ "infin",	8734,	"infty",	TEX_ENT_MATH },
	{ "int",	8747,	"int",		TEX_ENT_MATH },
	{ "iota",	953,	"iota",		TEX_ENT_MATH },
	{ "iquest", 	191,	"textquestiondown{}", 0 },
	{ "isin",	8712,	"in",		TEX_ENT_MATH },
	{ "iuml", 	239,	"\"{i}",	0 },
	{ "kappa",	954,	"kappa",	TEX_ENT_MATH },
	{ "lArr",	8656,	"Leftarrow",	TEX_ENT_MATH },
	{ "lambda",	955,	"lambda",	TEX_ENT_MATH },
	{ "lang",	9001,	"langle",	TEX_ENT_MATH },
	{ "laquo", 	171,	"guillemetleft{}", 0 },
	{ "larr",	8592,	"leftarrow",	TEX_ENT_MATH },
	{ "lceil",	8968,	"lceil",	TEX_ENT_MATH },
	{ "ldquo",	8220,	"``",		TEX_ENT_ASCII },
	{ "le",		8804,	"leq",		TEX_ENT_MATH },
	{ "lfloor",	8970,	"lfloor",	TEX_ENT_MATH },
	{ "lowast",	8727,	"_\\ast",	TEX_ENT_MATH },
	{ "lrm",	8206,	NULL,		0 }, /* XXX */
	{ "lsaquo",	8249,	NULL,		0 },
	{ "lsquo",	8216,	"`",		TEX_ENT_ASCII },
	{ "lt",		60,	"textless{}",	0 },
	{ "macr", 	175,	"={}",		0 },
	{ "mdash",	8212,	"---",		TEX_ENT_ASCII },
	{ "micro", 	181,	"textmu{}",	0 },
	{ "middot", 	183,	"textperiodcentered{}", 0 },
	{ "minus",	8722,	"-{}",		0 },
	{ "mu",		956,	"mu",		TEX_ENT_MATH },
	{ "nabla",	8711,	"nabla",	TEX_ENT_MATH },
	{ "nbsp", 	160,	"~",		TEX_ENT_ASCII },
	{ "ndash",	8211,	"--",		TEX_ENT_ASCII },
	{ "ne",		8800,	"not=",		TEX_ENT_MATH },
	{ "ni",		8715,	"ni",		TEX_ENT_MATH },
	{ "not", 	172,	"lnot",		TEX_ENT_MATH },
	{ "notin",	8713,	"not\\in",	TEX_ENT_MATH },
	{ "nsub",	8836,	"not\\subset",	TEX_ENT_MATH },
	{ "ntilde", 	241,	"~{n}",		0 },
	{ "nu",		957,	"nu",		TEX_ENT_MATH },
	{ "oacute", 	243,	"'{o}",		0 },
	{ "ocirc", 	244,	"^{o}",		0 },
	{ "oelig",	339,	"oe{}",		0 },
	{ "ograve", 	242,	"`{o}",		0 },
	{ "oline",	8254,	"ominus",	TEX_ENT_MATH },
	{ "omega",	969,	"omega",	TEX_ENT_MATH },
	{ "omicron",	959,	"omicron",	TEX_ENT_MATH },
	{ "oplus",	8853,	"oplus",	TEX_ENT_MATH },
	{ "or",		8744,	"vee",		TEX_ENT_MATH },
	{ "ordf", 	170,	"textordfeminine{}", 0 },
	{ "ordm", 	186,	"textordmasculine{}", 0 },
	{ "oslash", 	248,	"oslash",	TEX_ENT_MATH },
	{ "otilde", 	245,	"~{o}",		0 },
	{ "otimes",	8855,	"otimes",	TEX_ENT_MATH },
	{ "ouml", 	246,	"\"{o}",	0 },
	{ "para", 	182,	"P{}",		0 },
	{ "part",	8706,	"partial",	TEX_ENT_MATH },
	{ "permil",	8240,	"textperthousand{}", 0 },
	{ "perp",	8869,	"perp",		TEX_ENT_MATH },
	{ "phi",	966,	"phi",		TEX_ENT_MATH },
	{ "pi",		960,	"pi",		TEX_ENT_MATH },
	{ "piv",	982,	"varpi",	TEX_ENT_MATH },
	{ "plusmn", 	177,	"pm",		TEX_ENT_MATH },
	{ "pound", 	163,	"pounds{}",	0 },
	{ "prime",	8242,	"^\\prime{}",	TEX_ENT_ASCII },
	{ "prod",	8719,	"prod",		TEX_ENT_MATH },
	{ "prop",	8733,	"propto",	TEX_ENT_MATH },
	{ "psi",	968,	"psi",		TEX_ENT_MATH },
	{ "quot",	34,	"\"",		TEX_ENT_ASCII },
	{ "rArr",	8658,	"Rightarrow",	TEX_ENT_MATH },
	{ "radic",	8730,	"surd",		TEX_ENT_MATH },
	{ "rang",	9002,	"rangle",	TEX_ENT_MATH },
	{ "raquo", 	187,	"guillemotright{}", 0 },
	{ "rarr",	8594,	"rightarrow",	TEX_ENT_MATH },
	{ "rceil",	8969,	"rceil",	TEX_ENT_MATH },
	{ "rdquo",	8221,	"''",		TEX_ENT_ASCII },
	{ "real",	8476,	"Re",		TEX_ENT_MATH },
	{ "reg", 	174,	"textregistered{}", 0 },
	{ "rfloor",	8971,	"rfloor",	TEX_ENT_MATH },
	{ "rho",	961,	"rho",		TEX_ENT_MATH },
	{ "rlm",	8207,	NULL,		0 }, /* XXX */
	{ "rsaquo",	8250,	NULL,		0 }, /* XXX */
	{ "rsquo",	8217,	"'",		TEX_ENT_ASCII },
	{ "sbquo",	8218,	NULL,		0 }, /* XXX */
	{ "scaron",	353,	"v{s}",		0 },
	{ "sdot",	8901,	"cdot",		TEX_ENT_MATH },
	{ "sect", 	167,	"S{}",		0 },
	{ "shy", 	173,	"-{}",		0 },
	{ "sigma",	963,	"sigma",	TEX_ENT_MATH },
	{ "sigmaf",	962,	"sigmav",	TEX_ENT_MATH }, /* XXX?? */
	{ "sim",	8764,	"sim",		TEX_ENT_MATH },
	{ "sub",	8834,	"subset",	TEX_ENT_MATH },
	{ "sube",	8838,	"subseteq",	TEX_ENT_MATH },
	{ "sum",	8721,	"sum",		TEX_ENT_MATH },
	{ "sup",	8835,	"supset",	TEX_ENT_MATH },
	{ "sup1", 	185,	"$^1$",		TEX_ENT_ASCII },
	{ "sup2", 	178,	"$^2$",		TEX_ENT_ASCII },
	{ "sup3", 	179,	"$^3$",		TEX_ENT_ASCII },
	{ "supe",	8839,	"supseteq",	TEX_ENT_MATH },
	{ "szlig", 	223,	"ss{}",		0 },
	{ "tau",	964,	"tau",		TEX_ENT_MATH },
	{ "there4",	8756,	"therefore",	TEX_ENT_MATH },
	{ "theta",	952,	"theta",	TEX_ENT_MATH },
	{ "thetasym",	977,	"vartheta",	TEX_ENT_MATH }, /* XXX?? */
	{ "thinsp",	8201,	"hspace{0.167em}", 0 },
	{ "thorn", 	254,	"th{}",		0 },
	{ "tilde",	732,	"~{}",		0 },
	{ "times", 	215,	"times",	TEX_ENT_MATH },
	{ "trade",	8482,	"texttrademark{}", 0 },
	{ "uArr",	8657,	"Uparrow",	TEX_ENT_MATH },
	{ "uacute", 	250,	"'{u}",		0 },
	{ "uarr",	8593,	"uparrow",	TEX_ENT_MATH },
	{ "ucirc", 	251,	"^{u}",		0 },
	{ "ugrave", 	249,	"`{u}",		0 },
	{ "uml", 	168,	"\"{}",		0 },
	{ "upsih",	978,	NULL,		0 }, /* XXX */
	{ "upsilon",	965,	"upsilon",	TEX_ENT_MATH },
	{ "uuml", 	252,	"\"{u}",	0 },
	{ "weierp",	8472,	"wp",		TEX_ENT_MATH },
	{ "xi",		958,	"xi",		TEX_ENT_MATH },
	{ "yacute", 	253,	"'{y}",		0 },
	{ "yen", 	165,	"textyen{}",	0 },
	{ "yuml", 	255,	"\"{y}",	0 },
	{ "zeta",	950,	"zeta",		TEX_ENT_MATH },
	{ "zwj",	8205,	NULL,		0 }, /* XXX */
	{ "zwnj",	8204,	NULL,		0 }, /* XXX */
	{ NULL, 	0,	NULL,		0 }
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
