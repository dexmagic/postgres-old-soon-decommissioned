/*
 * Copyright (c) 1996 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 *
 *	  $Header$
 */

#include "postgres.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>

#include "utils/inet.h"
#include "utils/builtins.h"

#define NS_IN6ADDRSZ 16
#define NS_INT16SZ 2

#ifdef SPRINTF_CHAR
#define SPRINTF(x) strlen(sprintf/**/x)
#else
#define SPRINTF(x) ((size_t)sprintf x)
#endif

static char *inet_net_ntop_ipv4(const u_char *src, int bits,
				   char *dst, size_t size);
static char *inet_cidr_ntop_ipv4(const u_char *src, int bits,
					char *dst, size_t size);
static char *inet_net_ntop_ipv6(const u_char *src, int bits,
				   char *dst, size_t size);
static char *inet_cidr_ntop_ipv6(const u_char *src, int bits,
					char *dst, size_t size);

/*
 * char *
 * inet_cidr_ntop(af, src, bits, dst, size)
 *	convert network number from network to presentation format.
 *	generates CIDR style result always.
 * return:
 *	pointer to dst, or NULL if an error occurred (check errno).
 * author:
 *	Paul Vixie (ISC), July 1996
 */
char *
inet_cidr_ntop(int af, const void *src, int bits, char *dst, size_t size)
{
	switch (af)
	{
		case PGSQL_AF_INET:
			return (inet_cidr_ntop_ipv4(src, bits, dst, size));
		case PGSQL_AF_INET6:
			return (inet_cidr_ntop_ipv6(src, bits, dst, size));
		default:
			errno = EAFNOSUPPORT;
			return (NULL);
	}
}


/*
 * static char *
 * inet_cidr_ntop_ipv4(src, bits, dst, size)
 *	convert IPv4 network number from network to presentation format.
 *	generates CIDR style result always.
 * return:
 *	pointer to dst, or NULL if an error occurred (check errno).
 * note:
 *	network byte order assumed.  this means 192.5.5.240/28 has
 *	0x11110000 in its fourth octet.
 * author:
 *	Paul Vixie (ISC), July 1996
 */
static char *
inet_cidr_ntop_ipv4(const u_char *src, int bits, char *dst, size_t size)
{
	char	   *odst = dst;
	char	   *t;
	u_int		m;
	int			b;

	if (bits < 0 || bits > 32)
	{
		errno = EINVAL;
		return (NULL);
	}
	if (bits == 0)
	{
		if (size < sizeof "0")
			goto emsgsize;
		*dst++ = '0';
		*dst = '\0';
	}

	/* Format whole octets. */
	for (b = bits / 8; b > 0; b--)
	{
		if (size < sizeof ".255")
			goto emsgsize;
		t = dst;
		if (dst != odst)
			*dst++ = '.';
		dst += SPRINTF((dst, "%u", *src++));
		size -= (size_t) (dst - t);
	}

	/* Format partial octet. */
	b = bits % 8;
	if (b > 0)
	{
		if (size < sizeof ".255")
			goto emsgsize;
		t = dst;
		if (dst != odst)
			*dst++ = '.';
		m = ((1 << b) - 1) << (8 - b);
		dst += SPRINTF((dst, "%u", *src & m));
		size -= (size_t) (dst - t);
	}

	/* Format CIDR /width. */
	if (size < sizeof "/32")
		goto emsgsize;
	dst += SPRINTF((dst, "/%u", bits));

	return (odst);

emsgsize:
	errno = EMSGSIZE;
	return (NULL);
}

/*
 * static char *
 * inet_net_ntop_ipv6(src, bits, fakebits, dst, size)
 *	convert IPv6 network number from network to presentation format.
 *	generates CIDR style result always. Picks the shortest representation
 *	unless the IP is really IPv4.
 *	always prints specified number of bits (bits).
 * return:
 *	pointer to dst, or NULL if an error occurred (check errno).
 * note:
 *	network byte order assumed.  this means 192.5.5.240/28 has
 *	0x11110000 in its fourth octet.
 * author:
 *	Vadim Kogan (UCB), June 2001
 *	Original version (IPv4) by Paul Vixie (ISC), July 1996
 */

