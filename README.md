## Synopsis

**lowdown** is a fork of [hoedown](https://github.com/hoedown/hoedown).
It's inspired by the desire for markdown input for
[sblg(1)](https://kristaps.bsd.lv/sblg).  The fork is simply to make the
code readable:

1. Put all header files into one and clean up source layout.
2. Remove all macro cruft (Microsoft checks and builtins).
3. Remove all option handling.
4. Use [err(3)](http://man.openbsd.org/err.3).
5. Use [pledge(2)](http://man.openbsd.org/pledge.2) or Mac OS X's
   sandbox, if applicable.
6. Add manpage.

For the moment, **lowdown** is the same as
[hoedown](https://github.com/hoedown/hoedown) in XHTML mode with
autolinking, fenced code, and tables (see their [popular
presets](https://github.com/hoedown/hoedown/wiki/Popular-presets)).
The only modification is that the first paragraph is wrapped in an
`<aside>` block.

It's been verified to build and run on OpenBSD, Linux, and Mac OS X.  It
has a [Coverity](https://scan.coverity.com/projects/lowdown)
registration to boot.

This is a read-only repository for a CVS repository elsewhere.  But by
all means do pulls and submit issues: I'll merge them into the CVS
repository, then push to GitHub afterward.

## License

All sources use the ISC license.
See the [LICENSE.md](LICENSE.md) file for details.
