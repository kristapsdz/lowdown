javascript: https://cdnjs.cloudflare.com/ajax/libs/highlight.js/9.12.0/highlight.min.js
  diff.js
author: Kristaps Dzonsons
title: Lowdown Diffing Engine
css: https://cdnjs.cloudflare.com/ajax/libs/highlight.js/9.12.0/styles/default.min.css
  https://fonts.googleapis.com/css?family=Alegreya+Sans:400,400italic,500,700
  diff.css

# Lowdown Diffing Engine

In this paper, I briefly describe the "diff" engine used in
[lowdown-diff(1)](lowdown.1.html).
The work is motivated by the need to provide formatted output describing
the difference between two documents---specifically, formatted PDF via
the **-Tms** output.

## Introduction

Let two source files, *old.md* and *new.md*, refer to the old and new
versions of a file respectively.  The goal is to establish the changes
between these snippets in formatted output.  Let's begin with the old
version, *old.md*.

```markdown
*Lorem* ipsum dolor sit amet, consectetur adipiscing elit, sed do
eiusmod tempor incididunt ut [labore](index.html) et dolore magna
aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco
laboris nisi ut _aliquip_ ex ea commodo consequat. Duis aute irure dolor
in reprehenderit...
```

In the new version, *new.md*, we add some more links and styles.

```markdown
*Lorem* ipsum dolor sit amet, consectetur adipiscing elit, sed do
eiusmod tempor incididunt ut [labore](index.html) et dolore [magna
aliqua](index.html). Ut enim ad minim veniam, quis nostrud exercitation
ullamco laboris nisi ut _aliquip_ ex ea commodo consequat. Duis *aute
irure* dolor in reprehenderit...
```

The most simple way of viewing changes is with the venerable
[diff(1)](https://man.openbsd.org/diff.1) utility.  However, this will
only reflect changes in the input document---not the formatted output.

```diff
--- old.md      Tue Oct 17 11:25:01 2017
+++ new.md      Tue Oct 17 11:25:01 2017
@@ -1,5 +1,5 @@
 *Lorem* ipsum dolor sit amet, consectetur adipiscing elit, sed do
-eiusmod tempor incididunt ut [labore](index.html) et dolore magna
-aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco
-laboris nisi ut _aliquip_ ex ea commodo consequat. Duis aute irure dolor
-in reprehenderit...
+eiusmod tempor incididunt ut [labore](index.html) et dolore [magna
+aliqua](index.html). Ut enim ad minim veniam, quis nostrud exercitation
+ullamco laboris nisi ut _aliquip_ ex ea commodo consequat. Duis *aute
+irure* dolor in reprehenderit...
```

Not very helpful for any but source-level change investigation.  And
given that Markdown doesn't encourage the usual "new sentence, new line"
of some languages (like [mdoc(7)](https://man.openbsd.org/mdoc.7)), even
this level of change analysis is difficult: a single change might affect
multiple re-wrapped lines.

A similar possibility is to use
[wdiff(1)](https://www.gnu.org/software/wdiff/), which produces a set of
word-by-word differences.

```markdown
*Lorem* ipsum dolor sit amet, consectetur adipiscing elit, sed do
eiusmod tempor incididunt ut [labore](index.html) et dolore [-magna
aliqua.-] {+[magna
aliqua](index.html).+} Ut enim ad minim veniam, quis nostrud exercitation
ullamco laboris nisi ut _aliquip_ ex ea commodo consequat. Duis [-aute irure-] {+*aute
irure*+} dolor in reprehenderit...
```

We could then extend the Markdown language to accept the insertion and
deletion operations and let the output flow from there.

Unfortunately, doing so entails extending a language already prone to
extension and non-standardisation.  More unfortunately, a word-based
diff will not be sensitive to the Markdown language itself, and in
establishing context-free sequences of similar words, will overrun block
and span element boundaries.

On the other end of the spectrum, we can consider difference tools for
the output media.

We can directly analyse PDF output using (for example) the
[poppler](https://poppler.freedesktop.org/) tools, which would extract
text, then examine the output with a linear difference engine such as
[Diff, Match,
Patch](https://code.google.com/archive/p/google-diff-match-patch/).
This is not an optimal solution because, as with the word diff above, it
only compares words and cannot distinguish semantic artefacts such as in
italics, links, code blocks, and so on.

There are even more [HTML diff](https://www.w3.org/wiki/HtmlDiff) tools
available, so it's tempting to use one of these tools to produce an HTML
file consisting of differences, then further use a converter like
[wkhtmltopdf](https://wkhtmltopdf.org/) to generate PDFs.

Since the HTML difference engines often respect the structure of HTML,
this is much more optimal in handling semantic difference.  However,
re-structuring the difference does not easily produce a document of the
same style or readability as the PDFs themselves.

The most elegant (and reliable) solution is to attack the problem from
the language-level itself.  Since the [lowdown(3)](lowdown.3.html)
library is able to produce a full parse tree for analysis, it stands to
reason, given the wealth of literature on tree differences (instead of
the usual linear difference, as in the case of
[diff(1)](https://man.openbsd.org/diff.1) and friends), we can work
within the language to produce differences.

## Algorithm
