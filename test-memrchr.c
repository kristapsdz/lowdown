#ifdef __linux__
# define _GNU_SOURCE
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
