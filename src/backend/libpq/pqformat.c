/*-------------------------------------------------------------------------
 *
 * pqformat.c
 *		Routines for formatting and parsing frontend/backend messages
 *
 * Outgoing messages are built up in a StringInfo buffer (which is expansible)
 * and then sent in a single call to pq_putmessage.  This module provides data
 * formatting/conversion routines that are needed to produce valid messages.
 * Note in particular the distinction between "raw data" and "text"; raw data
 * is message protocol characters and binary values that are not subject to
 * MULTIBYTE conversion, while text is converted by MULTIBYTE rules.
 *
 * Incoming messages are read directly off the wire, as it were, but there
 * are still data-conversion tasks to be performed.
 *
 * Copyright (c) 1994, Regents of the University of California
 *
 *  $Id$
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 * Message output:
 *		pq_beginmessage	- initialize StringInfo buffer
 *		pq_sendbyte		- append a raw byte to a StringInfo buffer
 *		pq_sendint		- append a binary integer to a StringInfo buffer
 *		pq_sendbytes	- append raw data to a StringInfo buffer
 *		pq_sendtext		- append a text string (with MULTIBYTE conversion)
 *		pq_sendstring	- append a null-terminated text string (with MULTIBYTE)
 *		pq_endmessage	- send the completed message to the frontend
 * Note: it is also possible to append data to the StringInfo buffer using
 * the regular StringInfo routines, but this is discouraged since required
 * MULTIBYTE conversion may not occur.
 *
 * Message input:
 *		pq_getint		- get an integer from connection
 *		pq_getstr		- get a null terminated string from connection
 *		pq_getnchar		- get n characters from connection, and null-terminate
 * pq_getstr and pq_getnchar perform MULTIBYTE conversion on the collected
 * string.  Use the raw pqcomm.c routines pq_getstring and pq_getbytes
 * to fetch data without conversion.
 */
#include "postgres.h"

#include "libpq/pqformat.h"
#include "libpq/libpq.h"
#ifdef MULTIBYTE
#include "mb/pg_wchar.h"
#endif
#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif

#ifndef BYTE_ORDER
#error BYTE_ORDER must be defined as LITTLE_ENDIAN, BIG_ENDIAN or PDP_ENDIAN
#endif

#if BYTE_ORDER == LITTLE_ENDIAN

#define ntoh_s(n)	n
#define ntoh_l(n)	n
#define hton_s(n)	n
#define hton_l(n)	n

#else
#if BYTE_ORDER == BIG_ENDIAN

#define ntoh_s(n)	(uint16)((((uint16)n & 0x00ff) <<  8) | \
				 (((uint16)n & 0xff00) >>  8))
#define ntoh_l(n)	(uint32)((((uint32)n & 0x000000ff) << 24) | \
				 (((uint32)n & 0x0000ff00) <<  8) | \
				 (((uint32)n & 0x00ff0000) >>  8) | \
				 (((uint32)n & 0xff000000) >> 24))
#define hton_s(n)	(ntoh_s(n))
#define hton_l(n)	(ntoh_l(n))

#else
#if BYTE_ORDER == PDP_ENDIAN

#error PDP_ENDIAN macros not written yet

#else

#error BYTE_ORDER not defined as anything understood

#endif
#endif
#endif


/* --------------------------------
 *		pq_sendbyte		- append a raw byte to a StringInfo buffer
 * --------------------------------
 */
void
pq_sendbyte(StringInfo buf, int byt)
{
	appendStringInfoChar(buf, byt);
}

/* --------------------------------
 *		pq_sendbytes	- append raw data to a StringInfo buffer
 * --------------------------------
 */
void
pq_sendbytes(StringInfo buf, const char *data, int datalen)
{
	appendBinaryStringInfo(buf, data, datalen);
}

/* --------------------------------
 *		pq_sendtext		- append a text string (with MULTIBYTE conversion)
 *
 * NB: passed text string must be null-terminated, even though we expect
 * the caller to hand us the length (this is just because the caller
 * usually knows the length anyway).  In this routine, the data sent to
 * the frontend is NOT null-terminated.
 * --------------------------------
 */
void
pq_sendtext(StringInfo buf, const char *str, int slen)
{
#ifdef MULTIBYTE
	str = (const char *) pg_server_to_client(str, slen);
#endif
	appendBinaryStringInfo(buf, str, slen);
}

