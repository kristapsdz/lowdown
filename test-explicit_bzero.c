#include <string.h>

int
main(void)
{
	char foo[10];

	explicit_bzero(foo, sizeof(foo));
	return(0);
}
