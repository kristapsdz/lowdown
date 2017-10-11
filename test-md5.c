#include <sys/types.h>
#include <md5.h>

int main(void)
{
	MD5_CTX ctx;

	MD5Init(&ctx);
	MD5Update(&ctx, "abcd", 4);

	return 0;
}
