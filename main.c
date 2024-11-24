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
#include <sys/param.h>
#if HAVE_CAPSICUM
# include <sys/resource.h>
# include <sys/capsicum.h>
#endif
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h> /* INT_MAX */
#include <locale.h> /* set_locale() */
#if HAVE_SANDBOX_INIT
# include <sandbox.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if HAVE_TERMIOS
# include <termios.h> /* struct winsize */
#endif
#include <unistd.h>

#include "lowdown.h"

/*
 * Start with all of the sandboxes.
 * The sandbox_pre() happens before we open our input file for reading,
 * while the sandbox_post() happens afterward.
 */

#if HAVE_PLEDGE

static void
sandbox_post(int fdin, int fddin, int fdout)
{

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");
}

static void
sandbox_pre(void)
{

	if (pledge("stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");
}

#elif HAVE_SANDBOX_INIT

static void
sandbox_post(int fdin, int fddin, int fdout)
{
	char	*ep;
	int	 rc;

	rc = sandbox_init
		(kSBXProfilePureComputation,
		 SANDBOX_NAMED, &ep);
	if (rc != 0)
		errx(1, "sandbox_init: %s", ep);
}

static void
sandbox_pre(void)
{

	/* Do nothing. */
}

#elif HAVE_CAPSICUM

static void
sandbox_post(int fdin, int fddin, int fdout)
{
	cap_rights_t	 rights;

	cap_rights_init(&rights);

	cap_rights_init(&rights, CAP_EVENT, CAP_READ, CAP_FSTAT);
	if (cap_rights_limit(fdin, &rights) < 0)
 		err(1, "cap_rights_limit");

	if (fddin != -1) {
		cap_rights_init(&rights, 
			CAP_EVENT, CAP_READ, CAP_FSTAT);
		if (cap_rights_limit(fddin, &rights) < 0)
			err(1, "cap_rights_limit");
	}

	cap_rights_init(&rights, CAP_EVENT, CAP_WRITE, CAP_FSTAT);
	if (cap_rights_limit(STDERR_FILENO, &rights) < 0)
 		err(1, "cap_rights_limit");

	cap_rights_init(&rights, CAP_EVENT, CAP_WRITE, CAP_FSTAT);
	if (cap_rights_limit(fdout, &rights) < 0)
 		err(1, "cap_rights_limit");

	if (cap_enter())
		err(1, "cap_enter");
}

static void
sandbox_pre(void)
{

	/* Do nothing. */
}

#else /* No compile-time sandbox. */

/*
 * WebAssembly (WASI) is a virtual architecture, so it's "sandboxed" by
 * default.  Since building WASI targets is now fairly common, we can
 * specifically address this by not issuing the warning.  XXX: this
 * special-cases this output, which is not something that I like, but
 * until there are other platforms that are similarly
 * sandboxed-by-default, will keep as-is.
 */
#ifndef __wasi__
# warning Compiling without sandbox support.
#endif

static void
sandbox_post(int fdin, int fddin, int fdout)
{

	/* Do nothing. */
}

static void
sandbox_pre(void)
{

	/* Do nothing. */
}

#endif

static size_t
get_columns(void)
{
	size_t res = 72;
#if HAVE_TERMIOS
	struct winsize	 size;

	memset(&size, 0, sizeof(struct winsize));
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != -1)
		res = size.ws_col;
#endif
	return res;
}

/*
 * Recognise the metadata format of "foo = bar" and "foo: bar".
 * Translates from the former into the latter.
 * This way "foo = : bar" -> "foo : : bar", etc.
 * Errors out if the metadata is malformed (no colon or equal sign).
 */
static void
metadata_parse(char opt, char ***vals, size_t *valsz, const char *arg)
{
	const char	*loceq, *loccol;
	char		*cp;

	loceq = strchr(arg, '=');
	loccol = strchr(arg, ':');

	if ((loceq != NULL && loccol == NULL) ||
	    (loceq != NULL && loccol != NULL && loceq < loccol)) {
		if (asprintf(&cp, "%.*s: %s\n",
		    (int)(loceq - arg), arg, loceq + 1) == -1)
			err(1, NULL);
		*vals = reallocarray(*vals, *valsz + 1, sizeof(char *));
		if (*vals == NULL)
			err(1, NULL);
		(*vals)[*valsz] = cp;
		(*valsz)++;
		return;
	}
	if ((loccol != NULL && loceq == NULL) ||
	    (loccol != NULL && loceq != NULL && loccol < loceq)) {
		if (asprintf(&cp, "%s\n", arg) == -1)
			err(1, NULL);
		*vals = reallocarray(*vals, *valsz + 1, sizeof(char *));
		if (*vals == NULL)
			err(1, NULL);
		(*vals)[*valsz] = cp;
		(*valsz)++;
		return;
	}
	errx(1, "-%c: malformed metadata", opt);
}

