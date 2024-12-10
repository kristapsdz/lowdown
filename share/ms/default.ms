.\" -*- mode: troff; coding: utf-8 -*-"
$ifdef(copyright)--$
.ds LF Copyright \(co $copyright.escaperoffline$
$endif--$
$ifdef(and(date,copyright))--$
.ds RF $date.escaperoffline$
$endif--$
$ifdef(and(date,not(copyright)))--$
.DA $date.escaperoffline$
$endif--$
$ifdef(msheader)--$
$msheader$
$endif--$
.TL $title.escaperoffline$
$for(author.split())--$
.AU $this.escaperoffline$
$endfor--$
$for(affiliation.split())--$
.AI $this.escaperoffline$
$endfor--$
$body--$
