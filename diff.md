javascript: https://cdnjs.cloudflare.com/ajax/libs/highlight.js/9.12.0/highlight.min.js  
  diff.js  
author: Kristaps Dzonsons  
title: Lowdown Diffing Engine  
css: https://cdnjs.cloudflare.com/ajax/libs/highlight.js/9.12.0/styles/default.min.css  
  https://fonts.googleapis.com/css?family=Alegreya+Sans:400,400italic,500,700  
  diff.css  

# Lowdown Diffing Engine

In this paper, I briefly describe the "diff" engine used in
[lowdown-diff(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html) tool
in [lowdown](https://kristaps.bsd.lv/lowdown/index.html).  The work is
motivated by the need to provide formatted output describing the
difference between two documents---specifically, formatted PDF via the
**-Tms** output.

This documents a work in progress.  The source is documented fully in
[diff.c](https://github.com/kristapsdz/lowdown/blob/master/diff.c).
This paper itself is available as
[diff.md](https://github.com/kristapsdz/lowdown/blob/master/diff.md).
Please direct comments to me by e-mail or just use the GitHub interface.

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

In the new version, *new.md*, I add some more links and styles.

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
aliqua.-] {+[magna aliqua](index.html).+} Ut enim ad minim veniam, quis
nostrud exercitation ullamco laboris nisi ut _aliquip_ ex ea commodo
consequat. Duis [-aute irure-] {+*aute irure*+} dolor in
reprehenderit...
```

I could then extend the Markdown language to accept the insertion and
deletion operations and let the output flow from there.

Unfortunately, doing so entails extending a language already prone to
extension and non-standardisation.  More unfortunately, a word-based
diff will not be sensitive to the Markdown language itself, and in
establishing context-free sequences of similar words, will overrun block
and span element boundaries.

On the other end of the spectrum, I can consider difference tools for
the output media.

I can directly analyse PDF output using (for example) the
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
the language-level itself.  Since the
[lowdown(3)](https://kristaps.bsd.lv/lowdown/lowdown.3.html)
library is able to produce a full parse tree for analysis, it stands to
reason, given the wealth of literature on tree differences (instead of
the usual linear difference, as in the case of
[diff(1)](https://man.openbsd.org/diff.1) and friends), I can work
within the language to produce differences.

## Algorithm

The algorithm is in effect an ordered tree diff.  I began with
well-studied algorithms for a well-studied problem: XML tree
differences.  (The HTML difference tools described above inherit from
those algorithms.) For an overview of algorithms, see Change Detection
in XML Trees: a Survey[^Peters2005].

I base the [lowdown-diff(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html) algorithm off of Detecting
Changes in XML Documents[^Cobena2002].

The reason for this choice instead of another is primarily the ease in
implementation.  Moreover, since the programmatic output of the
algorithm is a generic AST, it's feasible to re-implement the algorithm
in different ways, or augment it at a later date.

The BULD algorithm described in this paper is straightforward:

1. Annotate each node in the parse tree with a hash of the subtree
   rooted at the node, inclusive.

2. Annotate each node with a weight corresponding to the subtree rooted
   at thd node.

3. Enqueue the new document's root node in a priority queue ordered by
   weight. Then, while the priority queue is non-empty:

    1. Pop the first node of the priority queue.
    2. Look for candidates in the old document whose hash matches the
       popped node's hash.
    3. Choose an optimal candidate and mark it as matched.
    4. If the no candidates were found, enqueue the node's children into
       the priority queue.
    5. A a candidate was selected, mark all of its subtree nodes as
       matching the corresponding nodes in the old tree ("propogate
       down"), then mark ancestor nodes similarly ("propogate up").

4. Run an optimisation phase over the new document's root node.

5. Step through both trees and create a new tree with nodes cloned from
   both and marked as inserted or deleted.

My implementation changes or extends the BULD algorithm in several small
ways, described in the per-step documentation below.

### Annotation

Each node in the tree is annotated with a hash and a weight.  The hash,
MD5, is computed in all data concerning a node.  For example, normal
text nodes (`LOWDOWN_NORMAL_TEXT`) have the hash produced from the
enclosed text.  Autolinks (`LOWDOWN_LINK_AUTO`) use the link text, link,
and type.

There are some nodes whose data is not hashed.  For example, list
numbers: since these may change when nodes are moved, the numbers are
not part of the hash.  In general, all volatile information that may be
inferred from the document structure (column number, list item number,
footnote number, etc.) is disregarded.

Non-leaf nodes compute their hashes from the node type and the hashes of
all of their children.  Thus, this step is a bottom-up search.

Node weight is computed exactly as noted in the paper.

### Optimal candidacy

A node's candidate in the old tree is one whose hash matches.  In most
documents, there are many candidates for certain types of nodes.
(Usually text nodes.)

Candidate optimality is computed by looking at the number of parent
nodes that match on both sides.  The number of parents to consider is
noted in the next sub-section.  The distance climbed is bounded by the
weight of the sub-tree as defined in the paper.

In the event of similar optimality, the node "closest" to the current
node is chosen.  Proximity is defined by the node identifier, which is
its prefix order in the parse tree.

### "Propogate up"

When propogating a match upward, the distance upward is bound depending
on the matched sub-tree as defined in the paper.  This makes it so that
"small" similarities (such as text) don't erroneously match two larger
sub-trees that are otherwise different.  Upward matches occur while the
nodes' labels are the same, including attributes (e.g., link text).

I did modify the algorithm to propogate upward "for free" through
similar singleton nodes, even if it means going beyond the maximum
number allowed by the sub-tree weight.

### Optimisation

The [lowdown-diff(1)](https://kristaps.bsd.lv/lowdown/lowdown.1.html) algorithm has only one
optimisation that extends the "propogate up" algorithm.  In the upward
propogation, the weight of any given sub-tree is used to compute how
high a match will propogate.  However, I add an optimisation that looks
at the cumulative weight of matching children.

This works well for Markdown documents, which are generally quite
shallow and text-heavy.

For each unmatched non-terminal node with at least one
matched child, the weights of all matched children with the same parents
(where the parent node is equal in label and attributes to the examined
node) are computed.  If any given parent of the matched children has
greater than 50% of the possible weight, it is matched.

This is performed bottom-up.

### Merging

The merging phase, which is not described in the paper, is very
straightforward.  It uses a recursive merge algorithm starting at the
root node of the new tree and the root node of the old tree.

1. The invariant is that the current node is matched by the corresonding
   node in the old tree.
2. First, step through child nodes in the old tree.  Mark as deleted all
   nodes not being matched to the new tree.
3. Next, step through child nodes in the new tree.  Mark as inserted all
   nodes not having a match in the old tree.
4. Now, starting with the first node in the new tree having a match in
   the old tree, search for that match in the list of old tree children.
5. If found, mark as deleted all previous old tree nodes up until the
   match.  Then re-run the merge algorithm starting at the matching
   child nodes.
6. If not found, mark the new node as inserted and return to (2).
7. Continue (after the recursive step) by moving after both matching
   nodes and returning to (2).

## API

The result of the algorithm is a new tree marked with insertions and
deletions.  These are specifically noted with the `LOWDOWN_CHNG_INSERT`
and `LOWDOWN_CHNG_DELETE` variables.

The algorithm may be run with the `lowdown_diff()` function, which
produces the merged tree.

A set of convenience functions, `lowdown_buf_diff()` and
`lowdown_file_diff()`, also provide this functionality.

## Future work

There are many possible improvements to the algorithm.

Foremost is the issue of normal text nodes.  There should be a process
first that merges consecutive text nodes.  This happens, for example,
when the `w` character is encountered at any time and might signify a
link.  The parsing algorithm will start a new text node at each such
character. 

With longer text nodes, the merging algorithm can recognise adjacent
text and use an LCS algorithm within the text instead of marking the
entire node as being inserted or deleted.  This would allow for sparse
changes within a long block of text to be properly handled.

The merging algorithm can also take advantage of LCS when ordering the
output of inserted and deleted components.  Right now, the algorithm is
simple in the sense of stopping at the earliest matched node without
considering subsequences.

[^Cobena2002]: [Detecting Changes in XML
    Documents](https://www.cs.rutgers.edu/~amelie/papers/2002/diff.pdf)
    (2002), Gregory Cobena, Serge Abiteboul, Amelie Marian.

[^Peters2005]: [Change Detection in XML Trees: a
    Survey](http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.73.8912&rep=rep1&type=pdf)
    (2005), Luuk Peters.
