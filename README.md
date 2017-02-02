## Synopsis

**lowdown** is a fork of [hoedown](https://github.com/hoedown/hoedown).
This is a read-only repository for tracking development of the system.
Stable releases are available at the [website](https://kristaps.bsd.lv/lowdown).

The fork features the following modifications to its predecessor:

1. Put all header files into one and clean up source layout.
2. Remove all macro cruft (Microsoft checks and builtins).
3. Remove all option handling.
4. Use [err(3)](http://man.openbsd.org/err.3).
5. Use [pledge(2)](http://man.openbsd.org/pledge.2), Mac OS X's sandbox,
   or FreeBDS's
   [capsicum(4)](https://www.freebsd.org/cgi/man.cgi?query=capsicum),
   if applicable.
6. Add manpage.
7. Strip use of externally-defined memory management.
8. Rename internal API (for brevity).
9. Prune dead code and de-obfuscate some internal structures.
10. Create a usable library interface.
11. Remove "semantic quote" option, as it has no nroff basis.
12. Remove "emphasis as underline" option, as it has no nroff basis and
    is confusing on the web.

For the moment, **lowdown** output is the same as
[hoedown](https://github.com/hoedown/hoedown) with the following presets:

- XHTML mode
- autolinking
- fenced code
- tables
- superscripts
- footnotes
- disabled inline HTML (truly an evil feature of Markdown)
- "smartypants" enabled

The following modifications to the HTML5 output have been made:

- "smartypants" emits Unicode codepoints instead of HTML entities to
  make the output XML-friendly

The following major feature additions have been added:

- output mode for troff (via either the *-ms* or *-man* package)
- extension output mode for GNU troff
- "smartypants" mode for the troff outputs
- metadata support

It builds and runs on OpenBSD, Linux ([musl](https://www.musl-libc.org/)
and glibc), and Mac OS X.

If you have any comments or patches, please feel free to post them here
or notify me by e-mail.

## License

All sources use the ISC license.
See the [LICENSE.md](LICENSE.md) file for details.
