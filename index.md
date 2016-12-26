# lowdown --- simple Markdown translator

*lowdown* is just another Markdown translator.  It can output
traditional HTML or input for your *troff* type-setter of choice, such
as [groff(1)](https://www.gnu.org/s/groff/), [Heirloom
troff](http://heirloom.sourceforge.net/doctools.html), or even
[mandoc(1)](http://man.openbsd.org/mandoc).  *lowdown* doesn't require
XSLT, Python, or even Perl -- it's just clean, [open
source](http://opensource.org/licenses/ISC) C code.  Its canonical
documentation is the [lowdown(1)](lowdown.1.html) manpage.

I wrote *lowdown* to provide a secure Markdown formatter on
[OpenBSD](http://www.openbsd.org), compiling into both HTML and PDF (the
latter via [mandoc(1)](http://man.openbsd.org/mandoc) or groff from
ports).  In the former case, I usually use it with
[sblg(1)](https://kristaps.bsd.lv/sblg) to pull together blog articles.
It was originally a fork of
*[hoedown](https://github.com/hoedown/hoedown)* and inherits from the hard
work of its authors (and those of *hoedown*'s origin,
*[sundown](https://github.com/vmg/sundown)*), but has changed
significantly.

Want an example?  For starters: this page, [index.md](index.md).  The
Markdown input is translated into XML using *lowdown*, then further into
HTML5 using [sblg(1)](https://kristaps.bsd.lv/sblg).  You can also see
it as [index.pdf](index.pdf), generated from
[groff(1)](https://www.gnu.org/s/groff/) in **-ms** mode.  Another
example is the GitHub [README.md](README.md) generated as
[README.html](README.html) or [README.pdf](README.pdf).

## Output

Of course, *lowdown* supports the usual HTML output. Specifically, it
forces HTML5 in XML mode.  You can use *lowdown* to create either a
snippet or standalone HTML5 document.

It also supports outputting to the **-ms** macros, originally
implemented for the *roff* typesetting package of Version 7 AT&T UNIX.
This way, you can have elegant PDF and PS output by using any modern
*troff* system such as [groff(1)](https://www.gnu.org/s/groff).

Furthermore, it supports the **-man** macros, also from Version 7
AT&T UNIX.  Beyond the usual *troff* systems, this is also supported by
[mandoc(1)](http://mdocml.bsd.lv).  You may be tempted to write
[manpages](http://man.openbsd.org) in Markdown, but please don't: use
[mdoc(7)](http://man.openbsd.org/mdoc), instead --- it's built for that
purpose!  The **-man** output is for technical documentation (section
7).

Both the **-ms** and **-man** output modes disallow images and
equations.  The former by definition, the latter due to (not
insurmountable) complexity of converting LaTeX to
[eqn(7)](http://man.openbsd.org/eqn).

## Input

Beyond the basic Markdown syntax support, *lowdown* supports the
following Markdown features:

- autolinking
- fenced code
- tables
- superscripts
- footnotes
- disabled inline HTML
- "smartypants"

The only additional non-canonical Markdown feature is wrapping the
initial paragraph of XHTML output in an `<aside>` block.  This is for
integration with [sblg(1)](https://kristaps.bsd.lv/sblg).
