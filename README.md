## Synopsis

**lowdown** is a fork of [hoedown](https://github.com/hoedown/hoedown),
although the parser and front-ends have changed significantly.

This is a read-only repository for tracking development of the system
and managing bug reports and patches.  (Feature requests will be just be
closed, sorry!) Stable releases are available at the
[website](https://kristaps.bsd.lv/lowdown).

The fork features the following modifications to its predecessor:

1. Put all header files into one and clean up source layout.
2. Remove all macro cruft (Microsoft checks and builtins).
3. Remove all option handling.
4. Use [err(3)](https://man.openbsd.org/err.3).
5. Use [pledge(2)](https://man.openbsd.org/pledge.2), Mac OS X's sandbox,
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
13. Have the back-end parser to generate an AST instead of directly
    rendering.
14. Several superfluous mechanisms (pools and stacks) removed.
15. Document language syntax in a manpage.

For the moment, **lowdown** output is the same as
[hoedown](https://github.com/hoedown/hoedown) with the following presets:

- XHTML mode
- autolinking
- fenced code
- tables
- superscripts
- footnotes
- disabled inline HTML (truly an evil feature of Markdown)
- smart typography enabled

Individual features can be enabled and disabled at will.

The following modifications to the HTML5 output have been made:

- smart typography emits Unicode codepoints instead of HTML entities to
  make the output XML-friendly
- emit image dimensions if specified in the link text

The following major feature additions have been added:

- output mode for troff (via either the *-ms* or *-man* package)
- extension output mode for GNU troff (*-mpdfmark*, PSPIC, etc.)
- smart typography interpreter for all outputs
- stylised terminal output (like
  [glow](https://github.com/charmbracelet/glow))
- metadata support
- tree output for AST debugging
- "diff" engine for semantic differences between documents

Tested to build and run on OpenBSD, Linux
([musl](https://www.musl-libc.org/) and glibc), FreeBSD, and Mac OS X.
It should, however, run on any modern UNIX system.

If you have any comments or patches, please feel free to post them here
or notify me by e-mail.

## License

All sources use the ISC license.
See the [LICENSE.md](LICENSE.md) file for details.
