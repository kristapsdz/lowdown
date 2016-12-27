# lowdown --- simple Markdown translator

*lowdown* is just another Markdown translator.  It can output
traditional HTML or input for your *troff* type-setter of choice, such
as [groff(1)](https://www.gnu.org/s/groff/), [Heirloom
troff](http://heirloom.sourceforge.net/doctools.html), or even
[mandoc(1)](http://man.openbsd.org/mandoc).  *lowdown* doesn't require
XSLT, Python, or even Perl -- it's just clean, [open
source](http://opensource.org/licenses/ISC) C code.  Its canonical
documentation is the [lowdown(1)](lowdown.1.html) manpage.

I wrote *lowdown* to provide a secure (using 
[pledge(2)](http://man.openbsd.org/pledge),
[capsicum(4)](https://www.freebsd.org/cgi/man.cgi?query=capsicum&sektion=4),
or
[sandbox\_init(3)](https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/sandbox_init.3.html))
Markdown formatter on
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

To get *lowdown*, just download, unpack, verify, then run `doas make
install` (or use `sudo`).

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

## Examples

I usually use **lowdown** when writing
[sblg(1)](https://kristaps.bsd.lv/sblg) articles when I'm too lazy to
write in proper HTML5.
(For those not in the know, [sblg(1)](https://kristaps.bsd.lv/sblg) is a
simple tool for knitting together blog articles into a blog feed.)
This basically means wrapping the output of **lowdown** in the elements
indicating a blog article.
I do this in my Makefiles:

```Makefile
.md.xml:
	( echo "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" ; \
	  echo "<article data-sblg-article=\"1\">" ; \
	  lowdown $< ; \
	  echo "</article>" ; ) >$@
```

If you just want a straight-up HTML5 file, use standalone mode:

```sh
lowdown -s -o README.html README.md
```

Using the nroff output mode works well when making PS or PDF files,
although it will omit graphics and equations.
(There is a possibility to later add support for PIC, but even then, it
will only support specific types of graphics.)

```sh
lowdown -s -Tnroff README.md | groff -t -ms > README.ps
```

On OpenBSD or other BSD systems, you can run **lowdown** within the base
system to produce PDF or PS files via [mandoc](http://mdocml.bsd.lv):

```sh
lowdown -s -Tman README.md | mandoc -Tpdf > README.pdf
```

Read [lowdown(1)](lowdown.1.html) for details on running the system.

## Testing

The canonical Markdown test, such as found in the original
[hoedown](https://github.com/hoedown/hoedown) sources, will not
currently work with **lowdown** because of it automatically runs
"smartypants" and several extension modes.

I've extensively run [AFL](http://lcamtuf.coredump.cx/afl/) against the
compiled sources, however, with no failures --- definitely a credit to
the [hoedown](https://github.com/hoedown/hoedown) authors (and those
from who they forked their own sources).

I'll also regularly run the system through
[valgrind](http://valgrind.org/), also without issue.

**lowdown** has a [Coverity](https://scan.coverity.com/projects/lowdown)
registration for static analysis.
