/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MD5_H
#define MD5_H

#include <linux/types.h>

struct MD5_CTX {
	unsigned int count[2];
	unsigned int state[4];
	unsigned char buffer[64];
};

static inline u32 F(u32 x, u32 y, u32 z)
{
	return (x & y) | (~x & z);
}

static inline u32 G(u32 x, u32 y, u32 z)
{
	return (x & z) | (y & ~z);
}

static inline u32 H(u32 x, u32 y, u32 z)
{
	return x ^ y ^ z;
}

static inline u32 I(u32 x, u32 y, u32 z)
{
	return y ^ (x | ~z);
}

static inline u32 ROTATE_LEFT(u32 x, u32 n)
{
	return (x << n) | (x >> (32 - n));
}

static inline void FF(u32 *a, u32 b, u32 c, u32 d,
		      u32 x, u32 s, u32 ac)
{
	*a += F(b, c, d) + x + ac;
	*a = ROTATE_LEFT(*a, s);
	*a += b;
}

static inline void GG(u32 *a, u32 b, u32 c, u32 d,
		      u32 x, u32 s, u32 ac)
{
	*a += G(b, c, d) + x + ac;
	*a = ROTATE_LEFT(*a, s);
	*a += b;
}

static inline void HH(u32 *a, u32 b, u32 c, u32 d,
		      u32 x, u32 s, u32 ac)
{
	*a += H(b, c, d) + x + ac;
	*a = ROTATE_LEFT(*a, s);
	*a += b;
}

static inline void II(u32 *a, u32 b, u32 c, u32 d,
		      u32 x, u32 s, u32 ac)
{
	*a += I(b, c, d) + x + ac;
	*a = ROTATE_LEFT(*a, s);
	*a += b;
}

void md5_init(struct MD5_CTX *context);
void md5_update(struct MD5_CTX *context, unsigned char *input, unsigned int inputlen);
void md5_final(struct MD5_CTX *context, unsigned char digest[16]);
void md5_transform(unsigned int state[4], unsigned char block[64]);
void md5_encode(unsigned char *output, unsigned int *input, unsigned int len);
void md5_decode(unsigned int *output, unsigned char *input, unsigned int len);

#endif
