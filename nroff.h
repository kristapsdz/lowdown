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
#ifndef NROFF_H
#define NROFF_H

enum	nfont {
	NFONT_ITALIC = 0, /* italic */
	NFONT_BOLD, /* bold */
	NFONT_FIXED, /* fixed-width */
	NFONT__MAX
};

enum	bscope {
	BSCOPE_BLOCK = 0,
	BSCOPE_SPAN,
	BSCOPE_SEMI,
	BSCOPE_SEMI_CLOSE,
	BSCOPE_LITERAL,
	BSCOPE_FONT,
	BSCOPE_COLOUR
};

struct 	nroff {
	struct hentryq	 	   headers_used; /* headers we've seen */
	enum lowdown_type	   type; /* man(7), ms(7), or mdoc(7) */
	int			   use_lp; /* man(7)/ms(7): use PP/LP */
	unsigned int		   flags; /* output flags */
	ssize_t			   headers_offs; /* header offset */
	const char		  *headers_sec; /* section from metadata */
	enum nfont		   fonts[NFONT__MAX]; /* see bqueue_font() */
	struct bnodeq		 **foots; /* footnotes */
	size_t			   footsz; /* footnote size */
	size_t			   footpos; /* footnote position (-tms) */
	size_t			   footdepth; /* printing/parsing footnotes */
	size_t			   indent; /* indentation width */
	const char		  *cr; /* fixed-width font */
	const char		  *cb; /* fixed-width bold font */
	const char		  *ci; /* fixed-width italic font */
	const char		  *cbi; /* fixed-width bold-italic font */
	const char		  *templ; /* output template */
	const struct lowdown_node *lastsec; /* last section seen */
};

/*
 * Instead of writing directly into the output buffer, we write
 * temporarily into bnodes, which are converted into output.  These
 * nodes are aware of whether they need surrounding newlines.
 */
struct	bnode {
	char			*nbuf; /* (safe) macro name or data */
	char			*buf; /* (unsafe) macro name or data */
	char			*nargs; /* (safe) blk/semi macro args */
	char			*args; /* (unsafe) blk/semi macro args */
	int			 close; /* BNODE_COLOUR/FONT */
	int			 tblhack; /* BSCOPE_SPAN */
        int			 headerhack; /* BSCOPE_BLOCK */
	enum bscope		 scope; /* scope */
	unsigned int		 font; /* if BNODE_FONT */
#define	BFONT_ITALIC		 0x01
#define	BFONT_BOLD		 0x02
#define	BFONT_FIXED		 0x04
	unsigned int		 colour; /* if BNODE_COLOUR */
#define	BFONT_BLUE		 0x01
#define	BFONT_RED		 0x02
	TAILQ_ENTRY(bnode)	 entries;
};

TAILQ_HEAD(bnodeq, bnode);

struct bnode *
bqueue_span(struct bnodeq *bq, const char *text);

struct bnode *
bqueue_block(struct bnodeq *bq, const char *text);

int
bqueue_font(const struct nroff *st, struct bnodeq *bq, int close);

int
bqueue_flush(const struct nroff *st, struct lowdown_buf *ob,
    const struct bnodeq *bq, unsigned int mdocline);

void
bqueue_free(struct bnodeq *bq);

int
nroff_in_section(const struct nroff *st, const char *section);

int
nroff_manpage_paragraph(struct nroff *st, const struct lowdown_node *n,
    struct bnodeq *obq, struct bnodeq *nbq);

#endif /* !NROFF_H */
