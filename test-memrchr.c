#if defined(__linux__) || defined(__MINT__)
#define _GNU_SOURCE	/* See test-*.c what needs this. */
#endif
#include <string.h>

int
main(void)
{
	const char *buf = "abcdef";
	void *res;

	res = memrchr(buf, 'a', strlen(buf));
	return(NULL == res ? 1 : 0);
}
