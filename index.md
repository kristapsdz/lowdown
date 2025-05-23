title: lowdown --- simple markdown translator
date: 2025-04-20
author: Kristaps Dzonsons

# [%title]

*lowdown* is a Markdown translator producing HTML5, *roff* documents in
the **ms** and **man** formats, LaTeX, gemini ("gemtext"), OpenDocument,
and ANSI/UTF8 terminal output.
The [open source](http://opensource.org/licenses/ISC) C source code has
no dependencies.

The tools are documented in
[lowdown(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html) and
[lowdown-diff(1)](https://kristaps.bsd.lv/lowdown/lowdown-diff.1.html),
the language in
[lowdown(5)](https://kristaps.bsd.lv/lowdown/lowdown.5.html), and the
library interface in
[lowdown(3)](https://kristaps.bsd.lv/lowdown/lowdown.3.html).

To get and use *lowdown*, check if it's available from your system's
package manager.  If not,
[download](https://kristaps.bsd.lv/lowdown/snapshots/lowdown.tar.gz),
[verify](https://kristaps.bsd.lv/lowdown/snapshots/lowdown.tar.gz.sha512),
and unpack the source.  Then build:

```sh
./configure
make
doas make install install_libs
```

On non-BSD systems, you may need to use `bmake` and `sudo`.

*lowdown* is a [BSD.lv](https://bsd.lv) project.  Its portability to
OpenBSD, NetBSD, FreeBSD, Mac OS X, Linux (glibc and musl), Solaris, and
IllumOS is enabled by
[oconfigure](https://github.com/kristapsdz/oconfigure).

One major difference between *lowdown* and other Markdown formatters it
that it internally converts to an AST instead of directly formatting
output.  This enables some semantic analysis of the content such as with
the [difference engine](https://kristaps.bsd.lv/lowdown/diff.html),
which shows the difference between two markdown trees in markdown.

As of version 2.0.0, *lowdown* uses
[semantic versioning](https://semver.org/) ("semver").  A major number
change indicates a change in the
[lowdown(3)](https://kristaps.bsd.lv/lowdown/lowdown.3.html) API, a
minor number indicates a change in functionality, and a patch number
indicates a bug-fix or change without functionality.  The version 2.0.0
reflects API changes in 1.4.0 that preceded.

## Output

*lowdown* produces HTML5 output with **-thtml**,
[LaTeX](https://www.latex-project.org/) documents with **-tlatex**,
"flat" OpenDocument (version 1.3) documents with **-tfodt**, 
[Gemini](https://gemini.circumlunar.space/docs/specification.html)
("gemtext") with **-tgemini**, *roff* documents with **-tms** and
**-tman**, or directly on ANSI terminals with **-tterm**.

The **-tlatex** and **-tms** are commonly used for PDF documents,
**-tfodt** for document processing, **-tman** for manpages, **-thtml**
or **-tgemini** for web, and **-tterm** for the command line.

By way of example: this page,
[index.md](https://kristaps.bsd.lv/lowdown/index.md), renders as
[index.latex.pdf](https://kristaps.bsd.lv/lowdown/index.latex.pdf)
with LaTeX (via **-tlatex**),
[index.mandoc.pdf](https://kristaps.bsd.lv/lowdown/index.mandoc.pdf)
with mandoc (via **-tman**), or
[index.nroff.pdf](https://kristaps.bsd.lv/lowdown/index.nroff.pdf)
with groff (via **-tms**).

> [![mandoc](screen-mandoc.thumb.jpg){width=30%}](screen-mandoc.png)
> [![term](screen-term.thumb.jpg){width=30%}](screen-term.png)
> [![groff](screen-groff.thumb.jpg){width=30%}](screen-groff.png)

> **-tman**
> **-tterm**
> **-tms**

Only **-thtml** and **-tlatex** allow images and equations, though
**-tms** has limited image support with encapsulated postscript.

## Input

Beyond traditional Markdown syntax support, *lowdown* supports the
following Markdown features and extensions:

- autolinking
- fenced code
- tables
- superscripts (traditional and GFM)
- footnotes
- disabled inline HTML
- "smart typography"
- metadata
- commonmark (**in progress**)
- definition lists
- extended attributes
- task lists
- admonitions
- templating

*lowdown* is fully compatible with the original Markdown syntax as
checked by the Markdown test suite, last version 1.0.3.  This suite is
available as part of the regression suite.

## Usage

Want to quickly review your Markdown in a terminal window?

```sh
lowdown -t term README.md | less -R
```

If you just want a straight-up HTML5 file, use standalone mode:

```sh
lowdown -s -o README.html README.md
```

This can use the document's meta-data to populate the title, CSS file,
and so on.

The roff **ms** macros work to make PS or PDF files.  The extra groff
arguments in the following are for UTF-8 processing (**-k**), tables
(**-t**), and clickable links and a table of contents (**-mspdf**).

```sh
lowdown -s -t ms README.md | pdfroff -itk -mspdf > README.pdf
```

The same can be effected with systems using
[mandoc](https://mandoc.bsd.lv) through the simpler **man** macros.

```sh
lowdown -s -tman README.md | mandoc -Tpdf > README.pdf
```

More support for PDF (and other print formats) is available with the
**-tlatex** output.

```sh
lowdown -s -tlatex README.md | pdflatex
```

Read [lowdown(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html) for
details on running the system.

*lowdown* is also available as a library,
[lowdown(3)](https://kristaps.bsd.lv/lowdown/lowdown.3.html).  This
is what's used internally by
[lowdown(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html) and
[lowdown-diff(1)](https://kristaps.bsd.lv/lowdown/lowdown-diff.1.html).

## Installation

Configure the system with the `configure` script, which may be passed
variables like `PREFIX` and `CC` that affect the build process.
See [oconfigure](https://github.com/kristapsdz/oconfigure) for possible
arguments.

```sh
./configure
```

If on a Mac OS X system, `./configure` may be passed
`SANDBOX_INIT_ERROR_IGNORE` set to `always` or `env`.  If `always`,
errors from the sandbox initialisation are ignored; if set to anything
else (like `env`), sandbox initialisation errors are ignored if the
user's environment contains `SANDBOX_INIT_ERROR_IGNORE`.  Passing this
on non-Mac OS X systems has no effect.

```sh
./configure SANDBOX_INIT_ERROR_IGNORE=always
```

This convoluted methodology is because Mac OS X sandboxes may not be
nested; and if *lowdown* were uesd within a sandbox, it would fail.

On Linux, the *lowdown* binary may be compiled with its shared library
instead of the default static library by passing `LINK_METHOD=shared` to
`./configure`.  Passing any other value, or omitting this entirely,
defaults to static linking.

```sh
./configure LINK_METHOD=shared
```

Once configured, build the system with `make` or, on non-BSD systems,
`bmake`.

```sh
make
```

To install the binaries (as root, in this example), run:

```sh
doas make install
```

On non-BSD systems, `sudo` might be required.  For libraries, you can
additionally run:

```sh
doas make install_libs
```

This may be split into `install_shared` and `install_static` for shared
and static libraries, respectively.

## Development

The code is neatly layed out and heavily documented internally.

First, start in
[library.c](https://github.com/kristapsdz/lowdown/blob/master/library.c).
(The [main.c](https://github.com/kristapsdz/lowdown/blob/master/main.c)
file is just a caller to the library interface.) Both the renderer
(which renders the parsed document contents in the output format) and
the document (which generates the parse AST) are initialised.

The parse is started in
[document.c](https://github.com/kristapsdz/lowdown/blob/master/document.c).
It is preceded by meta-data parsing, if applicable, which occurs before
document parsing but after the BOM.  The document is parsed into an AST
(abstract syntax tree) that describes the document as a tree of nodes,
each node corresponding an input token.  Once the entire tree has been
generated, the AST is passed into the front-end renderers, which
construct output depth-first.

There are a variety of renderers supported:
[html.c](https://github.com/kristapsdz/lowdown/blob/master/html.c) for
HTML5 output,
[nroff.c](https://github.com/kristapsdz/lowdown/blob/master/nroff.c) for
**-ms** and **-man** output,
[latex.c](https://github.com/kristapsdz/lowdown/blob/master/latex.c) for
LaTeX,
[gemini.c](https://github.com/kristapsdz/lowdown/blob/master/gemini.c) for
Gemini,
[odt.c](https://github.com/kristapsdz/lowdown/blob/master/odt.c) for
OpenDocument,
[term.c](https://github.com/kristapsdz/lowdown/blob/master/term.c)
for terminal output, and a debugging renderer
[tree.c](https://github.com/kristapsdz/lowdown/blob/master/tree.c).

### Parsing

Consider the following:

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
```

This tree would then be passed into a front-end, such as the HTML5
front-end with **-thtml**.  The nodes would be appended into a buffer,
which would then be passed back into the subsection parser.  It would
paste the buffer into `<h2>` blocks (in HTML5) or a `.SH` block (troff
outputs).

Finally, the subsection block would be fitted into whatever context it
was invoked within.

### Testing

The canonical Markdown tests are available as part of a regression
framework within the system.  Just use `make regress` to run these and
many other tests.

If you have [valgrind](https://valgrind.org) installed, `make valgrind`
will run all regression tests with all output modes and store any leaks
or bad behaviour.  These are output to the screen at the conclusion of
all tests.

The CI runner in *lowdown*'s GitHub repository runs both valgrind and
regular regression tests, the latter with the compiler's **-fsanitize**
options enabled, on each push.

I've extensively run [AFL](http://lcamtuf.coredump.cx/afl/) against the
compiled sources with no failures---definitely a credit to the
[hoedown](https://github.com/hoedown/hoedown) authors (and those from
whom they forked their own sources).  I'll also regularly run the system
through [valgrind](http://valgrind.org/), also without issue.  The
[afl/in](afl/in) directory contains a series of small input files that
may be used in longer AFL runs.

### How Can You Help?

Want to hack on *lowdown*?  Of course you do.

- Using a perfect hash (such as **gperf**) for entities.

- RTL languages are totally unrepresented.  It's difficult but not
impossible to refactor term.c to internally create a sequence of lines,
instead of a stream of contiguous output, allowing for a post-processing
RTL pass.

- There are bits and bobs remaining to be fixed or implemented.
You can always just search for `TODO`, `XXX`, or `FIXME` in the source
code.  This is your best bet.

- If you want a larger project, a **-tpdf** seems most interesting (and
quite difficult given that UTF-8 need be present).

- Implement a parser for mathematics such that `eqn` or similar may be
output.
