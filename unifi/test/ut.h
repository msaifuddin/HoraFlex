/* Minimal host unit-test helpers */
#ifndef UT_H
#define UT_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

extern int ut_fail, ut_pass;
#define CHECK(c) do { if (c) ut_pass++; else { ut_fail++; \
	fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a == _b) ut_pass++; else { ut_fail++; \
	fprintf(stderr, "%s:%d: %s == %s failed: %lld != %lld\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (const char *)(a), *_b = (b); if (!strcmp(_a, _b)) ut_pass++; else { ut_fail++; \
	fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); } } while (0)

size_t ut_unhex(const char *s, uint8_t *out, size_t max);
#endif
