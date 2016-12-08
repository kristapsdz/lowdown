## Synopsis

**lowdown** is a fork of [hoedown](https://github.com/hoedown/hoedown).
It's inspired by the desire for markdown input for
[sblg(1)](https://kristaps.bsd.lv/sblg).  The fork is simply to make the
code readable:

1. Put all header files into one and clean up source layout.
2. Remove all macro cruft (Microsoft checks and builtins).
3. Remove all option handling.
4. Use [err(3)](http://man.openbsd.org/err.3).
5. Use [pledge(2)](http://man.openbsd.org/pledge.2), Mac OS X's sandbox,
   or FreeBDS's
   [capsicum(4)](https://www.freebsd.org/cgi/man.cgi?query=capsicum),
   if applicable.
6. Add manpage.

For the moment, **lowdown** is the same as
[hoedown](https://github.com/hoedown/hoedown) with the following presets:

- XHTML mode
- autolinking
- fenced code
- tables
- footnotes
- "smartypants" enabled

The following modifications have been made:

- the first paragraph is wrapped in an `<aside>` block (for integration
  with [sblg(1)](https://kristaps.bsd.lv/sblg)
- "smartypants" emits Unicode codepoints instead of HTML entities to
  make the output XML-friendly

It builds and runs on OpenBSD, Linux ([musl](https://www.musl-libc.org/) and glibc), and Mac OS X.  (FreeBSD is
experimental.)  It has a
[Coverity](https://scan.coverity.com/projects/lowdown) registration to
boot.

This is a read-only repository for a CVS repository elsewhere.  But by
all means do pulls and submit issues: I'll merge them into the CVS
repository, then push to GitHub afterward.

## Example usage

I usually use **lowdown** when writing
[sblg(1)](https://kristaps.bsd.lv/sblg) articles when I'm too lazy to
write in proper HTML5.
(For those not in the know, [sblg(1)](https://kristaps.bsd.lv/sblg) is a
simple tool for knitting together blog articles into a blog feed.)
This basically means wrapping the output of **lowdown** in the elements
indicating a blog article.
I do this in my Makefiles:

```Makefile
.md.xml:
	( echo "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>" ; \
	  echo "<article data-sblg-article=\"1\">" ; \
	  lowdown $< ; \
	  echo "</article>" ; ) >$@
```

Of course, you can just do so on the shell, assuming "article.md" is the
filename of our article.

```sh
( echo "<!DOCTYPE html>" ; \
  echo "<html>" ; \
  echo "<head><title></title</head>" ; \
  echo "<body>" ; \
  lowdown article.md ; \
  echo "</body>" ; \
  echo "</html>" ; ) >article.html
```

## License

All sources use the ISC license.
See the [LICENSE.md](LICENSE.md) file for details.
