#include <cstdio>

int main()
{
	const volatile int value = 1;
	std::printf("MSan smoke check: %d\n", value);
	return 0;
}
