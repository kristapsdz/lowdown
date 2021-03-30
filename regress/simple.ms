.NH 1
.XN An h1 header
.LP
Paragraphs are separated by a blank line.
.PP
2nd paragraph. \fIItalic\fR, \fBbold\fR, and \fCmonospace\fR. Itemized lists
look like:
.IP "\(bu" 2
this one
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "\(bu" 2
that one
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "\(bu" 2
the other one
.LP
Note that \[u2014] not considering the asterisk \[u2014] the actual text
content starts at 4-columns in.
.RS
.PP
Block quotes are
written like so.
.PP
They can span multiple paragraphs,
if you like.
.RE
.LP
Use 3 dashes for an em-dash. Use 2 dashes for ranges (ex., \[u201C]it\[u2019]s all
in chapters 12\[u2013]14\[u201D]). Three dots \[u2026] will be converted to an ellipsis.
Unicode is supported. ☺
.NH 2
.XN An h2 header
.LP
Here\[u2019]s a numbered list:
.IP "1.  "
first item
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "2.  "
second item
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "3.  "
third item
.LP
Note again how the actual text starts at 4 columns in (4 characters
from the left side). Here\[u2019]s a code sample:
.LP
.nf
.ft CR
# Let me re-iterate ...
for i in 1 .. 10 { do-something(i) }
.ft
.fi
.PP
As you probably guessed, indented 4 spaces. By the way, instead of
indenting the block, you can use delimited blocks, if you like:
.LP
.nf
.ft CR
define foobar() {
    print \(dqWelcome to flavor country!\(dq;
}
.ft
.fi
.PP
(which makes copying & pasting easier). You can optionally mark the
delimited block for Pandoc to syntax highlight it:
.LP
.nf
.ft CR
import time
# Quick, count to ten!
for i in range(10):
    # (but not *too* quick)
    time.sleep(0.5)
    print i
.ft
.fi
.NH 3
.XN An h3 header
.LP
Now a nested list:
.IP "1.  "
First, get these ingredients:
.RS
.IP "\(bu" 2
carrots
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "\(bu" 2
celery
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "\(bu" 2
lentils
.RE
.IP "2.  "
Boil some water.
.IP "3.  "
Dump everything in the pot and follow
this algorithm:
.LP
.nf
.ft CR
find wooden spoon
uncover pot
stir
cover pot
balance wooden spoon precariously on pot handle
wait 10 minutes
goto first step (or shut off burner when done)
.ft
.fi
.IP
Do not bump wooden spoon or it will fall.
.LP
Notice again how text always lines up on 4-space indents (including
that last line which continues item 3 above).
.PP
Here\[u2019]s a link to 
.pdfhref W -A "," -D http://foo.bar a website
to a 
.pdfhref W -A "," -D local-doc.html local doc
and to a 
.pdfhref W -A "." -D #an-h2-header section heading in the current doc
Here\[u2019]s a footnote \**.
.PP
Tables can look like this:
.TS H
tab(|) expand allbox;
lb lb lb
l l l.
T{
size
T}|T{
material
T}|T{
color
T}
.TH
T{
9
T}|T{
leather
T}|T{
brown
T}
T{
10
T}|T{
hemp canvas
T}|T{
natural
T}
T{
11
T}|T{
glass
T}|T{
transparent
T}
.TE
.LP
Table: Shoes, their sizes, and what they\[u2019]re made of
.PP
(The above is the caption for the table.)
.PP
A horizontal rule follows.
.LP
\l'\n(.lu-\n(\n[.in]u'
.PP
Here\[u2019]s a definition list:
.IP "apples"
Good for making applesauce.
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "oranges"
Citrus!
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "tomatoes"
There\[u2019]s no \[u201C]e\[u201D] in tomatoe.
.LP
Again, text is indented 4 spaces. (Put a blank line between each
term/definition pair to spread things out more.)
.PP
Images can be specified like so:
.PP
.PP
And note that you can backslash-escape any punctuation characters
which you wish to be displayed literally, ex.: \(gafoo\(ga, *bar*, etc.
.FS
Footnote text goes here.
.FE
