javascript: https://cdnjs.cloudflare.com/ajax/libs/highlight.js/9.12.0/highlight.min.js  
  diff.js  
author: Kristaps Dzonsons  
affiliation: BSD.lv
title: Lowdown Manpage Support
css: https://cdnjs.cloudflare.com/ajax/libs/highlight.js/9.12.0/styles/default.min.css  
  https://fonts.googleapis.com/css?family=Alegreya+Sans:400,400italic,500,700  
  diff.css  

# Introduction

Have you wanted to write a manpage for your library or program but are
put off by the complexity of the weird manpage language?  Read on.

For background, manpages ("manual pages") are Unix system documentation
usually accessed with [man(1)](https://man.openbsd.org/man).  Or, as is
the case for the **man** link just mentioned,
[man.cgi(8)](https://man.openbsd.org/man.cgi.8).  Manpages document
programming libraries, command-line utilities, device drivers,
formats---most anything available on a Unix system.  That numeric suffix
like "1" in [man(1)](https://man.openbsd.org/man) is the manpage
section, which groups what's being documented: 1 for programs, 2 for
system calls, 3 for functions, etc.[^suffixes]

[^suffixes]:
   This information is *also* in the manpage itself, which is the
   canonical section.  In practice, you can put manpages most anywhere
   where **man** will see them.  Personally, I think that having both a
   directory and in-band designation of manual section are confusing.

If you want to see real manpages, they're installed in
*/usr/share/man/man1*, */usr/local/man/man1*, or other directory
specific to the operating system and manual section (in this case,
programs, or "1"), where **man** picks them up.[^scan]  (On some
systems, the files will be compressed.)

[^scan]:
   It's not quite that simple: if you just copy a manpage into one of
   those directories, it may need to be indexed before being picked up
   by **man**.  The tool that does that is
   [makewhatis(8)](https://man.openbsd.org/makewhatis).

Manpages were traditionally written in
[man(7)](https://man.openbsd.org/man), but these days are usually in
[mdoc(7)](https://man.openbsd.org/mdoc).  Both of these are "macros" for
the underlying [roff(7)](https://man.openbsd.org/roff) text formatting
language.

For example, the [printf(3)](https://man.openbsd.org/printf.3) function
prototype is written in **mdoc** as follows:

```
.Sh SYNOPSIS
.Ft int
.Fn printf "const char *restrict format" "..."
```

Each part (function type, name, arguments) is labelled by a "macro" made
specifically to format those parts, like
[`Fn`](https://man.openbsd.org/mdoc.7#Fn).  If it were in the **man**
language, it would more like this:

```
.SH SYNOPSIS
\fIint\fR
.br \" line break after the type
.in +4
.ti -4
\fBprintf\fR(\fIconst char *restrict format\fR, \fI...\fR);
```

In the battle of **man** vs **mdoc**, authors either trade off
remembering macros with deep knowledge of how to format pages.  Which to me is
a pretty lose-lose.

That brings me to the point: one of the major complaints about writing
manpages is *complexity*:  authors must either navigate a maze of macros
and syntax rules in **mdoc**, or extensively (and usually
inconsistently) manage styling *and* syntax rules in **man**.

Unfortunately, the result is that many authors just don't write
manpages.  So... **why not Markdown**?  We know what manpages look
like---just not how to format them.  Isn't that what Markdown is there
for?  Why not just write a manpage-like Markdown and have the formatter
handle the rest?

With [lowdown](https://kristaps.bsd.lv/lowdown) as of version 3.0.0,
authors can use the **-tmdoc** or **-tman** output options, which
converts into into **mdoc** or **man**.  But now with the additional
**--roff-manpage** option, lowdown attempts to create well-formed,
semantic **mdoc** (or **man**) by understanding the context in which
statements are specified.

Quick example: [strlcpy.md](mdoc-strlcpy.md.txt) rendered as
[strlcpy.html](mdoc-strlcpy.html) and [grep.md](mdoc-grep.md.txt) rendered
as [grep.html](mdoc-grep.html).

How can you use it?

Just make your Markdown document look like your manpage.  Use a font
mode (emphasis or double emphasis) for all the content that's usually
rendered differently from normal text: program flags, functions,
arguments, variables, flag arguments, etc., etc.

More specifically....

# Layout

The general structure of a manpage should follow what's documented in
[mdoc(7)](https://man.openbsd.org/mdoc).  This is basically a restatement of
that document using Markdown syntax.

Before starting, classify the *section* of your manpage.  Utilities are
in section 1 or 8 (the latter more for system utilities, although the
difference is ambiguous), games in 6, system calls in 2, and programming
functions in 3.  For kernel work, section 4 is for device drivers and 9
for kernel functions.  File and wire formats are in section 5.  Anything
else goes into section 7.

Begin by setting the title and section.  This can either be directly in
the document or passed in as metadata.  If the title is omitted, it's
filled in from the first entry in the NAME section.  The default section
is 7.

```markdown
title: progname
section: 1

# NAME

progname - one line about what it does

# LIBRARY

(For sections 2, 3, and 9 only.)

# SYNOPSIS

`progname [-abc] [--baz foo] [FILE]`

(See per-section documentation, below.)

# DESCRIPTION

The *progname* utility processes with the following:

*-a*, *-b*, *-c*
: Three flags, one being *-a*, doing something.

*--baz foo*
: Long flag with argument *foo*.

*FILE*
: The optional file.

  This option's description has two paragraphs.

# CONTEXT

For section 9 functions only.

# IMPLEMENTATION NOTES

Not commonly used.

# RETURN VALUES

For sections 2, 3, and 9 function return values only.

# ENVIRONMENT

For sections 1, 6, 7, and 8 only.

# FILES

# EXIT STATUS

For sections 1, 6, and 8 only.

# EXAMPLES

# DIAGNOSTICS

For sections 1, 4, 6, 7, 8, and 9 printf/stderr messages only.

# ERRORS

For sections 2, 3, 4, and 9 errno settings only.

# SEE ALSO

foo(1), bar(2)

(See per-section documentation, below.)

# STANDARDS

# HISTORY

# AUTHORS

# CAVEATS

# BUGS

# SECURITY CONSIDERATIONS
```

Sections (meaning, what's after the header---not to be confused with a
manpage section like 1 or 3) without content should be omitted.  The
following show specific ways to fill in some special sections using
Markdown.

## NAME

The **NAME** section consists of one or more comma-separated components,
followed by a hyphen or dash, then a one-line description without trailing
punctuation.

For example, a manpage documenting [printf(3)](https://man.openbsd.org/printf.3)
functions might look like this:

```markdown
# NAME

printf, fprintf, sprintf, snprintf, asprintf, dprintf, vprintf, vfprintf,
vsprintf, vsnprintf, vasprintf, vdprintf -- formatted output conversion
```

A simple utility might look like this:

```markdown
# NAME

ls --- list directory contents

```

## SYNOPSIS

For utilities (sections 1, 6, and 8), use the conventional layout as
follows.  To prevent confusing the Markdown parser, either use a code
span or escape all Markdown-specific tokens.  I usually stick with code
spans.  Have one invocation per paragraph (that is, each separated by a
blank line).

```markdown
# SYNOPSIS

`apropos [-afk] [-C file] [-M path] [-m path] [-O outkey]
    [-S arch] [-s section] expression ...`

`addr2line [-a|--addresses]
    [-b bfdname|--target=bfdname]
    [-C|--demangle[=style]]
    [-e filename|--exe=filename]
    [-f|--functions] [-s|--basename]
    [-i|--inlines]
    [-j|--section=name]
    [-H|--help] [-V|--version]
    [addr addr ...]`
```

Any fonts, colours, or extra whitespace will be ignored.  It's ok to have smart
punctuation: it will all be recognised and translated accordingly.

For functions (sections 2, 3, and 9), write out your function prototypes as
they appear in code.  Either use a code block or escape special Markdown
characters.  Again, any fonts, colours, or extra whitespace will be elided.

```markdown
# SYNOPSIS

`#include <stdio.h>`

`int
snprintf(char *restrict str, size_t size,
    const char *restrict format, ...);`
```

Functions are recognised by having a parenthesis; inclusions by their
`#include` statement.  Both should have one invocation per paragraph.

To include complex types (like `struct`s), use a code block (here shown with a
space in the three consecutive backticks):

```markdown
# SYNOPSIS

`#include <openssl/asn1.h>`

` ``
typedef struct {
     int bitnum;
     const char *lname;
     const char *sname;
} BIT_STRING_BITNAME;
` ``

`int ASN1_BIT_STRING_num_asc(const char *name,
    BIT_STRING_BITNAME *table);`
```

Lastly, for variables, just put each on one line.  Nothing else should go into
the synopsis section.

```markdown
# SYNOPSIS

`#include <unistd.h>`

`extern char *optarg;`

`extern int opterr;`

`extern int optind;`

`extern int optopt;`

`extern int optreset;`

`int getopt(int argc, char * const *argv, const char *optstring);`
```

If unexpected conditions are encountered that don't look like a utility or
function synopsis, the block is formatted as-is.  If this happens to your
manpage, look it over to make sure that it's well-formed.

## SEE ALSO

References to other manpages.  List these first by section, then by alpha.
Manpage references must be in the format of `page(section)`.
Non-conforming paragraphs are left as-is.

```markdown
# SEE ALSO

foo(1), bar(3), baz(2)
```

## Others...

For all other sections, enclose any context-sensitive stuff in a font
mode.  What does that mean?  It's easy: when looking at a manpage with
[man(1)](https://man.openbsd.org/man), anything with a different font.

This includes, but is not limited to:

- other manpage references: `*foo(1)*`
- program flags and arguments: `*[-abcdef]*`, `*[-f baz]*`
- self-referencing program names: `the *foo* program...`
- variable names, types, and arguments: `*int*`, `*foo()*`, `*foo("bar")*`

The **--roff-manpage** parser will recognise these and try to format
them in a manpage-specific way.  For example, in a section 1 manpage,

```
*-C[num]*, *--context[=num]*
: Print *num* lines of leading and trailing context surrounding each
match.  The default is 2 and is equivalent to *-A 2 -B 2*.
whitespace may be given between the option and its argument.
```

In **-tmdoc** mode, this will render as:

```
.It Fl C Ns Oo Ar num Oc Ns No , Fl \-context Ns Oo Ar =num Oc 
Print 
.Ar num
lines of leading and trailing context surrounding each
match.  The default is 2 and is equivalent to 
.Fl A Ar 2
.Fl B Ar 2 .
```

Ew.

# Wrapping up

This is an aggressive feature and it will make mistakes.  The goal is
to have output be meaningful and to pass
[mandoc(1)](https://man.openbsd.org/mandoc)'s linter.  For the time
being, there are some missing bits:

- equal signs at the start of arguments shouldn't be rendered as part of
  the argument, and sometimes they are
- in function manpages, provide a way (maybe a hint from the type of
  font mode?) to differentiate variable types and names
    - as an aside, differentiating variable versus function types is
      basically not possible from Markdown as-is
- new sentence, new line
- instances of trailing white-space

If you're going to use this feature, please keep a sharp eye out for
ignored formatting: this means that the formatter gave up.  If it does,
or if things just don't render right, please raise an issue on
[GitHub](https://github.com/kristapsdz/lowdown) with the
offending bits.  Or just mail me, but better the GitHub, as I tend to
let emails ferment.

If you're automatically generating a manpage, say from within a
*Makefile*, make sure to install the generated manual in the right
place, and it should be available for your users!
