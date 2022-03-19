/*
** Interface definition for Pikchr.
*/


/* The main interface.  Invoke this routine to translate PIKCHR source
** text into SVG. The SVG is returned in a buffer obtained from malloc().
** The caller is responsible for freeing the buffer.
**
** If an error occurs, *pnWidth is filled with a negative number and
** the return buffer contains error message text instead of SVG.  By
** default, the error message is HTML encoded.  However, error messages
** come out as plaintext if the PIKCHR_PLAINTEXT_ERRORS flag is included
** as one of the bits in the mFlags parameter.
*/
char *pikchr(
  const char *zText,     /* Input PIKCHR source text.  zero-terminated */
  const char *zClass,    /* Add class="%s" to <svg> markup */
  unsigned int mFlags,   /* Flags used to influence rendering behavior */
  int *pnWidth,          /* OUT: Write width of <svg> here, if not NULL */
  int *pnHeight          /* OUT: Write height here, if not NULL */
);

/* Include PIKCHR_PLAINTEXT_ERRORS among the bits of mFlags on the 3rd
** argument to pikchr() in order to cause error message text to come out
** as text/plain instead of as text/html
*/
#define PIKCHR_PLAINTEXT_ERRORS 0x0001

/* Include PIKCHR_DARK_MODE among the bits of mFlags on the 3rd
** argument to pikchr() to render the image in dark mode.
*/
#define PIKCHR_DARK_MODE        0x0002
