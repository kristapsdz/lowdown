title: lowdown --- simple markdown translator

# lowdown --- simple Markdown translator

*lowdown* is just another Markdown translator.  It can output
traditional HTML or a document for your *troff* type-setter of choice,
such as [groff(1)](https://www.gnu.org/s/groff/), [Heirloom
troff](http://heirloom.sourceforge.net/doctools.html), or even
[mandoc(1)](http://man.openbsd.org/mandoc).  *lowdown* doesn't require
XSLT, Python, or even Perl -- it's just clean, secure, [open
source](http://opensource.org/licenses/ISC) C code with no dependencies.
Its canonical documentation is the [lowdown(1)](lowdown.1.html) manpage.

*lowdown* started as a fork of
*[hoedown](https://github.com/hoedown/hoedown)* to add sandboxing
([pledge(2)](http://man.openbsd.org/pledge),
[capsicum(4)](https://www.freebsd.org/cgi/man.cgi?query=capsicum&sektion=4),
or
[sandbox\_init(3)](https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/sandbox_init.3.html))
and *troff* output to securely generate PDFs on
[OpenBSD](http://www.openbsd.org) with just
[mandoc(1)](http://man.openbsd.org/mandoc).  This ballooned into a
larger task (as described on the [GitHub
page](https://github.com/kristapsdz/lowdown)) due to the high amounts of
cruft in the code.

Want an example?  For starters: this page, [index.md](index.md).  The
Markdown input is rendered into XML using *lowdown*, then further into
HTML5 using [sblg(1)](https://kristaps.bsd.lv/sblg).  You can also see
it as [index.pdf](index.pdf), generated from
[groff(1)](https://www.gnu.org/s/groff/) in **-ms** mode.  Another
example is the GitHub [README.md](README.md) rendered as
[README.html](README.html) or [README.pdf](README.pdf).

To get *lowdown*, just [download](snapshots/lowdown.tar.gz),
[verify](snapshots/lowdown.tar.gz.sha512), unpack, run `./configure`,
then run `doas make install` (or use `sudo`).  *lowdown* is a
[BSD.lv](https://bsd.lv) project.
[Homebrew](https://brew.sh) users can use BSD.lv's
[tap](https://github.com/kristapsdz/homebrew-repo).

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
[mandoc(1)](http://mdocml.bsd.lv).

You may be tempted to write [manpages](http://man.openbsd.org) in
Markdown, but please don't: use [mdoc(7)](http://man.openbsd.org/mdoc),
instead --- it's built for that purpose!  The **-man** output is for
technical documentation only (section 7).

Both the **-ms** and **-man** output modes disallow images and
equations.  The former by definition (although **-ms** might have a
future with some elbow grease), the latter due to (not insurmountable)
complexity of converting LaTeX to [eqn(7)](http://man.openbsd.org/eqn).

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

## Examples

I usually use *lowdown* when writing
[sblg(1)](https://kristaps.bsd.lv/sblg) articles when I'm too lazy to
write in proper HTML5.
(For those not in the know, [sblg(1)](https://kristaps.bsd.lv/sblg) is a
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

The troff output modes work well to make PS or PDF files, although they
will omit graphics and equations.
(There is a possibility to later add support for PIC, but even then, it
will only support specific types of graphics.)

```sh
lowdown -s -Tms README.md | groff -t -ms > README.ps
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
output format) and the document (which invokes the renderer as it parses
the document) are initialised.  There are two renderers supported:
[html.c](https://github.com/kristapsdz/lowdown/blob/master/html.c) for
HTML5 output and
[nroff.c](https://github.com/kristapsdz/lowdown/blob/master/nroff.c) for
**-ms** and **-man** output.
Input and output buffers are defined in
[buffer.c](https://github.com/kristapsdz/lowdown/blob/master/buffer.c).

### Sequence

The parse is started in
[document.c](https://github.com/kristapsdz/lowdown/blob/master/document.c).
It is preceded by meta-data parsing, if applicable, which occurs before
document parsing but after the BOM.

Document parsing is the cruddiest part of the imported code, although
I've made some efforts to clean it up whenever I touch it.  *lowdown*
parses recursively, building the output document bottom-up.  It looks
something like this:

1. Begin parsing a component.
2. Parse out subcomponents, creating a recursive step.
3. Render the component, pasting in the subcomponents' renderered output.

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

Both of these subcomponents would be appended into a buffer, which would
then be passed back into the subsection parser.  It would paste the
buffer into `<h2>` blocks (in HTML5) or a `.SH` block (troff outputs).

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

### Problems

**Warning: here be dragons.**

The **-Tms** and **-Tman** output modes do not produce clean output due
to the white-space issue.  To fix this, I'll need to create a proper AST
of the input before formatting output.  Why the original authors did not
do this is a mystery to me.

The white-space issue is illustrated by the following:

```markdown
Read this *[linky](link)* here.
```

Looks benign.  But in GNU extension mode, we want to put that link into
a hyperlink, which means using the line macro `pdfhref`, as in

```roff
.pdfhref W -D link linky
```

The first problem is that the italics are normally rendered using the
roff `\fI` and `\fP` mode; which, if used, wouldn't render properly:

```roff
Read this 
\fI
.pdfhref W -D link linky
\fP here.
```

Since the `\fP` is a control character, it's stripped and the line is
considered verbatim (leading whitespace).

To avoid all of this, I force inline formatters to recognise that the
contained text is a macro and adjust accordingly.  Now, this becomes:

```roff
Read this
.I
.pdfhref W -D link linky
.R
here.
```

There's still a problem.  What if we have abutting text?

```markdown
Read this "[linky](link)".
```

The period will be on its own line.  To make up for this, I recognise
(for the time being, only trailing) abutting text and adjust horizontal
space manually:

```roff
Read this "
.I
.pdfhref W -D link linky
.R
\h[-0.4]".
```

This works, but not always.  For example, if the link occurs at the end
of a sentence, there's no keep between the link and the abutting text.
If the link is in italics and the subsequent text is not --- kerning
issues.  It's actually extremely ugly and I hate it.

The problem is the design of the parser: since content is parsed and
appended to buffers as it's parsed, there's no knowledge of
"look-ahead", and the output formatter can't properly know to do what
should be done:

```roff
Read this
.I
.pdfhref W -P \(dq -A \(dq. -D link linky
.R
```
