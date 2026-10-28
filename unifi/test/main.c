#include "ut.h"

void test_crypto(void);
void test_tnbu(void);
void test_cfg(void);
void test_agent(void);

int main(void)
{
	test_crypto();
	test_tnbu();
	test_cfg();
	test_agent();
	printf("%d passed, %d failed\n", ut_pass, ut_fail);
	return ut_fail ? 1 : 0;
}
