#include "ut.h"
int ut_fail, ut_pass;

size_t ut_unhex(const char *s, uint8_t *out, size_t max)
{
	size_t n = 0;
	while (s[0] && s[1] && n < max) {
		unsigned v;
		if (sscanf(s, "%2x", &v) != 1)
			break;
		out[n++] = (uint8_t)v;
		s += 2;
	}
	return n;
}
