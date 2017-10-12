/*	$Id$ */
/*
 * Copyright (c) 2016, 2017 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <sys/param.h>
#if HAVE_CAPSICUM
# include <sys/resource.h>
# include <sys/capability.h>
#endif

#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#if HAVE_SANDBOX_INIT
# include <sandbox.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
}

static void
sandbox_pre(void)
{

	if (-1 == pledge("stdio rpath wpath cpath", NULL))
		err(EXIT_FAILURE, "pledge");
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
	if (0 != rc)
		errx(EXIT_FAILURE, "sandbox_init: %s", ep);
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
 		err(EXIT_FAILURE, "cap_rights_limit");

	if (-1 != fddin) {
		cap_rights_init(&rights, 
			CAP_EVENT, CAP_READ, CAP_FSTAT);
		if (cap_rights_limit(fddin, &rights) < 0)
			err(EXIT_FAILURE, "cap_rights_limit");
	}

	cap_rights_init(&rights, CAP_EVENT, CAP_WRITE, CAP_FSTAT);
	if (cap_rights_limit(STDERR_FILENO, &rights) < 0)
 		err(EXIT_FAILURE, "cap_rights_limit");

	cap_rights_init(&rights, CAP_EVENT, CAP_WRITE, CAP_FSTAT);
	if (cap_rights_limit(fdout, &rights) < 0)
 		err(EXIT_FAILURE, "cap_rights_limit");

	if (cap_enter())
		err(EXIT_FAILURE, "cap_enter");
}

static void
sandbox_pre(void)
{

	/* Do nothing. */
}

#else /* No sandbox. */

#warning Compiling without sandbox support.

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

static void
message(enum lowdown_err er, void *arg, const char *buf)
{

	if (NULL != buf)
		fprintf(stderr, "%s: %s: %s\n", (const char *)arg,
			lowdown_errstr(er), buf);
	else
		fprintf(stderr, "%s: %s\n", (const char *)arg,
			lowdown_errstr(er));

}

static unsigned int
feature_out(const char *v)
{

	if (NULL == v)
		return(0);
	if (0 == strcasecmp(v, "html-skiphtml"))
		return(LOWDOWN_HTML_SKIP_HTML);
	if (0 == strcasecmp(v, "html-escape"))
		return(LOWDOWN_HTML_ESCAPE);
	if (0 == strcasecmp(v, "html-hardwrap"))
		return(LOWDOWN_HTML_HARD_WRAP);
	if (0 == strcasecmp(v, "html-head-ids"))
		return(LOWDOWN_HTML_HEAD_IDS);
	if (0 == strcasecmp(v, "nroff-skiphtml"))
		return(LOWDOWN_NROFF_SKIP_HTML);
	if (0 == strcasecmp(v, "nroff-hardwrap"))
		return(LOWDOWN_NROFF_HARD_WRAP);
	if (0 == strcasecmp(v, "nroff-groff"))
		return(LOWDOWN_NROFF_GROFF);
	if (0 == strcasecmp(v, "nroff-numbered"))
		return(LOWDOWN_NROFF_NUMBERED);
	if (0 == strcasecmp(v, "smarty"))
		return(LOWDOWN_SMARTY);

	warnx("%s: unknown feature", v);
	return(0);
}

static unsigned int
feature_in(const char *v)
{

	if (NULL == v)
		return(0);
	if (0 == strcasecmp(v, "tables"))
		return(LOWDOWN_TABLES);
	if (0 == strcasecmp(v, "fenced"))
		return(LOWDOWN_TABLES);
	if (0 == strcasecmp(v, "footnotes"))
		return(LOWDOWN_FOOTNOTES);
	if (0 == strcasecmp(v, "autolink"))
		return(LOWDOWN_AUTOLINK);
	if (0 == strcasecmp(v, "strike"))
		return(LOWDOWN_STRIKE);
	if (0 == strcasecmp(v, "hilite"))
		return(LOWDOWN_HILITE);
	if (0 == strcasecmp(v, "super"))
		return(LOWDOWN_SUPER);
	if (0 == strcasecmp(v, "math"))
		return(LOWDOWN_MATH);
	if (0 == strcasecmp(v, "nointem"))
		return(LOWDOWN_NOINTEM);
	if (0 == strcasecmp(v, "mathexp"))
		return(LOWDOWN_MATHEXP);
	if (0 == strcasecmp(v, "nocodeind"))
		return(LOWDOWN_NOCODEIND);
	if (0 == strcasecmp(v, "metadata"))
		return(LOWDOWN_METADATA);
	if (0 == strcasecmp(v, "commonmark"))
		return(LOWDOWN_COMMONMARK);

	warnx("%s: unknown feature", v);
	return(0);
}

