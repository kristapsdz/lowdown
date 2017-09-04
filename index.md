title: lowdown --- simple markdown translator
author: Kristaps Dzonsons

# [%title]

*lowdown* is a Markdown translator producing HTML5 and *roff* documents
in the **ms** and **man** formats.  It doesn't require XSLT, Python, or
external libraries -- it's just clean, secure, [open
source](http://opensource.org/licenses/ISC) C code with no dependencies.
Its canonical documentation is the [lowdown(1)](lowdown.1.html) manpage
with the library interface at [lowdown(3)](lowdown.3.html).

*lowdown* started as a fork of
[hoedown](https://github.com/hoedown/hoedown) to add sandboxing
([pledge(2)](http://man.openbsd.org/pledge),
[capsicum(4)](https://www.freebsd.org/cgi/man.cgi?query=capsicum&sektion=4),
or
[sandbox\_init(3)](https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/sandbox_init.3.html))
and *roff* output to securely generate PDFs on
[OpenBSD](http://www.openbsd.org) with just
[mandoc(1)](http://man.openbsd.org/mandoc).

Want an example?  For starters: this page, [index.md](index.md).  The
Markdown input is rendered an HTML5 fragment using *lowdown*, then
further using [sblg](https://kristaps.bsd.lv/sblg).  You
can also see it as [index.pdf](index.pdf), generated from
[groff(1)](https://www.gnu.org/s/groff/) from **ms** output.  Another
example is the GitHub [README.md](README.md) rendered as
[README.html](README.html) or [README.pdf](README.pdf).

To get *lowdown*, just [download](snapshots/lowdown.tar.gz),
[verify](snapshots/lowdown.tar.gz.sha512), unpack, run `./configure`,
then run `doas make install` (or use `sudo`).  *lowdown* is a
[BSD.lv](https://bsd.lv) project.
[Homebrew](https://brew.sh) users can use BSD.lv's
[tap](https://github.com/kristapsdz/homebrew-repo).

If you can help it, however,
don't use Markdown.  Why? Read [Ingo's comments on
Markdown](https://undeadly.org/cgi?action=article&sid=20170304230520)
for a good explanation.

## Output

Of course, *lowdown* supports the usual HTML output. Specifically, it
produces HTML5 in XML mode.  You can use *lowdown* to create either a
snippet or standalone HTML5 document.

It also supports outputting to the **ms** macros, originally
implemented for the *roff* typesetting package of Version 7 AT&T UNIX.
This way, you can have elegant PDF and PS output by using any modern
*troff* system such as [groff(1)](https://www.gnu.org/s/groff).

Furthermore, it supports the **man** macros, also from Version 7
AT&T UNIX.  Beyond the usual *troff* systems, this is also supported by
[mandoc](https://mdocml.bsd.lv).

You may be tempted to write [manpages](https://man.openbsd.org) in
Markdown, but please don't: use [mdoc(7)](https://man.openbsd.org/mdoc),
instead --- it's built for that purpose!  The **man** output is for
technical documentation only (section 7).

Both the **ms** and **man** output modes disallow images and
equations.  The former by definition (although **ms** might have a
future with some elbow grease), the latter due to (not insurmountable)
complexity of converting LaTeX to [eqn(7)](https://man.openbsd.org/eqn).

You can control output features by using the **-D** (disable feature)
and **-E** (enable feature) flags documented in
[lowdown.1.html](lowdown.1.html).

## Input

Beyond the basic Markdown syntax support, *lowdown* supports the
following Markdown features and extensions:

- autolinking
- fenced code
- tables
- superscripts
- footnotes
- disabled inline HTML
- "smartypants"
- metadata

You can control which parser features are used by using the **-d**
(disable feature) and **-e** (enable feature) flags documented in
[lowdown.1.html](lowdown.1.html).

## Examples

I usually use *lowdown* when writing
[sblg](https://kristaps.bsd.lv/sblg) articles when I'm too lazy to
write in proper HTML5.
(For those not in the know, [sblg](https://kristaps.bsd.lv/sblg) is a
simple tool for knitting together blog articles into a blog feed.)
This basically means wrapping the output of *lowdown* in the elements
indicating a blog article.
I do this in my Makefiles:

```Makefile
.md.xml:
     ( echo "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" ; \
       echo "<article data-sblg-article=\"1\">" ; \
       echo "<header>" ; \
       echo "<h1>" ; \
       lowdown -X title $< ; \
       echo "</h1>" ; \
       echo "<aside>" ; \
       lowdown -X htmlaside $< ; \
       echo "</aside>" ; \
       echo "</header>" ; \
       lowdown $< ; \
       echo "</article>" ; ) >$@
```

If you just want a straight-up HTML5 file, use standalone mode:

```sh
lowdown -s -o README.html README.md
```

This can use the document's meta-data to populate the title, CSS file,
and so on.

The troff output modes work well to make PS or PDF files, although they
will omit graphics and equations.
There is a possibility to later add support for PIC, but even then, it
will only support specific types of graphics.
The extra groff arguments in the following invocation are for UTF-8
processing (**-k** and **-Dutf8**), tables (**-t**), and clickable links
(**-mpdfmark**).

```sh
lowdown -s -Tms README.md | \
  groff -k -Dutf8 -t -ms -mpdfmark > README.ps
```

On OpenBSD or other BSD systems, you can run *lowdown* within the base
system to produce PDF or PS files via [mandoc](http://mdocml.bsd.lv):

```sh
lowdown -s -Tman README.md | mandoc -Tpdf > README.pdf
```

Read [lowdown(1)](lowdown.1.html) for details on running the system.

## Library

*lowdown* is also available as a library, [lowdown(3)](lowdown.3.html).
This effectively wraps around everything invoked by
[lowdown(1)](lowdown.1.html), so it's basically the same but... a
library.

## Testing

The canonical Markdown test, such as found in the original
[hoedown](https://github.com/hoedown/hoedown) sources, will not
currently work with *lowdown* because of the mandatory "smartypants" and
other extensions.

I've extensively run [AFL](http://lcamtuf.coredump.cx/afl/) against the
compiled sources with no failures --- definitely a credit to
the [hoedown](https://github.com/hoedown/hoedown) authors (and those
from who they forked their own sources).  I'll also regularly run the system
through [valgrind](http://valgrind.org/), also without issue.

*lowdown* has a [Coverity](https://scan.coverity.com/projects/lowdown)
registration for static analysis.

## Hacking

Want to hack on *lowdown*?  Of course you do.  (Or maybe you should
focus on better PS and PDF output for
[mandoc(1)](http://mdocml.bsd.lv).)

First, start in
[library.c](https://github.com/kristapsdz/lowdown/blob/master/library.c).
(The [main.c](https://github.com/kristapsdz/lowdown/blob/master/main.c)
file is just a caller to the library interface.)
Both the renderer (which renders the parsed document contents in the
output format) and the document (which generates the parse AST) are
initialised.

The parse is started in
[document.c](https://github.com/kristapsdz/lowdown/blob/master/document.c).
It is preceded by meta-data parsing, if applicable, which occurs before
document parsing but after the BOM.
The document is parsed into an AST (abstract syntax tree) that describes
the document as a tree of nodes, each node corresponding an input token.
Once the entire tree has been generated, the AST is passed into the
front-end renderers, which construct output depth-first.

There are two renderers supported:
[html.c](https://github.com/kristapsdz/lowdown/blob/master/html.c) for
HTML5 output and
[nroff.c](https://github.com/kristapsdz/lowdown/blob/master/nroff.c) for
**-ms** and **-man** output.

### Example

For example, consider the following:

```markdown
## Hello **world**
```

First, the outer block (the subsection) would begin parsing.  The parser
would then step into the subcomponent: the header contents.  It would
then render the subcomponents in order: first the regular text "Hello",
then a bold section.  The bold section would be its own subcomponent
with its own regular text child, "world".

When run through the **-Ttree** output, it would generate:

```
LOWDOWN_ROOT
  LOWDOWN_DOC_HEADER
  LOWDOWN_HEADER
    LOWDOWN_NORMAL_TEXT
      data: 6 Bytes: Hello 
    LOWDOWN_DOUBLE_EMPHASIS
      LOWDOWN_NORMAL_TEXT
        data: 5 Bytes: world
  LOWDOWN_DOC_FOOTER
```

This tree would then be passed into a front-end, such as the HTML5
front-end with **-Thtml**.  The nodes would be appended into a buffer,
which would then be passed back into the subsection parser.  It would
paste the buffer into `<h2>` blocks (in HTML5) or a `.SH` block (troff
outputs).

Finally, the subsection block would be fitted into whatever context it
was invoked within.

### Escaping

The only time that "real text" is passed directly from the input buffer
into the output renderer is when then `normal_text` callback is invoked,
blockcode or codespan, raw HTML, or hyperlink components.  In both
renderers, you can see how the input is properly escaped by passing into
[escape.c](https://github.com/kristapsdz/lowdown/blob/master/escape.c).

After being fully parsed into an output buffer, the output buffer is
passed into a "smartypants" rendering, one for each renderer type.

## Known Issues (or, How You Can Help)

There are some known issues, mostly in PDF (**-Tms** and **-Tman**)
output.

Foremost, there needs to be a font modifier stack, as this feature is
not supported directly in the roff language.
For example, if one execute \*foo \*\*bar\*\* baz\*, the output will be
confused because this translate to \fIfoo \fBbar\fP baz\fP. 

Second, there needs to be logic to handle when a link is the first or
last component of a font change.  For example, \*\[foo\](...)\* will put
the font markers on different lines.

In all modes, the "smartypants" formatting should be embedded in
document output --- not in a separate step as implemented in the
original sources.

Lastly, I'd like a full reference of the Markdown language accepted as a
manpage.  Markdown is incredibly inconsistent, so a simple, readable
document would be very helpful.
