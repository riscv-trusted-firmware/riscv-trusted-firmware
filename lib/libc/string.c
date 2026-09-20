// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <string.h>

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d < s || d >= s + n)
		return memcpy(dst, src, n);
	d += n;
	s += n;
	while (n--)
		*--d = *--s;
	return dst;
}

void *memset(void *s, int c, size_t n)
{
	unsigned char *p = s;

	while (n--)
		*p++ = (unsigned char)c;
	return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *pa = a, *pb = b;

	for (; n; n--, pa++, pb++)
		if (*pa != *pb)
			return *pa - *pb;
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t max)
{
	size_t n = 0;

	while (n < max && s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (unsigned char)*a - (unsigned char)*b;
		if (!*a)
			break;
	}
	return 0;
}

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return NULL;
	}
}

char *strrchr(const char *s, int c)
{
	const char *last = NULL;

	for (;; s++) {
		if (*s == (char)c)
			last = s;
		if (!*s)
			return (char *)last;
	}
}

void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = s;

	for (; n; n--, p++)
		if (*p == (unsigned char)c)
			return (void *)p;
	return NULL;
}

char *strstr(const char *haystack, const char *needle)
{
	size_t n = strlen(needle);

	for (; *haystack; haystack++)
		if (!strncmp(haystack, needle, n))
			return (char *)haystack;
	return n ? NULL : (char *)haystack;
}
