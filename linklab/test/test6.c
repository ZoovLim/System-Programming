#include <stdlib.h>

int main(void)
{
	void *a;

	a = realloc((void*)0x10101010, 1024);
	free(a);
	a = realloc(a, 100);

	return 0;
}
