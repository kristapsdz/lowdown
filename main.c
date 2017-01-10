/*	$Id$ */
/*
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
#include <sys/param.h>
#ifdef __FreeBSD__
# include <sys/resource.h>
# include <sys/capability.h>
#endif

#include <err.h>
#include <errno.h>
#ifdef __APPLE__
# include <sandbox.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "lowdown.h"

/*
 * Start with all of the sandboxes.
 * The sandbox_pre() happens before we open our input file for reading,
 * while the sandbox_post() happens afterward.
 */

#if defined(__OpenBSD__) && OpenBSD > 201510

static void
sandbox_post(int fdin, int fdout)
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

#elif defined(__APPLE__)

static void
sandbox_post(int fdin, int fdout)
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

#elif defined(__FreeBSD__)

static void
sandbox_post(int fdin, int fdout)
{
	cap_rights_t	 rights;

	cap_rights_init(&rights);

	cap_rights_init(&rights, CAP_EVENT, CAP_READ, CAP_FSTAT);
	if (cap_rights_limit(fdin, &rights) < 0) 
 		err(EXIT_FAILURE, "cap_rights_limit");

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
sandbox_post(int fdin, int fdout)
{

	/* Do nothing. */
}

static void
sandbox_pre(void)
{

	/* Do nothing. */
}

#endif

int
main(int argc, char *argv[])
{
	FILE		*fin = stdin, *fout = stdout;
	const char	*fnin = "<stdin>", *fnout = NULL,
	      		*title = NULL;
	struct lowdown_opts opts;
	const char	*pname;
	int		 c, standalone = 0;
	struct tm	*tm;
	char		 buf[32];
	unsigned char	*ret = NULL;
	size_t		 retsz = 0;
	time_t		 t = time(NULL);

	memset(&opts, 0, sizeof(struct lowdown_opts));

	opts.type = LOWDOWN_HTML;

	tm = localtime(&t);

	sandbox_pre();

#ifdef __linux__
	pname = argv[0];
#else
	if (argc < 1)
		pname = "lowdown";
	else if (NULL == (pname = strrchr(argv[0], '/')))
		pname = argv[0];
	else
		++pname;
#endif

	while (-1 != (c = getopt(argc, argv, "st:T:o:")))
		switch (c) {
		case ('T'):
			if (0 == strcasecmp(optarg, "ms"))
				opts.type = LOWDOWN_NROFF;
			else if (0 == strcasecmp(optarg, "html"))
				opts.type = LOWDOWN_HTML;
			else if (0 == strcasecmp(optarg, "man"))
				opts.type = LOWDOWN_MAN;
			else
				goto usage;
			break;
		case ('t'):
			title = optarg;
			break;
		case ('s'):
			standalone = 1;
			break;
		case ('o'):
			fnout = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (argc > 0 && strcmp(argv[0], "-")) {
		fnin = argv[0];
		if (NULL == (fin = fopen(fnin, "r")))
			err(EXIT_FAILURE, "%s", fnin);
	}

	if (NULL != fnout && strcmp(fnout, "-")) {
		if (NULL == (fout = fopen(fnout, "w")))
			err(EXIT_FAILURE, "%s", fnout);
	}

	sandbox_post(fileno(fin), fileno(fout));

	/* 
	 * We're now completely sandboxed.
	 * Nothing more is allowed to happen.
	 */

	if ( ! lowdown_file(&opts, fin, &ret, &retsz))
		err(EXIT_FAILURE, "%s", fnin);

	if (fin != stdin) 
		fclose(fin);

	if (LOWDOWN_HTML == opts.type) {
		if (standalone)
			fprintf(fout, "<!DOCTYPE html>\n"
			      "<html>\n"
			      "<head>\n"
			      "<meta charset=\"utf-8\">\n"
			      "<meta name=\"viewport\" content=\""
			       "width=device-width,initial-scale=1\">\n"
			      "<title>%s</title>\n"
			      "</head>\n"
			      "<body>\n", NULL == title ?
			      "Untitled article" : title);
		fwrite(ret, 1, retsz, fout);
		if (standalone)
			fputs("</body>\n"
			      "</html>\n", fout);
	} else {
		strftime(buf, sizeof(buf), "%F", tm);
		if (standalone && LOWDOWN_NROFF == opts.type)
			fprintf(fout, ".TL\n%s\n", 
				NULL == title ?
				"Untitled article" : title);
		else if (standalone && LOWDOWN_MAN == opts.type)
			fprintf(fout, ".TH \"%s\" 7 %s\n", 
				NULL == title ?  
				"UNTITLED ARTICLE" : title, buf);
		fwrite(ret, 1, retsz, fout);
	}

	free(ret);
	if (fout != stdout)
		fclose(fout);
	return(EXIT_SUCCESS);
usage:
	fprintf(stderr, "usage: %s "
		"[-s] "
		"[-o output] "
		"[-t title] "
		"[-T mode] "
		"[file]\n", pname);
	return(EXIT_FAILURE);
}
