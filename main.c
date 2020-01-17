/*	$Id$ */
/*
 * Copyright (c) 2016, 2017, 2020 Kristaps Dzonsons <kristaps@bsd.lv>
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

	if (pledge("stdio", NULL) == -1)
		err(EXIT_FAILURE, "pledge");
}

static void
sandbox_pre(void)
{

	if (pledge("stdio rpath wpath cpath", NULL) == -1)
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
	if (rc != 0)
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

	if (fddin != -1) {
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

	if (buf != NULL)
		fprintf(stderr, "%s: %s: %s\n", (const char *)arg,
			lowdown_errstr(er), buf);
	else
		fprintf(stderr, "%s: %s\n", (const char *)arg,
			lowdown_errstr(er));

}

static unsigned int
feature_out(const char *v)
{

	if (v == NULL)
		return 0;
	if (strcasecmp(v, "html-skiphtml") == 0)
		return LOWDOWN_HTML_SKIP_HTML;
	if (strcasecmp(v, "html-escape") == 0)
		return LOWDOWN_HTML_ESCAPE;
	if (strcasecmp(v, "html-hardwrap") == 0)
		return LOWDOWN_HTML_HARD_WRAP;
	if (strcasecmp(v, "html-head-ids") == 0)
		return LOWDOWN_HTML_HEAD_IDS;
	if (strcasecmp(v, "nroff-skiphtml") == 0)
		return LOWDOWN_NROFF_SKIP_HTML;
	if (strcasecmp(v, "nroff-hardwrap") == 0)
		return LOWDOWN_NROFF_HARD_WRAP;
	if (strcasecmp(v, "nroff-groff") == 0)
		return LOWDOWN_NROFF_GROFF;
	if (strcasecmp(v, "nroff-numbered") == 0)
		return LOWDOWN_NROFF_NUMBERED;
	if (strcasecmp(v, "smarty") == 0)
		return LOWDOWN_SMARTY;

	warnx("%s: unknown feature", v);
	return 0;
}

static unsigned int
feature_in(const char *v)
{

	if (v == NULL)
		return 0;
	if (strcasecmp(v, "tables") == 0)
		return LOWDOWN_TABLES;
	if (strcasecmp(v, "fenced") == 0)
		return LOWDOWN_TABLES;
	if (strcasecmp(v, "footnotes") == 0)
		return LOWDOWN_FOOTNOTES;
	if (strcasecmp(v, "autolink") == 0)
		return LOWDOWN_AUTOLINK;
	if (strcasecmp(v, "strike") == 0)
		return LOWDOWN_STRIKE;
	if (strcasecmp(v, "hilite") == 0)
		return LOWDOWN_HILITE;
	if (strcasecmp(v, "super") == 0)
		return LOWDOWN_SUPER;
	if (strcasecmp(v, "math") == 0)
		return LOWDOWN_MATH;
	if (strcasecmp(v, "nointem") == 0)
		return LOWDOWN_NOINTEM;
	if (strcasecmp(v, "nocodeind") == 0)
		return LOWDOWN_NOCODEIND;
	if (strcasecmp(v, "metadata") == 0)
		return LOWDOWN_METADATA;
	if (strcasecmp(v, "commonmark") == 0)
		return LOWDOWN_COMMONMARK;

	warnx("%s: unknown feature", v);
	return 0;
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

	if (strcasecmp(getprogname(), "lowdown-diff") == 0) 
		diff = 1;

	while ((c = getopt(argc, argv, "D:d:E:e:sT:o:vX:")) != -1)
		switch (c) {
		case 'D':
			if ((feat = feature_out(optarg)) == 0)
				goto usage;
			opts.oflags &= ~feat;
			break;
		case 'd':
			if ((feat = feature_in(optarg)) == 0)
				goto usage;
			opts.feat &= ~feat;
			break;
		case 'E':
			if ((feat = feature_out(optarg)) == 0)
				goto usage;
			opts.oflags |= feat;
			break;
		case 'e':
			if ((feat = feature_in(optarg)) == 0)
				goto usage;
			opts.feat |= feat;
			break;
		case 'o':
			fnout = optarg;
			break;
		case 's':
			standalone = 1;
			break;
		case 'T':
			if (strcasecmp(optarg, "ms") == 0)
				opts.type = LOWDOWN_NROFF;
			else if (strcasecmp(optarg, "html") == 0)
				opts.type = LOWDOWN_HTML;
			else if (strcasecmp(optarg, "man") == 0)
				opts.type = LOWDOWN_MAN;
			else if (strcasecmp(optarg, "term") == 0)
				opts.type = LOWDOWN_TERM;
			else if (strcasecmp(optarg, "tree") == 0)
				opts.type = LOWDOWN_TREE;
			else
				goto usage;
			break;
		case 'v':
			opts.msg = message;
			break;
		case 'X':
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

	if (diff && extract != NULL)
		errx(EXIT_FAILURE, "-X not applicable to diff mode");

	if ((diff && (argc == 0 || argc > 2)) || (!diff && argc > 1))
		goto usage;

	if (diff) {
		if (argc > 1 && strcmp(argv[1], "-")) {
			fnin = argv[1];
			if ((fin = fopen(fnin, "r")) == NULL)
				err(EXIT_FAILURE, "%s", fnin);
		}
		fndin = argv[0];
		if ((din = fopen(fndin, "r")) == NULL)
			err(EXIT_FAILURE, "%s", fndin);
	} else {
		if (argc && strcmp(argv[0], "-")) {
			fnin = argv[0];
			if ((fin = fopen(fnin, "r")) == NULL)
				err(EXIT_FAILURE, "%s", fnin);
		}
	}

	/* Configure the output file. */

	if (fnout != NULL && strcmp(fnout, "-") &&
	    (fout = fopen(fnout, "w")) == NULL)
		err(EXIT_FAILURE, "%s", fnout);

	sandbox_post(fileno(fin), din == NULL ? 
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
		if (!lowdown_file_diff(&opts, fin, &dopts, din, &ret, &retsz))
			err(EXIT_FAILURE, "%s", fnin);
	} else {
		opts.arg = (void *)fnin;
		if (!lowdown_file(&opts, fin, &ret, &retsz, &m, &msz))
			err(EXIT_FAILURE, "%s", fnin);
	}

	if (extract != NULL) {
		for (i = 0; i < msz; i++) 
			if (strcasecmp(m[i].key, extract) == 0)
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
	if (fin != stdin)
		fclose(fin);
	for (i = 0; i < msz; i++) {
		free(m[i].key);
		free(m[i].value);
	}
	free(m);
	return status;
usage:
	fprintf(stderr, "usage: lowdown "
		"[-sv] "
		"[-D feature] "
		"[-d feature] "
		"[-E feature] "
		"[-e feature]\n"
		"               "
		"[-o output] "
		"[-T mode] "
		"[file]\n");
	fprintf(stderr, "       lowdown "
		"[-v] "
		"[-o output] "
		"[-T mode] "
		"[-X keyword] "
		"[file]\n");
	fprintf(stderr, "       lowdown-diff "
		"[-sv] "
		"[-D feature] "
		"[-d feature] "
		"[-E feature]\n"
		"                    "
		"[-e feature] "
		"[-o output] "
		"[-T mode] "
		"oldfile "
		"[file]\n");

	return EXIT_FAILURE;
}
