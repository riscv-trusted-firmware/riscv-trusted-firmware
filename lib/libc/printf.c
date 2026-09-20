// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Minimal printf: %c %s %d %i %u %x %X %p %%, flags '-' '0', width, and
 * the l/ll/z length modifiers. No floating point.
 */

#include <console.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <util.h>

struct out {
	char *buf;
	size_t size;
	size_t pos;
};

static void out_char(struct out *o, char c)
{
	if (o->pos + 1 < o->size)
		o->buf[o->pos] = c;
	o->pos++;
}

static void out_pad(struct out *o, int n, char pad)
{
	while (n-- > 0)
		out_char(o, pad);
}

static void out_str(struct out *o, const char *s, int width, bool left,
		    size_t len)
{
	int pad = width - (int)len;

	if (!left)
		out_pad(o, pad, ' ');
	while (len--)
		out_char(o, *s++);
	if (left)
		out_pad(o, pad, ' ');
}

static void out_num(struct out *o, unsigned long long v, unsigned int base,
		    bool neg, bool upper, int width, bool left, bool zero)
{
	char tmp[24] = {};
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int n = 0;

	do {
		tmp[n++] = digits[v % base];
		v /= base;
	} while (v);
	if (neg)
		tmp[n++] = '-';

	int pad = width - n;

	if (!left)
		out_pad(o, pad, zero ? '0' : ' ');
	while (n)
		out_char(o, tmp[--n]);
	if (left)
		out_pad(o, pad, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct out o = { buf, size, 0 };

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			out_char(&o, *fmt);
			continue;
		}
		fmt++;

		bool left = false, zero = false;
		int width = 0, lmod = 0;

		for (;; fmt++) {
			if (*fmt == '-')
				left = true;
			else if (*fmt == '0')
				zero = true;
			else
				break;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		for (; *fmt == 'l' || *fmt == 'z'; fmt++)
			lmod += (*fmt == 'z') ? 2 : 1;

		switch (*fmt) {
		case 'c': {
			char c = (char)va_arg(ap, int);

			out_str(&o, &c, width, left, 1);
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);

			if (!s)
				s = "(null)";
			out_str(&o, s, width, left, strlen(s));
			break;
		}
		case 'd':
		case 'i': {
			long long v = lmod >= 2 ? va_arg(ap, long long) :
				      lmod == 1 ? va_arg(ap, long) :
						  va_arg(ap, int);
			bool neg = v < 0;

			out_num(&o,
				neg ? -(unsigned long long)v :
				(unsigned long long)v,
				10, neg, false, width, left, zero);
			break;
		}
		case 'u':
		case 'x':
		case 'X': {
			unsigned long long v =
				lmod >= 2 ? va_arg(ap, unsigned long long) :
				lmod == 1 ? va_arg(ap, unsigned long) :
					    va_arg(ap, unsigned int);

			out_num(&o, v, *fmt == 'u' ? 10 : 16, false,
				*fmt == 'X', width, left, zero);
			break;
		}
		case 'p':
			out_char(&o, '0');
			out_char(&o, 'x');
			out_num(&o, (unsigned long)va_arg(ap, void *), 16,
				false, false, 2 * (int)sizeof(void *), false,
				true);
			break;
		case '%':
			out_char(&o, '%');
			break;
		case '\0':
			fmt--;
			break;
		default:
			out_char(&o, '%');
			out_char(&o, *fmt);
			break;
		}
	}

	if (size)
		buf[MIN(o.pos, size - 1)] = '\0';
	return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n = 0;

	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

int vprintf(const char *fmt, va_list ap)
{
	char buf[CONFIG_LIBC_PRINTF_BUF] = {};
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);

	console_puts(buf);
	return n;
}

int printf(const char *fmt, ...)
{
	va_list ap;
	int n = 0;

	va_start(ap, fmt);
	n = vprintf(fmt, ap);
	va_end(ap);
	return n;
}

int puts(const char *s)
{
	console_puts(s);
	console_putc('\n');
	return 0;
}