/*
 * Read a full file into the NUL-terminated return pointer.  Exits on
 * failure.
 */
static char *
readfile(const char *fn)
{
	int	 	 fd;
	struct stat	 st;
	ssize_t		 ssz;
	size_t		 sz;
	char		*cp, *orig;

	if ((fd = open(fn, O_RDONLY)) == -1)
		err(1, "%s", fn);
	if (fstat(fd, &st) == -1)
		err(1, "%s", fn);
	if ((uint64_t)st.st_size > SIZE_MAX - 1)
		errx(1, "%s: file too long", fn);
	sz = (size_t)st.st_size;
	if ((cp = orig = malloc(sz + 1)) == NULL)
		err(1, NULL);
	while (sz > 0) {
		if ((ssz = read(fd, cp, sz)) == -1)
			err(1, "%s", fn);
		if (ssz == 0)
			errx(1, "%s: short file", fn);
		sz -= (size_t)ssz;
		cp += ssz;
	}
	*cp = '\0';
	close(fd);
	return orig;
}

int
main(int argc, char *argv[])
{
	FILE			*fin = stdin, *fout = stdout, 
				*din = NULL;
	const char		*fnin = "<stdin>", *fnout = NULL,
	      	 		*fndin = NULL, *extract = NULL, *er,
				*mainopts = "LM:m:sT:t:o:X:",
	      			*diffopts = "M:m:sT:t:o:",
				*templfn = NULL, *odtstylefn = NULL;
	struct lowdown_opts 	 opts;
	int			 c, diff = 0, status = 0, afl = 0,
				 rfl = 0, aifl = 0, rifl = 0,
				 centre = 0, list = 0;
	char			*ret = NULL, *cp, *templptr = NULL,
				*nroffcodefn = NULL,
				*odtstyleptr = NULL;
	size_t		 	 i, retsz = 0, rcols;
	struct lowdown_meta 	*m;
	struct lowdown_metaq	 mq;
	struct option 		 lo[] = {
		{ "html-escapehtml",	no_argument,	&afl, LOWDOWN_HTML_ESCAPE },
		{ "html-no-escapehtml",	no_argument,	&rfl, LOWDOWN_HTML_ESCAPE },
		{ "html-hardwrap",	no_argument,	&afl, LOWDOWN_HTML_HARD_WRAP },
		{ "html-no-hardwrap",	no_argument,	&rfl, LOWDOWN_HTML_HARD_WRAP },
		{ "html-head-ids",	no_argument,	&afl, LOWDOWN_HTML_HEAD_IDS },
		{ "html-no-head-ids",	no_argument,	&rfl, LOWDOWN_HTML_HEAD_IDS },
		{ "html-num-ent",	no_argument,	&afl, LOWDOWN_HTML_NUM_ENT },
		{ "html-no-num-ent",	no_argument,	&rfl, LOWDOWN_HTML_NUM_ENT },
		{ "html-owasp",		no_argument,	&afl, LOWDOWN_HTML_OWASP },
		{ "html-no-owasp",	no_argument,	&rfl, LOWDOWN_HTML_OWASP },
		{ "html-skiphtml",	no_argument,	&afl, LOWDOWN_HTML_SKIP_HTML },
		{ "html-no-skiphtml",	no_argument,	&rfl, LOWDOWN_HTML_SKIP_HTML },
		{ "html-titleblock",	no_argument,	&afl, LOWDOWN_HTML_TITLEBLOCK },
		{ "html-no-titleblock",	no_argument,	&rfl, LOWDOWN_HTML_TITLEBLOCK },
		{ "html-callout-mdn",	no_argument,	&afl, LOWDOWN_HTML_CALLOUT_MDN },
		{ "html-no-callout-mdn",no_argument,	&rfl, LOWDOWN_HTML_CALLOUT_MDN },
		{ "html-callout-gfm",	no_argument,	&afl, LOWDOWN_HTML_CALLOUT_GFM },
		{ "html-no-callout-gfm",no_argument,	&rfl, LOWDOWN_HTML_CALLOUT_GFM },

		{ "latex-numbered",	no_argument,	&afl, LOWDOWN_LATEX_NUMBERED },
		{ "latex-no-numbered",	no_argument,	&rfl, LOWDOWN_LATEX_NUMBERED },
		{ "latex-skiphtml",	no_argument,	&afl, LOWDOWN_LATEX_SKIP_HTML },
		{ "latex-no-skiphtml",	no_argument,	&rfl, LOWDOWN_LATEX_SKIP_HTML },

		{ "nroff-traditional",	no_argument,	&rfl, LOWDOWN_NROFF_GROFF },
		{ "nroff-no-traditional",no_argument,	&afl, LOWDOWN_NROFF_GROFF },
		{ "nroff-no-groff",	no_argument,	&rfl, LOWDOWN_NROFF_GROFF },
		{ "nroff-groff",	no_argument,	&afl, LOWDOWN_NROFF_GROFF },
		{ "nroff-nolinks",	no_argument, 	&afl, LOWDOWN_NROFF_NOLINK },
		{ "nroff-no-nolinks",	no_argument, 	&rfl, LOWDOWN_NROFF_NOLINK },
		{ "nroff-numbered",	no_argument,	&afl, LOWDOWN_NROFF_NUMBERED },
		{ "nroff-no-numbered",	no_argument,	&rfl, LOWDOWN_NROFF_NUMBERED },
		{ "nroff-shortlinks",	no_argument, 	&afl, LOWDOWN_NROFF_SHORTLINK },
		{ "nroff-no-shortlinks",no_argument, 	&rfl, LOWDOWN_NROFF_SHORTLINK },
		{ "nroff-skiphtml",	no_argument,	&afl, LOWDOWN_NROFF_SKIP_HTML },
		{ "nroff-no-skiphtml",	no_argument,	&rfl, LOWDOWN_NROFF_SKIP_HTML },
		{ "nroff-endnotes",	no_argument,	&afl, LOWDOWN_NROFF_ENDNOTES },
		{ "nroff-no-endnotes",	no_argument,	&rfl, LOWDOWN_NROFF_ENDNOTES },
		{ "nroff-code-font",	required_argument, NULL, 7 },

		{ "odt-skiphtml",	no_argument,	&afl, LOWDOWN_ODT_SKIP_HTML },
		{ "odt-no-skiphtml",	no_argument,	&rfl, LOWDOWN_ODT_SKIP_HTML },
		{ "odt-style",		required_argument, NULL, 6 },

		{ "term-columns",	required_argument, NULL, 4 },
		{ "term-hmargin",	required_argument, NULL, 2 },
		{ "term-vmargin",	required_argument, NULL, 3 },
		{ "term-width",		required_argument, NULL, 1 },

		{ "gemini-link-end",	no_argument, 	&afl, LOWDOWN_GEMINI_LINK_END },
		{ "gemini-no-link-end",	no_argument, 	&rfl, LOWDOWN_GEMINI_LINK_END },
		{ "gemini-link-roman",	no_argument, 	&afl, LOWDOWN_GEMINI_LINK_ROMAN },
		{ "gemini-no-link-roman", no_argument, 	&rfl, LOWDOWN_GEMINI_LINK_ROMAN },
		{ "gemini-link-noref",	no_argument, 	&afl, LOWDOWN_GEMINI_LINK_NOREF },
		{ "gemini-no-link-noref", no_argument, 	&rfl, LOWDOWN_GEMINI_LINK_NOREF },
		{ "gemini-link-inline",	no_argument, 	&afl, LOWDOWN_GEMINI_LINK_IN },
		{ "gemini-no-link-inline",no_argument, 	&rfl, LOWDOWN_GEMINI_LINK_IN },
		{ "gemini-metadata",	no_argument, 	&afl, LOWDOWN_GEMINI_METADATA },
		{ "gemini-no-metadata",	no_argument, 	&rfl, LOWDOWN_GEMINI_METADATA },

		{ "term-no-ansi",	no_argument, 	&afl, LOWDOWN_TERM_NOANSI },
		{ "term-ansi",		no_argument, 	&rfl, LOWDOWN_TERM_NOANSI },
		{ "term-no-colour",	no_argument, 	&afl, LOWDOWN_TERM_NOCOLOUR },
		{ "term-colour",	no_argument, 	&rfl, LOWDOWN_TERM_NOCOLOUR },
		{ "term-nolinks",	no_argument, 	&afl, LOWDOWN_TERM_NOLINK },
		{ "term-no-nolinks",	no_argument, 	&rfl, LOWDOWN_TERM_NOLINK },
		{ "term-shortlinks",	no_argument, 	&afl, LOWDOWN_TERM_SHORTLINK },
		{ "term-no-shortlinks",	no_argument, 	&rfl, LOWDOWN_TERM_SHORTLINK },
		{ "term-all-meta",	no_argument, 	&afl, LOWDOWN_TERM_ALL_META },
		{ "term-all-metadata",	no_argument, 	&afl, LOWDOWN_TERM_ALL_META },
		{ "term-no-all-meta",	no_argument, 	&rfl, LOWDOWN_TERM_ALL_META },
		{ "term-no-all-metadata", no_argument, 	&rfl, LOWDOWN_TERM_ALL_META },

		{ "out-smarty",		no_argument,	&afl, LOWDOWN_SMARTY },
		{ "out-no-smarty",	no_argument,	&rfl, LOWDOWN_SMARTY },
		{ "out-standalone",	no_argument,	&afl, LOWDOWN_STANDALONE },
		{ "out-no-standalone",	no_argument,	&rfl, LOWDOWN_STANDALONE },

		{ "parse-hilite",	no_argument,	&aifl, LOWDOWN_HILITE },
		{ "parse-no-hilite",	no_argument,	&rifl, LOWDOWN_HILITE },
		{ "parse-tables",	no_argument,	&aifl, LOWDOWN_TABLES },
		{ "parse-no-tables",	no_argument,	&rifl, LOWDOWN_TABLES },
		{ "parse-fenced",	no_argument,	&aifl, LOWDOWN_FENCED },
		{ "parse-no-fenced",	no_argument,	&rifl, LOWDOWN_FENCED },
		{ "parse-footnotes",	no_argument,	&aifl, LOWDOWN_FOOTNOTES },
		{ "parse-no-footnotes",	no_argument,	&rifl, LOWDOWN_FOOTNOTES },
		{ "parse-autolink",	no_argument,	&aifl, LOWDOWN_AUTOLINK },
		{ "parse-no-autolink",	no_argument,	&rifl, LOWDOWN_AUTOLINK },
		{ "parse-strike",	no_argument,	&aifl, LOWDOWN_STRIKE },
		{ "parse-no-strike",	no_argument,	&rifl, LOWDOWN_STRIKE },
		{ "parse-super",	no_argument,	&aifl, LOWDOWN_SUPER },
		{ "parse-no-super",	no_argument,	&rifl, LOWDOWN_SUPER },
		{ "parse-super-short",	no_argument,	&aifl, LOWDOWN_SUPER_SHORT },
		{ "parse-no-super-short",no_argument,	&rifl, LOWDOWN_SUPER_SHORT },
		{ "parse-math",		no_argument,	&aifl, LOWDOWN_MATH },
		{ "parse-no-math",	no_argument,	&rifl, LOWDOWN_MATH },
		{ "parse-mantitle",	no_argument,	&aifl, LOWDOWN_MANTITLE },
		{ "parse-no-mantitle",	no_argument,	&rifl, LOWDOWN_MANTITLE },
		{ "parse-codeindent",	no_argument,	&rifl, LOWDOWN_NOCODEIND },
		{ "parse-no-codeindent",no_argument,	&aifl, LOWDOWN_NOCODEIND },
		{ "parse-intraemph",	no_argument,	&rifl, LOWDOWN_NOINTEM },
		{ "parse-no-intraemph",	no_argument,	&aifl, LOWDOWN_NOINTEM },
		{ "parse-metadata",	no_argument,	&aifl, LOWDOWN_METADATA },
		{ "parse-no-metadata",	no_argument,	&rifl, LOWDOWN_METADATA },
		{ "parse-cmark",	no_argument,	&aifl, LOWDOWN_COMMONMARK },
		{ "parse-no-cmark",	no_argument,	&rifl, LOWDOWN_COMMONMARK },
		{ "parse-deflists",	no_argument,	&aifl, LOWDOWN_DEFLIST },
		{ "parse-no-deflists",	no_argument,	&rifl, LOWDOWN_DEFLIST },
		/* TODO: remove these... */
		{ "parse-img-ext",	no_argument,	&aifl, LOWDOWN_IMG_EXT },
		{ "parse-no-img-ext",	no_argument,	&rifl, LOWDOWN_IMG_EXT },
		{ "parse-ext-attrs",	no_argument,	&aifl, LOWDOWN_ATTRS },
		{ "parse-no-ext-attrs",	no_argument,	&rifl, LOWDOWN_ATTRS },
		{ "parse-tasklists",	no_argument,	&aifl, LOWDOWN_TASKLIST },
		{ "parse-no-tasklists",	no_argument,	&rifl, LOWDOWN_TASKLIST },
		{ "parse-callouts",	no_argument,	&aifl, LOWDOWN_CALLOUTS },
		{ "parse-no-callouts",	no_argument,	&rifl, LOWDOWN_CALLOUTS },
		{ "parse-maxdepth",	required_argument, NULL, 5 },

		{ "template",		required_argument, NULL, 8 },
		{ NULL,			0,	NULL,	0 }
	};

	/* Get the real number of columns or 72. */

	rcols = get_columns();

	sandbox_pre();

	TAILQ_INIT(&mq);
	memset(&opts, 0, sizeof(struct lowdown_opts));

	opts.maxdepth = 128;
	opts.type = LOWDOWN_HTML;
	opts.feat =
		LOWDOWN_ATTRS |
		LOWDOWN_AUTOLINK |
		LOWDOWN_COMMONMARK |
		LOWDOWN_DEFLIST |
		LOWDOWN_FENCED |
		LOWDOWN_FOOTNOTES |
		LOWDOWN_CALLOUTS |
		LOWDOWN_MANTITLE |
		LOWDOWN_METADATA |
		LOWDOWN_STRIKE |
		LOWDOWN_SUPER |
		LOWDOWN_TABLES |
		LOWDOWN_TASKLIST;
	opts.oflags = 
		LOWDOWN_HTML_ESCAPE |
		LOWDOWN_HTML_HEAD_IDS |
		LOWDOWN_HTML_NUM_ENT |
		LOWDOWN_HTML_OWASP |
		LOWDOWN_HTML_SKIP_HTML |
		LOWDOWN_NROFF_GROFF |
		LOWDOWN_NROFF_NUMBERED |
		LOWDOWN_NROFF_SKIP_HTML |
		LOWDOWN_ODT_SKIP_HTML |
		LOWDOWN_LATEX_SKIP_HTML |
		LOWDOWN_LATEX_NUMBERED |
		LOWDOWN_SMARTY;

	if (strcasecmp(getprogname(), "lowdown-diff") == 0) 
		diff = 1;

	while ((c = getopt_long(argc, argv, 
	       diff ? diffopts : mainopts, lo, NULL)) != -1)
		switch (c) {
		case 'M':
			metadata_parse(c, &opts.metaovr, 
				&opts.metaovrsz, optarg);
			break;
		case 'm':
			metadata_parse(c, &opts.meta, 
				&opts.metasz, optarg);
			break;
		case 'o':
			fnout = optarg;
			break;
		case 's':
			opts.oflags |= LOWDOWN_STANDALONE;
			break;
		case 't':
			/* FALLTHROUGH */
		case 'T':
			if (strcasecmp(optarg, "ms") == 0)
				opts.type = LOWDOWN_NROFF;
			else if (strcasecmp(optarg, "gemini") == 0)
				opts.type = LOWDOWN_GEMINI;
			else if (strcasecmp(optarg, "html") == 0)
				opts.type = LOWDOWN_HTML;
			else if (strcasecmp(optarg, "latex") == 0)
				opts.type = LOWDOWN_LATEX;
			else if (strcasecmp(optarg, "man") == 0)
				opts.type = LOWDOWN_MAN;
			else if (strcasecmp(optarg, "fodt") == 0)
				opts.type = LOWDOWN_FODT;
			else if (strcasecmp(optarg, "term") == 0)
				opts.type = LOWDOWN_TERM;
			else if (strcasecmp(optarg, "tree") == 0)
				opts.type = LOWDOWN_TREE;
			else if (strcasecmp(optarg, "null") == 0)
				opts.type = LOWDOWN_NULL;
			else
				goto usage;
			break;
		case 'X':
			extract = optarg;
			list = 0;
			break;
		case 'L':
			list = 1;
			extract = NULL;
			break;
		case 0:
			if (rfl)
				opts.oflags &= ~rfl;
			if (afl)
				opts.oflags |= afl;
			if (rifl)
				opts.feat &= ~rifl;
			if (aifl)
				opts.feat |= aifl;
			break;
		case 1:
			opts.cols = strtonum(optarg, 0, INT_MAX, &er);
			if (er == NULL)
				break;
			errx(1, "--term-width: %s", er);
		case 2:
			if (strcmp(optarg, "centre") == 0 ||
			    strcmp(optarg, "centre") == 0) {
				centre = 1;
				break;
			}
			opts.hmargin = strtonum
				(optarg, 0, INT_MAX, &er);
			if (er == NULL)
				break;
			errx(1, "--term-hmargin: %s", er);
		case 3:
			opts.vmargin = strtonum(optarg, 0, INT_MAX, &er);
			if (er == NULL)
				break;
			errx(1, "--term-vmargin: %s", er);
		case 4:
			rcols = strtonum(optarg, 1, INT_MAX, &er);
			if (er == NULL)
				break;
			errx(1, "--term-columns: %s", er);
		case 5:
			opts.maxdepth = strtonum(optarg, 0, INT_MAX, &er);
			if (er == NULL)
				break;
			errx(1, "--parse-maxdepth: %s", er);
		case 6:
			odtstylefn = optarg;
			break;
		case 7:
			/*
			 * Break down some aliases here: "none", "bold",
			 * or "code".
			 */
			if (strcmp(optarg, "none") == 0)
				nroffcodefn = strdup("R,B,I,BI");
			else if (strcmp(optarg, "bold") == 0)
				nroffcodefn = strdup("B,B,BI,BI");
			else if (strcmp(optarg, "code") == 0)
				nroffcodefn = strdup("CR,CB,CI,CBI");
			else
				nroffcodefn = strdup(optarg);
			if (nroffcodefn == NULL)
				err(1, NULL);
			break;
		case 8:
			templfn = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (opts.type == LOWDOWN_TERM ||
 	    opts.type == LOWDOWN_GEMINI)
		setlocale(LC_CTYPE, "");

	/* 
	 * By default, try to show 80 columns.
	 * Don't show more than the number of available columns.
	 */

	if (opts.cols == 0) {
		if ((opts.cols = rcols) > 80)
			opts.cols = 80;
	} else if (opts.cols > rcols)
		opts.cols = rcols;

	/* If we're centred, set our margins. */

	if (centre && opts.cols < rcols)
		opts.hmargin = (rcols - opts.cols) / 2;

	/* 
	 * Diff mode takes two arguments: the first is mandatory (the
	 * old file) and the second (the new one) is optional.
	 * Non-diff mode takes an optional single argument.
	 */

	if ((diff && (argc == 0 || argc > 2)) || (!diff && argc > 1))
		goto usage;

	if (diff) {
		if (argc > 1 && strcmp(argv[1], "-")) {
			fnin = argv[1];
			if ((fin = fopen(fnin, "r")) == NULL)
				err(1, "%s", fnin);
		}
		fndin = argv[0];
		if ((din = fopen(fndin, "r")) == NULL)
			err(1, "%s", fndin);
	} else {
		if (argc && strcmp(argv[0], "-")) {
			fnin = argv[0];
			if ((fin = fopen(fnin, "r")) == NULL)
				err(1, "%s", fnin);
		}
	}

	/*
	 * If we have a template specified, load it now before we drop
	 * privileges.
	 */

	if (templfn != NULL)
		opts.templ = templptr = readfile(templfn);

	/*
	 * DEPRECATED: use --template instead.
	 */

	if (opts.type == LOWDOWN_FODT && odtstylefn != NULL)
		opts.odt.sty = odtstyleptr = readfile(odtstylefn);

	/*
	 * If specified and in -tman or -tms, parse the constant width
	 * fonts.  As mentioned in nroff.c, the code font "C" is not
	 * portable, so let the user override it.  This comes as a
	 * comma-separated sequence of R[,B[,I[,BI]]].  Any of these may
	 * inherit the default by being unspecified or empty, e.g.,
	 * "CR,CB".
	 */

	if ((opts.type == LOWDOWN_MAN || opts.type == LOWDOWN_NROFF) &&
	    nroffcodefn != NULL && *nroffcodefn != '\0') {
		opts.nroff.cr = cp = nroffcodefn;
		while (*cp != '\0' && *cp != ',')
			cp++;
		if (*cp != '\0') {
			*cp++ = '\0';
			opts.nroff.cb = cp;
			while (*cp != '\0' && *cp != ',')
				cp++;
			if (*cp != '\0') {
				*cp++ = '\0';
				opts.nroff.ci = cp;
				while (*cp != '\0' && *cp != ',')
					cp++;
				if (*cp != '\0') {
					*cp++ = '\0';
					opts.nroff.cbi = cp;
					while (*cp != '\0' && *cp != ',')
						cp++;
				}
			}
		}
		if (opts.nroff.cr != NULL && *opts.nroff.cr == '\0')
			opts.nroff.cr = NULL;
		if (opts.nroff.cb != NULL && *opts.nroff.cb == '\0')
			opts.nroff.cb = NULL;
		if (opts.nroff.ci != NULL && *opts.nroff.ci == '\0')
			opts.nroff.ci = NULL;
		if (opts.nroff.cbi != NULL && *opts.nroff.cbi == '\0')
			opts.nroff.cbi = NULL;
	}

	/* Configure the output file. */

	if (fnout != NULL && strcmp(fnout, "-") &&
	    (fout = fopen(fnout, "w")) == NULL)
		err(1, "%s", fnout);

	sandbox_post(fileno(fin), din == NULL ? 
		-1 : fileno(din), fileno(fout));

	/* We're now completely sandboxed. */

	/* Require metadata when extracting. */

	if (extract || list)
		opts.feat |= LOWDOWN_METADATA;

	/* 
	 * Allow NO_COLOUR to dictate colours.
	 * This only works for -Tterm output when not in diff mode.
	 */

	if (getenv("NO_COLOR") != NULL ||
	    getenv("NO_COLOUR") != NULL)
		opts.oflags |= LOWDOWN_TERM_NOCOLOUR;

	if (diff) {
		opts.oflags &= ~LOWDOWN_TERM_NOCOLOUR;
		if (!lowdown_file_diff
		    (&opts, fin, din, &ret, &retsz))
			errx(1, "%s: failed parse", fnin);
	} else {
		if (!lowdown_file(&opts, fin, &ret, &retsz, &mq))
			errx(1, "%s: failed parse", fnin);
	}

	if (extract != NULL) {
		assert(!diff);
		TAILQ_FOREACH(m, &mq, entries) 
			if (strcasecmp(m->key, extract) == 0)
				break;
		if (m != NULL) {
			fprintf(fout, "%s\n", m->value);
		} else {
			status = 1;
			warnx("%s: unknown keyword", extract);
		}
	} else if (list) {
		assert(!diff);
		TAILQ_FOREACH(m, &mq, entries)
			fprintf(fout, "%s\n", m->key);
	} else
		fwrite(ret, 1, retsz, fout);

	free(ret);
	free(nroffcodefn);
	free(templptr);
	free(odtstyleptr);

	if (fout != stdout)
		fclose(fout);
	if (din != NULL)
		fclose(din);
	if (fin != stdin)
		fclose(fin);

	for (i = 0; i < opts.metasz; i++)
		free(opts.meta[i]);
	for (i = 0; i < opts.metaovrsz; i++)
		free(opts.metaovr[i]);

	free(opts.meta);
	free(opts.metaovr);

	lowdown_metaq_free(&mq);
	return status;
usage:
	if (!diff) {
		fprintf(stderr, 
			"usage: lowdown [-Ls] [input_options] "
			"[output_options] [-M metadata]\n"
			"               [-m metadata] "
			"[-o output] [-t mode] [-X keyword] [file]\n");
	} else
		fprintf(stderr, 
			"usage: lowdown-diff [-s] [input_options] "
			"[output_options] [-M metadata]\n"
			"                    [-m metadata] "
			"[-o output] [-t mode] oldfile [newfile]\n");
	return 1;
}