static char *
inet_cidr_ntop_ipv6(const u_char *src, int bits, char *dst, size_t size)
{
	u_int		m;
	int			b;
	int			p;
	int			zero_s,
				zero_l,
				tmp_zero_s,
				tmp_zero_l;
	int			i;
	int			is_ipv4 = 0;
	int			double_colon = 0;
	unsigned char inbuf[16];
	char		outbuf[sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255/128")];
	char	   *cp;
	int			words;
	u_char	   *s;

	if (bits < 0 || bits > 128)
	{
		errno = EINVAL;
		return (NULL);
	}

	cp = outbuf;
	double_colon = 0;

	if (bits == 0)
	{
		*cp++ = ':';
		*cp++ = ':';
		*cp = '\0';
		double_colon = 1;
	}
	else
	{
		/* Copy src to private buffer.	Zero host part. */
		p = (bits + 7) / 8;
		memcpy(inbuf, src, p);
		memset(inbuf + p, 0, 16 - p);
		b = bits % 8;
		if (b != 0)
		{
			m = ~0 << (8 - b);
			inbuf[p - 1] &= m;
		}

		s = inbuf;

		/* how many words need to be displayed in output */
		words = (bits + 15) / 16;
		if (words == 1)
			words = 2;

		/* Find the longest substring of zero's */
		zero_s = zero_l = tmp_zero_s = tmp_zero_l = 0;
		for (i = 0; i < (words * 2); i += 2)
		{
			if ((s[i] | s[i + 1]) == 0)
			{
				if (tmp_zero_l == 0)
					tmp_zero_s = i / 2;
				tmp_zero_l++;
			}
			else
			{
				if (tmp_zero_l && zero_l < tmp_zero_l)
				{
					zero_s = tmp_zero_s;
					zero_l = tmp_zero_l;
					tmp_zero_l = 0;
				}
			}
		}

		if (tmp_zero_l && zero_l < tmp_zero_l)
		{
			zero_s = tmp_zero_s;
			zero_l = tmp_zero_l;
		}

		if (zero_l != words && zero_s == 0 && ((zero_l == 6) ||
					  ((zero_l == 5 && s[10] == 0xff && s[11] == 0xff) ||
					   ((zero_l == 7 && s[14] != 0 && s[15] != 1)))))
			is_ipv4 = 1;

		/* Format whole words. */
		for (p = 0; p < words; p++)
		{
			if (zero_l != 0 && p >= zero_s && p < zero_s + zero_l)
			{
				/* Time to skip some zeros */
				if (p == zero_s)
					*cp++ = ':';
				if (p == words - 1)
				{
					*cp++ = ':';
					double_colon = 1;
				}
				s++;
				s++;
				continue;
			}

			if (is_ipv4 && p > 5)
			{
				*cp++ = (p == 6) ? ':' : '.';
				cp += SPRINTF((cp, "%u", *s++));
				/* we can potentially drop the last octet */
				if (p != 7 || bits > 120)
				{
					*cp++ = '.';
					cp += SPRINTF((cp, "%u", *s++));
				}
			}
			else
			{
				if (cp != outbuf)
					*cp++ = ':';
				cp += SPRINTF((cp, "%x", *s * 256 + s[1]));
				s += 2;
			}
		}
	}

	if (!double_colon)
	{
		if (bits < 128 - 32)
			cp += SPRINTF((cp, "::"));
		else if (bits < 128 - 16)
			cp += SPRINTF((cp, ":0"));
	}

	/* Format CIDR /width. */
	SPRINTF((cp, "/%u", bits));

	if (strlen(outbuf) + 1 > size)
		goto emsgsize;
	strcpy(dst, outbuf);

	return (dst);

emsgsize:
	errno = EMSGSIZE;
	return (NULL);
}

/*
 * char *
 * inet_net_ntop(af, src, bits, dst, size)
 *	convert host/network address from network to presentation format.
 *	"src"'s size is determined from its "af".
 * return:
 *	pointer to dst, or NULL if an error occurred (check errno).
 * note:
 *	192.5.5.1/28 has a nonzero host part, which means it isn't a network
 *	as called for by inet_net_pton() but it can be a host address with
 *	an included netmask.
 * author:
 *	Paul Vixie (ISC), October 1998
 */
char *
inet_net_ntop(int af, const void *src, int bits, char *dst, size_t size)
{
	switch (af)
	{
		case PGSQL_AF_INET:
			return (inet_net_ntop_ipv4(src, bits, dst, size));
		case PGSQL_AF_INET6:
			return (inet_net_ntop_ipv6(src, bits, dst, size));
		default:
			errno = EAFNOSUPPORT;
			return (NULL);
	}
}

/*
 * static char *
 * inet_net_ntop_ipv4(src, bits, dst, size)
 *	convert IPv4 network address from network to presentation format.
 *	"src"'s size is determined from its "af".
 * return:
 *	pointer to dst, or NULL if an error occurred (check errno).
 * note:
 *	network byte order assumed.  this means 192.5.5.240/28 has
 *	0b11110000 in its fourth octet.
 * author:
 *	Paul Vixie (ISC), October 1998
 */
static char *
inet_net_ntop_ipv4(const u_char *src, int bits, char *dst, size_t size)
{
	char	   *odst = dst;
	char	   *t;
	int			len = 4;
	int			b;

	if (bits < 0 || bits > 32)
	{
		errno = EINVAL;
		return (NULL);
	}

	/* Always format all four octets, regardless of mask length. */
	for (b = len; b > 0; b--)
	{
		if (size < sizeof ".255")
			goto emsgsize;
		t = dst;
		if (dst != odst)
			*dst++ = '.';
		dst += SPRINTF((dst, "%u", *src++));
		size -= (size_t) (dst - t);
	}

	/* don't print masklen if 32 bits */
	if (bits != 32)
	{
		if (size < sizeof "/32")
			goto emsgsize;
		dst += SPRINTF((dst, "/%u", bits));
	}

	return (odst);

emsgsize:
	errno = EMSGSIZE;
	return (NULL);
}

static int
decoct(const u_char *src, int bytes, char *dst, size_t size)
{
	char	   *odst = dst;
	char	   *t;
	int			b;

	for (b = 1; b <= bytes; b++)
	{
		if (size < sizeof "255.")
			return (0);
		t = dst;
		dst += SPRINTF((dst, "%u", *src++));
		if (b != bytes)
		{
			*dst++ = '.';
			*dst = '\0';
		}
		size -= (size_t) (dst - t);
	}
	return (dst - odst);
}

static char *
inet_net_ntop_ipv6(const u_char *src, int bits, char *dst, size_t size)
{
	/*
	 * Note that int32_t and int16_t need only be "at least" large enough
	 * to contain a value of the specified size.  On some systems, like
	 * Crays, there is no such thing as an integer variable with 16 bits.
	 * Keep this in mind if you think this function should have been coded
	 * to use pointer overlays.  All the world's not a VAX.
	 */
	char		tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255/128"];
	char	   *tp;
	struct
	{
		int			base,
					len;
	}			best, cur;
	u_int		words[NS_IN6ADDRSZ / NS_INT16SZ];
	int			i;

	if ((bits < -1) || (bits > 128))
	{
		errno = EINVAL;
		return (NULL);
	}

	/*
	 * Preprocess: Copy the input (bytewise) array into a wordwise array.
	 * Find the longest run of 0x00's in src[] for :: shorthanding.
	 */
	memset(words, '\0', sizeof words);
	for (i = 0; i < NS_IN6ADDRSZ; i++)
		words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));
	best.base = -1;
	cur.base = -1;
	for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++)
	{
		if (words[i] == 0)
		{
			if (cur.base == -1)
				cur.base = i, cur.len = 1;
			else
				cur.len++;
		}
		else
		{
			if (cur.base != -1)
			{
				if (best.base == -1 || cur.len > best.len)
					best = cur;
				cur.base = -1;
			}
		}
	}
	if (cur.base != -1)
	{
		if (best.base == -1 || cur.len > best.len)
			best = cur;
	}
	if (best.base != -1 && best.len < 2)
		best.base = -1;

	/*
	 * Format the result.
	 */
	tp = tmp;
	for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++)
	{
		/* Are we inside the best run of 0x00's? */
		if (best.base != -1 && i >= best.base &&
			i < (best.base + best.len))
		{
			if (i == best.base)
				*tp++ = ':';
			continue;
		}
		/* Are we following an initial run of 0x00s or any real hex? */
		if (i != 0)
			*tp++ = ':';
		/* Is this address an encapsulated IPv4? */
		if (i == 6 && best.base == 0 && (best.len == 6 ||
								 (best.len == 7 && words[7] != 0x0001) ||
								  (best.len == 5 && words[5] == 0xffff)))
		{
			int			n;

			n = decoct(src + 12, 4, tp, sizeof tmp - (tp - tmp));
			if (n == 0)
			{
				errno = EMSGSIZE;
				return (NULL);
			}
			tp += strlen(tp);
			break;
		}
		tp += SPRINTF((tp, "%x", words[i]));
	}

	/* Was it a trailing run of 0x00's? */
	if (best.base != -1 && (best.base + best.len) ==
		(NS_IN6ADDRSZ / NS_INT16SZ))
		*tp++ = ':';
	*tp = '\0';

	if (bits != -1 && bits != 128)
		tp += SPRINTF((tp, "/%u", bits));

	/*
	 * Check for overflow, copy, and we're done.
	 */
	if ((size_t) (tp - tmp) > size)
	{
		errno = EMSGSIZE;
		return (NULL);
	}
	strcpy(dst, tmp);
	return (dst);
}
