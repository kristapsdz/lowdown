.PP
first\c
.pdfhref L -D footnote-1 -- \**
.FS
.pdfhref M footnote-1
one
.FE
.TS H
tab(|) expand allbox;
lb lb
l l.
T{
a
T}|T{
c
T}
.TH
T{
9
T}|T{
foo\c
.pdfhref L -D footnote-2 -- \**
.pdfhref L -D footnote-3 -- \**
T}
.TE
.FS
.pdfhref M footnote-2
two
.FE
.FS
.pdfhref M footnote-3
three
.PP
hello
.IP "\(bu" 5
world
.FE
.PP
Now\c
.pdfhref L -D footnote-4 -- \**
.FS
.pdfhref M footnote-4
four
.FE
