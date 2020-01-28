title: lowdown --- simple markdown translator
author: Kristaps Dzonsons

# [%title]

*lowdown* is a Markdown translator producing HTML5 and *roff* documents
in the **ms** and **man** formats.  It's just clean, secure, [open
source](http://opensource.org/licenses/ISC) C code with no dependencies.
Its canonical documentation is
[lowdown(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html) for the
formatter, [lowdown(5)](https://kristaps.bsd.lv/lowdown/lowdown.5.html)
for the syntax, and the library interface at
[lowdown(3)](https://kristaps.bsd.lv/lowdown/lowdown.3.html).

*lowdown* started as a fork of
[hoedown](https://github.com/hoedown/hoedown) to add sandboxing
([pledge(2)](https://man.openbsd.org/pledge),
[capsicum(4)](https://www.freebsd.org/cgi/man.cgi?query=capsicum&sektion=4),
or
[sandbox\_init(3)](https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/sandbox_init.3.html))
and *roff* output to securely generate PDFs on
[OpenBSD](https://www.openbsd.org) with just
[mandoc(1)](https://man.openbsd.org/mandoc).

It can also be used to show the difference between two Markdown
documents with *lowdown-diff*, documented as the [lowdown diffing
engine](https://kristaps.bsd.lv/lowdown/diff.html).  This uses a
tree-based difference algorithm to show semantically-valid Markdown
differences.

To get *lowdown*, just
[download](https://kristaps.bsd.lv/lowdown/snapshots/lowdown.tar.gz),
[verify](https://kristaps.bsd.lv/lowdown/snapshots/lowdown.tar.gz.sha512),
unpack, run `./configure`, then run `doas make install` (or use `sudo`).
*lowdown* is a [BSD.lv](https://bsd.lv) project.
[Homebrew](https://brew.sh) users can use BSD.lv's
[tap](https://github.com/kristapsdz/homebrew-repo).

For an argument against using Markdown at all, read [Ingo's comments on
Markdown](https://undeadly.org/cgi?action=article&sid=20170304230520).

## Output

Of course, *lowdown* supports the usual HTML output with **-Thtml**.
Specifically, it produces HTML5 in XML mode.  You can use *lowdown* to
create either a snippet or standalone HTML5 document.

It also supports outputting to the **ms** macros (**-Tms**), originally
implemented for the *roff* typesetting package of Version 7 AT&T UNIX.
This way, you can have elegant PDF and PS output by using any modern
*troff* system such as [groff(1)](https://www.gnu.org/s/groff).
Furthermore, it supports the **man** macros (**-Tman**), also from
Version 7 AT&T UNIX[^nomanpages].  Beyond the usual *troff* systems,
this is also supported by [mandoc](https://mdocml.bsd.lv).

By way of example: this page,
[index.md](https://kristaps.bsd.lv/lowdown/index.md), renders as
[index.pdf](https://kristaps.bsd.lv/lowdown/index.pdf) from
[groff(1)](https://www.gnu.org/s/groff/) and using **-Tms** output.
Another example is the GitHub
[README.md](https://kristaps.bsd.lv/lowdown/README.md) rendered as
[README.html](https://kristaps.bsd.lv/lowdown/README.html) or
[README.pdf](https://kristaps.bsd.lv/lowdown/README.pdf).

[^nomanpages]:
    You may be tempted to write [manpages](https://man.openbsd.org)
    in Markdown, but please don't: use
    [mdoc(7)](https://man.openbsd.org/mdoc), instead --- it's built
    for that purpose!  The **man** output is for technical
    documentation only (section 7).

*lowdown* also supports ANSI-compatible UTF-8 terminals with **-Tterm**.
This renders stylised Markdown-looking output for easy reading.  It's
inspired by [glow](https://github.com/charmbracelet/glow).
You can also use **-Tms** and **-Tman** with the formatter cooperation.

> [![mandoc](screen-mandoc.thumb.jpg)](screen-mandoc.png)
> [![term](screen-term.thumb.jpg)](screen-term.png)
> [![groff](screen-groff.thumb.jpg)](screen-groff.png)

> **-Tman**
> **-Tterm**
> **-Tms**

Only **-Thtml** allows images and equations, though **-Tms** has limited
image support with encapsulated postscript.

## Input

Beyond the basic Markdown syntax support, *lowdown* supports the
following Markdown features and extensions:

- autolinking
- fenced code
- tables
- superscripts
- footnotes
- disabled inline HTML
- "smart typography"
- metadata
- commonmark (**in progress**)

## Examples

I usually use *lowdown* when writing
[sblg](https://kristaps.bsd.lv/sblg) articles when I'm too lazy to
write in proper HTML5.
([sblg](https://kristaps.bsd.lv/sblg) is a simple tool for knitting
together blog articles into a blog feed.)
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
will omit equations and only use local PS/EPS images in **-Tms** mode.
The extra groff arguments in the following invocation are for UTF-8
processing (**-k** and **-Kutf8**), tables (**-t**), and clickable links
and a table of contents (**-mspdf**).

If outputting PDF, use the pdfroff script instead of **-Tpdf** output.
This allows image generation to work properly.  If not, a blank square
will be output in places of your images.

```sh
lowdown -sTms README.md | groff -kti -Kutf8 -mspdf > README.ps
lowdown -sTms README.md | pdfroff -tik -Kutf8 -mspdf > README.pdf
```

The same can be effected with systems using
[mandoc](https://mdocml.bsd.lv):

```sh
lowdown -s -Tman README.md | mandoc -Tps > README.ps
lowdown -s -Tman README.md | mandoc -Tpdf > README.pdf
```

For terminal output, troff or mandoc may be used in their respective
**-Tutf8** or **-Tascii** modes.  Alternatively, *lowdown* can render
directly to ANSI terminals with UTF-8 support:

```sh
lowdown -Tterm README.md | less -R
```

Read [lowdown(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html) for
details on running the system.

## Library

*lowdown* is also available as a library,
[lowdown(3)](https://kristaps.bsd.lv/lowdown/lowdown.3.html).  This
effectively wraps around everything invoked by
[lowdown(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html), so it's
basically the same but... a library.

## Testing

The canonical Markdown test, such as found in the original
[hoedown](https://github.com/hoedown/hoedown) sources, will not
currently work with *lowdown* as-is because of the default options such
as smart typography.

I've extensively run [AFL](http://lcamtuf.coredump.cx/afl/) against the
compiled sources with no failures --- definitely a credit to
the [hoedown](https://github.com/hoedown/hoedown) authors (and those
from who they forked their own sources).  I'll also regularly run the system
through [valgrind](http://valgrind.org/), also without issue.

## Code layout

The code is neatly layed out and heavily documented internally.

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

There are four renderers supported:
[html.c](https://github.com/kristapsdz/lowdown/blob/master/html.c) for
HTML5 output,
[nroff.c](https://github.com/kristapsdz/lowdown/blob/master/nroff.c) for
**-ms** and **-man** output,
[term.c](https://github.com/kristapsdz/lowdown/blob/master/term.c)
for terminal output, and a debugging renderer
[tree.c](https://github.com/kristapsdz/lowdown/blob/master/tree.c).

## Example

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

## Compatibility

*lowdown* is fully compatible with the original Markdown syntax as checked by
the Markdown test suite, last version 1.0.3.

To run the Markdown suite, use the following command-line invocation:

```
lowdown --parse-no-metadata \
	--parse-no-autolink \
	--parse-no-cmark \
	--out-no-smarty \
	--html-no-head-ids \
	--html-no-skiphtml \
	--html-no-escapehtml \
	--html-no-owasp \
	--html-no-num-ent 
```

This is effectively "compatibility" mode with the earlier Markdown.

## How Can You Help?

Want to hack on *lowdown*?  Of course you do.

There are lots of bits and bobs remaining to be fixed or implemented.
You can always just search for `TODO`, `XXX`, or `FIXME` in the source
code.  This is your best bet.

There are some larger known issues, mostly in PDF (**-Tms** and
**-Tman**) output.

- There needs to be logic to handle when a link is the first or last
component of a font change.  For example, \*\[foo\](...)\* will put
the font markers on different lines.

- Footnotes in **-Tms** with groff extensions should use pdfmark to link
to and from the definition.

- Tables in **-Tterm**.

Lastly, you can always just fuzz the system through your fuzzer of
choice.  For just the parser, use the **-Tnull** output channel.

If you want a larger project, a **-Tpdf** seems most interesting (and
quite difficult given that UTF-8 need be present).  Another project that
has been implemented elsewhere is a parser for mathematics such that
`eqn` or similar may be output.
