.NH 1
An h1 header
.pdfhref O 1 An h1 header
.pdfhref M an-h1-header
.LP
Paragraphs are separated by a blank line.
.PP
2nd paragraph. \fIItalic\fR, \fBbold\fR, and \f(CRmonospace\fR. Itemized lists
look like:
.IP "\(bu" 5
this one
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "\(bu" 5
that one
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "\(bu" 5
the other one
.LP
Note that \(em not considering the asterisk \(em the actual text
content starts at 4-columns in.
.RS
.PP
Block quotes are
written like so.
.PP
They can span multiple paragraphs,
if you like.
.RE
.PP
Use 3 dashes for an em-dash. Use 2 dashes for ranges (ex., \(lqit\(cqs all
in chapters 12\(en14\(rq). Three dots \[u2026] will be converted to an ellipsis.
Unicode is supported. ☺
.NH 2
An h2 header
.pdfhref O 2 An h2 header
.pdfhref M an-h2-header
.LP
Here\(cqs a numbered list:
.IP "1." 5
first item
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "2." 5
second item
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "3." 5
third item
.LP
Note again how the actual text starts at 4 columns in (4 characters
from the left side). Here\(cqs a code sample:
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
An h3 header
.pdfhref O 3 An h3 header
.pdfhref M an-h3-header
.LP
Now a nested list:
.IP "1." 5
First, get these ingredients:
.RS
.IP "\(bu" 5
carrots
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "\(bu" 5
celery
.if n \
.sp -1
.if t \
.sp -0.25v
.IP "\(bu" 5
lentils
.RE
.IP "2." 5
Boil some water.
.IP "3." 5
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
Here\(cqs a link to \c
.pdfhref W -A "\c" -D http://foo.bar -- a website
, to a \c
.pdfhref W -A "\c" -D local-doc.html -- local doc
, and to a \c
.pdfhref L -A "\c" -D #an-h2-header -- section heading in the current doc
\&. Here\(cqs a footnote \c
.pdfhref L -A "\c" -D footnote-1 -- \**
\&.
.FS
.pdfhref M footnote-1
Footnote text goes here.
.FE
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
.PP
Table: Shoes, their sizes, and what they\(cqre made of
.PP
(The above is the caption for the table.)
.PP
A horizontal rule follows.
.LP
.ie d HR \{\
.HR
\}
.el \{\
.sp 1v
\l'\n(.lu'
.sp 1v
.\}
.LP
Here\(cqs a definition list:
.XP
apples
.br
Good for making applesauce.
.XP
oranges
.br
Citrus!
.XP
tomatoes
.br
There\(cqs no \(lqe\(rq in tomatoe.
.LP
Again, text is indented 4 spaces. (Put a blank line between each
term/definition pair to spread things out more.)
.PP
Images can be specified like so:
.PP
\fBexample image\fR (Image: \fIexample-image.jpg\fR)
.PP
And note that you can backslash-escape any punctuation characters
which you wish to be displayed literally, ex.: \(gafoo\(ga, *bar*, etc.