/* --------------------------------
 *		pq_sendstring	- append a null-terminated text string (with MULTIBYTE)
 *
 * NB: passed text string must be null-terminated, even though we expect
 * the caller to hand us the length (this is just because the caller
 * usually knows the length anyway).
 * --------------------------------
 */
void
pq_sendstring(StringInfo buf, const char *str, int slen)
{
#ifdef MULTIBYTE
	str = (const char *) pg_server_to_client(str, slen);
#endif
	appendBinaryStringInfo(buf, str, slen+1);
}

/* --------------------------------
 *		pq_sendint		- append a binary integer to a StringInfo buffer
 * --------------------------------
 */
void
pq_sendint(StringInfo buf, int i, int b)
{
	unsigned char	n8;
	uint16			n16;
	uint32			n32;

	switch (b)
	{
		case 1:
			n8 = (unsigned char) i;
			appendBinaryStringInfo(buf, (char *) &n8, 1);
			break;
		case 2:
			n16 = ((PG_PROTOCOL_MAJOR(FrontendProtocol) == 0) ? hton_s(i) : htons((uint16) i));
			appendBinaryStringInfo(buf, (char *) &n16, 2);
			break;
		case 4:
			n32 = ((PG_PROTOCOL_MAJOR(FrontendProtocol) == 0) ? hton_l(i) : htonl((uint32) i));
			appendBinaryStringInfo(buf, (char *) &n32, 4);
			break;
		default:
			elog(ERROR, "pq_sendint: unsupported size %d", b);
			break;
	}
}

/* --------------------------------
 *		pq_endmessage	- send the completed message to the frontend
 *
 * The data buffer is pfree()d, but if the StringInfo was allocated with
 * makeStringInfo then the caller must still pfree it.
 * --------------------------------
 */
void
pq_endmessage(StringInfo buf)
{
	if (pq_putmessage('\0', buf->data, buf->len))
	{
		snprintf(PQerrormsg, ERROR_MSG_LENGTH,
				 "FATAL: pq_endmessage failed: errno=%d\n", errno);
		fputs(PQerrormsg, stderr);
		pqdebug("%s", PQerrormsg);
	}
	pfree(buf->data);
	buf->data = NULL;
}

/* --------------------------------
 *		pq_getint - get an integer from connection
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int
pq_getint(int *result, int b)
{
	int				status;
	unsigned char	n8;
	uint16			n16;
	uint32			n32;

	switch (b)
	{
		case 1:
			status = pq_getbytes((char *) &n8, 1);
			*result = (int) n8;
			break;
		case 2:
			status = pq_getbytes((char *) &n16, 2);
			*result = (int) ((PG_PROTOCOL_MAJOR(FrontendProtocol) == 0) ?
							 ntoh_s(n16) : ntohs(n16));
			break;
		case 4:
			status = pq_getbytes((char *) &n32, 4);
			*result = (int) ((PG_PROTOCOL_MAJOR(FrontendProtocol) == 0) ?
							 ntoh_l(n32) : ntohl(n32));
			break;
		default:
			/* if we elog(ERROR) here, we will lose sync with the frontend,
			 * so just complain to postmaster log instead...
			 */
			fprintf(stderr, "pq_getint: unsupported size %d\n", b);
			status = EOF;
			*result = 0;
			break;
	}
	return status;
}

/* --------------------------------
 *		pq_getstr - get a null terminated string from connection
 *
 *		FIXME: we ought to use an expansible StringInfo buffer,
 *		rather than dropping data if the message is too long.
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int
pq_getstr(char *s, int maxlen)
{
	int			c;
#ifdef MULTIBYTE
	char	   *p;
#endif

	c = pq_getstring(s, maxlen);

#ifdef MULTIBYTE
	p = (char*) pg_client_to_server((unsigned char *) s, maxlen);
	if (s != p)					/* actual conversion has been done? */
		strcpy(s, p);
#endif

	return c;
}

/* --------------------------------
 *		pq_getnchar - get n characters from connection, and null-terminate
 *
 *		returns 0 if OK, EOF if trouble
 * --------------------------------
 */
int
pq_getnchar(char *s, int len)
{
	int			c;
#ifdef MULTIBYTE
	char	   *p;
#endif

	c = pq_getbytes(s, len);
	s[len] = '\0';

#ifdef MULTIBYTE
	p = (char*) pg_client_to_server((unsigned char *) s, len+1);
	if (s != p)					/* actual conversion has been done? */
		strcpy(s, p);
#endif

	return c;
}
