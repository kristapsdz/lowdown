javascript: https://cdnjs.cloudflare.com/ajax/libs/highlight.js/9.12.0/highlight.min.js  
  diff.js  
author: Kristaps Dzonsons  
affiliation: BSD.lv
title: Lowdown Manpage Support
css: https://cdnjs.cloudflare.com/ajax/libs/highlight.js/9.12.0/styles/default.min.css  
  https://fonts.googleapis.com/css?family=Alegreya+Sans:400,400italic,500,700  
  diff.css  

# Introduction

Manpages ("manual pages") are system documentation usually presented by the
[man(1)](https://man.openbsd.org/man) utility.  Or, as is the case for the link
just used, an HTML rendering of the same.  Manpages cover programming libraries
(usually for the C and Perl programming language), command-line utilities,
device drivers, formats---most anything available on a modern Unix system.

Manpages were traditionally written in the
[roff(7)](https://man.openbsd.org/roff) language, usually using the
[man(7)](https://man.openbsd.org/man) macro package.
(Much like LaTeX and TeX, roff supports macro packages that have specific
functionality above the base language.)
After 1990 or so, authors switched by and large to Cynthia Livingston's
[mdoc(7)](https://man.openbsd.org/mdoc) macro set.

The **mdoc** macro set is the more modern choice, and favours a semantic mark-up
approach that allows authors to focus on their content, and the renderer to
adhere to output conventions.  It also allows indexers like
[apropos(1)](https://man.openbsd.org/apropos) and
[whatis(1)](https://man.openbsd.org/whatis) to store more contextual
information that can be looked up by users, such as all manpages with
function arguments matching a pattern.

On the other hand, **man** is simpler with a more syntactic approach, requiring
authors to format their manpages much more extensively.

For example, the **mdoc** way of decorating a function synopsis:

```
.Sh SYNOPSIS
.Ft int
.Fn sprintf "char *restrict str" "const char *restrict format" ...
```

The same but using **man**):

```
.SH SYNOPSIS
\fIint\fR
.sp
\fBsprintf\fR(\fIchar *restrict str\fR, \fIconst char *restrict format\fR,
    \fI...\fR);
```

These days, manpages are a mix of both---if they exist at all!

One of the major complaints about authoring Unix manpages is the complexity of
the **roff** language, using either macro package.  Authors must either navigate
a maze of macros and syntax rules in **mdoc**, or extensively (and usually
inconsistently) manage styling in **man**.

**So... why not Markdown?**

```markdown
# SYNOPSIS

*int*
**sprintf**(*char \* restrict str*, *const char \* restrict format*,
    *\.\.\.*);
```

With [lowdown](https://kristaps.bsd.lv/lowdown), authors could translate the
above into a **man** document using the **-tman** output option.

As of version 3.0.0, authors can also use the **-tmdoc** output option, which
attempts to convert into **mdoc**.  Since **mdoc** has a richer set of macros, but
is more idiomatic in where those macros are used, this requires authors of
Markdown manpages to be a little bit more attentive to the styling of their
Markdown manual inputs.

# Layout

The general structure of a manpage should follow what's documented in
[mdoc(7)](https://man.openbsd.org/mdoc).  This is basically a restatement of
that document using Markdown syntax.

Before starting, you'll need to classify the *section* of your manpage.
Utilities are in section 1 or 8 (the latter more for system utilities, although
the difference is ambiguous), games in 6, system calls in 2, and programming
functions in 3.  For kernel work, section 4 is for device drivers and 9 for
kernel functions.  File and wire formats are in section 5.  Anything else goes
into section 7.

Begin by setting the title and section.  This can either be directly in the
document or passed in as meta-data.  If the title is omitted, it's filled in
from the first entry in the NAME section.  The default section is 7.

```markdown
title: progname
section: 1

# NAME

progname - one line about what it does

# LIBRARY

(For sections 2, 3, and 9 only.)

# SYNOPSIS

progname \[-abc] \[FILE]

(See per-section documentation, below.)

# DESCRIPTION

The **progname** utility processes files...

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

*foo(1)*, *bar(2)*

(See per-section documentation, below.)

# STANDARDS

# HISTORY

# AUTHORS

# CAVEATS

# BUGS

# SECURITY CONSIDERATIONS
```

Sections without content should be left blank.

The following sections show specific ways to fill in some special sections.

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

git --- the stupid content tracker
```

## SYNOPSIS

Manpages recognised by lowdown's **-tmdoc** are either programming functions
(manual sections 2, 3, and 9) or programs (sections 1, 6, and 8).  This is
usually the trickiest section to get right.

For utilities, use the conventional layout as follows.  To prevent confusing the
Markdown parser, either use a code span or escape all Markdown-specific tokens.
I'll show both, for comparison.  Have one invocation per paragraph (that is,
each separated by a blank line).

```markdown
# SYNOPSIS

apropos \[-afk] \[-C file] \[-M path] \[-m path] \[-O outkey]
    \[-S arch] \[-s section] expression \.\.\.

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

\#include \<stdio.h>

int
sprintf(char \* restrict str, const char \* restrict format, \.\.\.);

`int
snprintf(char *restrict str, size_t size, const char *restrict format, ...);`
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

`int ASN1_BIT_STRING_num_asc(const char *name, BIT_STRING_BITNAME *table);`
```

Lastly, for variables, just put each on one line.  Nothing else should go into
the synopsis section.

```markdown
# SYNOPSIS

\#include \<unistd.h>

extern char *optarg;

extern int opterr;

extern int optind;

extern int optopt;

extern int optreset;

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


