#include <string.h>

int main(void)
{
	char buf[10];
	memset_s(buf, 0, 'c', sizeof(buf));
	return 0;
}
