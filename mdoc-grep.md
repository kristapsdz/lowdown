section: 1

# NAME

grep, egrep, fgrep, zgrep, zegrep, zfgrep -- file pattern searcher

# SYNOPSIS

`grep [-abcEFGHhIiLlnoqRsUVvwxZ] [-A num] [-B num] [-C[num]] [-e pattern]
  [-f file] [-m num] [--binary-files=value] [--context[=num]]
  [--label=name] [--line-buffered] [--null] [pattern] [file ...]`

# DESCRIPTION

The grep utility searches any given input files, selecting lines that
match one or more patterns.  By default, a pattern matches an input line
if the regular expression (RE) in the pattern matches the input line
without its trailing newline.  An empty expression matches every line.
Each input line that matches at least one of the patterns is written to
the standard output.  If no file arguments are specified, the standard
input is used.

grep is used for simple patterns and basic regular expressions (BREs);
egrep can handle extended regular expressions (EREs).  See re\_format(7)
for more information on regular expressions.  fgrep is quicker than both
grep and egrep, but can only handle fixed patterns (i.e. it does not
interpret regular expressions).  Patterns may consist of one or more
lines, allowing any of the pattern lines to match a portion of the input.

*zgrep*, *zegrep*, and *zfgrep* act like *grep*, *egrep*, and *fgrep*,
respectively, but accept input files compressed with the compress(1) or
gzip(1) compression utilities.

The following options are available:

*-A num*
: Print *num* lines of trailing context after each match.  See also the
*-B* and *-C* options.

*-a*
: Treat all files as ASCII text.  Normally grep will simply print "Binary file
... matches" if files contain binary characters.  Use of this option forces
grep to output lines matching the specified pattern.

*-B num*
: Print *num* lines of leading context before each match.  See also the
*-A* and *-C* options.

*-b*
: Each output line is preceded by its position (in bytes) in the file.  If
option *-o* is also specified, the position of the matched pattern is
displayed.

*-C[num]*, *--context[=num]*
: Print *num* lines of leading and trailing context surrounding each
match.  The default is 2 and is equivalent to *-A 2 -B 2*.  Note: no
whitespace may be given between the option and its argument.

*-c*
: Only a count of selected lines is written to standard output.

*-E*
: Interpret pattern as an extended regular expression (i.e. force
        grep to behave as egrep).

*-e pattern*
: Specify a pattern used during the search of the input: an input line
is selected if it matches any of the specified patterns.  This option is
most useful when multiple *-e* options are used to specify multiple
patterns, or when a pattern begins with a dash (‘-’).

*-F*
: Interpret pattern as a set of fixed strings (i.e. force grep to behave
as fgrep).

*-f file*
: Read one or more newline separated patterns from *file*.  Empty pattern
lines match every input line.  Newlines are not considered part of a
pattern.  If *file* is empty, nothing is matched.

*-G*
: Interpret pattern as a basic regular expression (i.e. force grep to
behave as traditional grep).

*-H*
: Always print filename headers (i.e. filenames) with output lines.

*-h*
: Never print filename headers (i.e. filenames) with output lines.

*-I*
: Ignore binary files.

*-i*
: Perform case insensitive matching.  By default, grep is case sensitive.

*-L*
: Only the names of files not containing selected lines are written to
standard output.  Pathnames are listed once per file searched.  If the
standard input is searched, the string “(standard input)” is written.

*-l*
: Only the names of files containing selected lines are written to
standard output.  grep will only search a file until a match has been
found, making searches potentially less expensive.  Pathnames are listed
once per file searched.  If the standard input is searched, the string
“(standard input)” is written.

*-m num*
: Stop after finding at least one match on num different lines.

*-n*
: Each output line is preceded by its relative line number in the file,
starting at line 1.  The line number counter is reset for each file
processed.  This option is ignored if *-c*, *-L*, *-l*, or *-q* is
specified.

*-o*
: Print each match, but only the match, not the entire line.

*-q*
: Quiet mode: suppress normal output.  grep will only search a file
until a match has been found, making searches potentially less
expensive.

*-R*
: Recursively search subdirectories listed.  If no file is given, grep
searches the current working directory.

*-s*
: Silent mode.  Nonexistent and unreadable files are ignored (i.e.
their error messages are suppressed).

*-U*
: Search binary files, but do not attempt to print them.

*-V*
: Display version information.  All other options are ignored.

*-v*
: Selected lines are those not matching any of the specified patterns.

*-w*
: The expression is searched for as a word (as if surrounded by
'[[:<:]]' and '[[:>:]]’; see re\_format(7)).

*-x*
: Only input lines selected against an entire fixed string or regular
expression are considered to be matching lines.

*-Z*
: Force grep to behave as zgrep.

*--binary-files=value*
: Controls searching and printing of binary files.  Options are
*binary*, the default: search binary files but do not print them;
*without-match*: do not search binary files; and *text*: treat all files
as text.

*--label=name*
: Print *name* instead of the filename before lines.

*--line-buffered*
: Force output to be line buffered.  By default, output is line buffered
when standard output is a terminal and block buffered otherwise.

*--null*
: Output a zero byte instead of the character that normally follows a
file name.  This option makes the output unambiguous, even in the
presence of file names containing unusual characters like newlines.
This is similar to the *-print0* primary in *find(1)*.

# EXIT STATUS

The *grep* utility exits with one of the following values:

0
: One or more lines were selected.

1
: No lines were selected.

\>1
: An error occurred.

# EXAMPLES

To find all occurrences of the word 'patricia' in a file:

```
$ grep 'patricia' myfile
```

To find all occurrences of the pattern '.Pp' at the beginning of a line:

```
$ grep '^\.Pp' myfile
```

The apostrophes ensure the entire expression is evaluated by *grep*
instead of by the user's shell.  The caret '^' matches the null string
at the beginning of a line, and the '\' escapes the '.', which would
otherwise match any character.

To find all lines in a file which do not contain the words 'foo' or 'bar':

```
$ grep -v -e 'foo' -e 'bar' myfile
```

A simple example of an extended regular expression:

```
$ egrep '19|20|25' calendar
```

Peruses the file 'calendar' looking for either 19, 20, or 25.

# SEE ALSO

ed(1), ex(1), gzip(1), sed(1), re\_format(7)

# STANDARDS

The grep utility is compliant with the IEEE Std 1003.1-2008 (“POSIX.1”)
specification.

The flags *[-AaBbCGHhILmoRUVwZ]* are extensions to that specification,
and the behaviour of the *-f* flag when used with an empty pattern file
is left undefined.

All long options are provided for compatibility with GNU versions of
this utility.

Historic versions of the *grep* utility also supported the flags
*[-ruy]*.  This implementation supports those options; however, their
use is strongly discouraged.

# HISTORY

The *grep* command first appeared in Version 4 AT&T UNIX.