int
main(int argc, char *argv[])
{
	FILE		*fin = stdin, *fout = stdout, *din = NULL;
	const char	*fnin = "<stdin>", *fnout = NULL,
	      	 	*fndin = NULL, *extract = NULL;
	struct lowdown_opts opts, dopts;
	int		 c, standalone = 0, status = EXIT_SUCCESS,
			 diff = 0;
	char		*ret = NULL;
	int	 	 feat;
	size_t		 i, retsz = 0, msz = 0;
	struct lowdown_meta *m = NULL;

	memset(&opts, 0, sizeof(struct lowdown_opts));

	opts.type = LOWDOWN_HTML;
	opts.feat = LOWDOWN_FOOTNOTES |
		LOWDOWN_AUTOLINK |
		LOWDOWN_TABLES |
		LOWDOWN_SUPER |
		LOWDOWN_STRIKE |
		LOWDOWN_FENCED |
		LOWDOWN_COMMONMARK |
		LOWDOWN_METADATA;
	opts.oflags = 
		LOWDOWN_NROFF_SKIP_HTML |
		LOWDOWN_HTML_SKIP_HTML |
		LOWDOWN_NROFF_GROFF |
		LOWDOWN_SMARTY |
		LOWDOWN_HTML_HEAD_IDS;

	sandbox_pre();

	if (0 == strcasecmp(getprogname(), "lowdown-diff")) 
		diff = 1;

	while (-1 != (c = getopt(argc, argv, "D:d:E:e:sT:o:vX:")))
		switch (c) {
		case ('D'):
			if (0 == (feat = feature_out(optarg)))
				goto usage;
			opts.oflags &= ~feat;
			break;
		case ('d'):
			if (0 == (feat = feature_in(optarg)))
				goto usage;
			opts.feat &= ~feat;
			break;
		case ('E'):
			if (0 == (feat = feature_out(optarg)))
				goto usage;
			opts.oflags |= feat;
			break;
		case ('e'):
			if (0 == (feat = feature_in(optarg)))
				goto usage;
			opts.feat |= feat;
			break;
		case ('o'):
			fnout = optarg;
			break;
		case ('s'):
			standalone = 1;
			break;
		case ('T'):
			if (0 == strcasecmp(optarg, "ms"))
				opts.type = LOWDOWN_NROFF;
			else if (0 == strcasecmp(optarg, "html"))
				opts.type = LOWDOWN_HTML;
			else if (0 == strcasecmp(optarg, "man"))
				opts.type = LOWDOWN_MAN;
			else if (0 == strcasecmp(optarg, "tree"))
				opts.type = LOWDOWN_TREE;
			else
				goto usage;
			break;
		case ('v'):
			opts.msg = message;
			break;
		case ('X'):
			extract = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	/* 
	 * Diff mode takes two arguments: the first is mandatory (the
	 * old file) and the second (the new one) is optional.
	 * Non-diff mode takes an optional single argument.
	 */

	if (diff && NULL != extract)
		errx(EXIT_FAILURE, "-X not applicable to diff mode");

	if ((diff && (0 == argc || argc > 2)) ||
	    ( ! diff && argc > 1))
		goto usage;

	if (diff) {
		if (argc > 1 && strcmp(argv[1], "-")) {
			fnin = argv[1];
			if (NULL == (fin = fopen(fnin, "r")))
				err(EXIT_FAILURE, "%s", fnin);
		}
		fndin = argv[0];
		if (NULL == (din = fopen(fndin, "r")))
			err(EXIT_FAILURE, "%s", fndin);
	} else {
		if (argc && strcmp(argv[0], "-")) {
			fnin = argv[0];
			if (NULL == (fin = fopen(fnin, "r")))
				err(EXIT_FAILURE, "%s", fnin);
		}
	}

	/* Configure the output file. */

	if (NULL != fnout && strcmp(fnout, "-") &&
	    NULL == (fout = fopen(fnout, "w")))
		err(EXIT_FAILURE, "%s", fnout);

	sandbox_post(fileno(fin), NULL == din ? 
		-1 : fileno(din), fileno(fout));

	/* We're now completely sandboxed. */

	/* Require metadata when extracting. */

	if (extract)
		opts.feat |= LOWDOWN_METADATA;
	if (standalone)
		opts.oflags |= LOWDOWN_STANDALONE;

	if (diff) {
		dopts = opts;
		opts.arg = (void *)fnin;
		dopts.arg = (void *)fndin;
		if ( ! lowdown_file_diff(&opts, fin, &dopts, din, &ret, &retsz))
			err(EXIT_FAILURE, "%s", fnin);
	} else {
		opts.arg = (void *)fnin;
		if ( ! lowdown_file(&opts, fin, &ret, &retsz, &m, &msz))
			err(EXIT_FAILURE, "%s", fnin);
		if (fin != stdin)
			fclose(fin);
	}

	if (NULL != extract) {
		for (i = 0; i < msz; i++) 
			if (0 == strcasecmp(m[i].key, extract))
				break;
		if (i < msz) {
			fprintf(fout, "%s\n", m[i].value);
		} else {
			status = EXIT_FAILURE;
			warnx("%s: unknown keyword", extract);
		}
	} else
		fwrite(ret, 1, retsz, fout);

	free(ret);
	if (fout != stdout)
		fclose(fout);
	if (NULL != din)
		fclose(din);
	for (i = 0; i < msz; i++) {
		free(m[i].key);
		free(m[i].value);
	}
	free(m);
	return(status);
usage:
	fprintf(stderr, "usage: %s "
		"[-sv] "
		"[-D feature] "
		"[-d feature] "
		"[-E feature] "
		"[-e feature] "
		"[-o output] "
		"[-T mode] "
		"[-X keyword] "
		"[file]\n", getprogname());
	return(EXIT_FAILURE);
}
