## Synopsis

**lowdown** is a fork of [hoedown](https://github.com/hoedown/hoedown).
The fork is simply to make the code readable:

1. Put all header files into one.
2. Remove all macro cruft (Microsoft checks and builtins).
3. Remove all option handling.
4. Use [err(3)](http://man.openbsd.org/err.3).
4. Use [pledge(2)](http://man.openbsd.org/pledge.2) (if applicable).

It's functionally equivalent to running
[hoedown](https://github.com/hoedown/hoedown) in XHTML mode.

**lowdown** was inspired by the desire for markdown input for
[sblg(1)](https://kristaps.bsd.lv/sblg).

## License

All sources use the ISC license.
See the [LICENSE.md](LICENSE.md) file for details.
