#include <sys/capability.h>

int
main(void)
{
	cap_enter();
	return(0);
}
