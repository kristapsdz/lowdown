.PP
a \c
.pdfhref W -D https://bar.com -- https://bar.com
b c
.PP
a b\c
.pdfhref W -A "\c" -D https://bar.com -- https://bar.com
c d
.PP
a \c
.pdfhref W -D https://bar.com -- b
c
.PP
a b\c
.pdfhref W -A "\c" -D https://bar.com -- c
d e
.NH 1
a \c
.pdfhref W -D https://bar.com -- b
c
.pdfhref O 1 a b c
.pdfhref M a-b-c
.LP
asd
.NH 1
a\c
.pdfhref W -A "\c" -D https://bar.com -- b
c
.pdfhref O 1 abc
.pdfhref M abc
.LP
a \c
.pdfhref W -D https://foo.com -- \fIb\fR \f(BIc\fR
d
.PP
\f(BIa \c
.pdfhref W -D https://foo.com -- b
c\fR d
