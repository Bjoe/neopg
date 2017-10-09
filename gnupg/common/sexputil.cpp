/* sexputil.c - Utility functions for S-expressions.
 * Copyright (C) 2005, 2007, 2009 Free Software Foundation, Inc.
 * Copyright (C) 2013 Werner Koch
 *
 * This file is part of GnuPG.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either
 *
 *   - the GNU Lesser General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version.
 *
 * or
 *
 *   - the GNU General Public License as published by the Free
 *     Software Foundation; either version 2 of the License, or (at
 *     your option) any later version.
 *
 * or both in parallel, as here.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* This file implements a few utility functions useful when working
   with canonical encrypted S-expressions (i.e. not the S-exprssion
   objects from libgcrypt).  */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include "util.h"
#include "tlv.h"
#include "sexp-parse.h"
#include "openpgpdefs.h"  /* for pubkey_algo_t */


/* Return a malloced string with the S-expression CANON in advanced
   format.  Returns NULL on error.  */
static char *
sexp_to_string (gcry_sexp_t sexp)
{
  size_t n;
  char *result;

  if (!sexp)
    return NULL;
  n = gcry_sexp_sprint (sexp, GCRYSEXP_FMT_ADVANCED, NULL, 0);
  if (!n)
    return NULL;
  result = (char*) xtrymalloc (n);
  if (!result)
    return NULL;
  n = gcry_sexp_sprint (sexp, GCRYSEXP_FMT_ADVANCED, result, n);
  if (!n)
    BUG ();

  return result;
}


/* Return a malloced string with the S-expression CANON in advanced
   format.  Returns NULL on error.  */
char *
canon_sexp_to_string (const unsigned char *canon, size_t canonlen)
{
  size_t n;
  gcry_sexp_t sexp;
  char *result;

  n = gcry_sexp_canon_len (canon, canonlen, NULL, NULL);
  if (!n)
    return NULL;
  if (gcry_sexp_sscan (&sexp, NULL, (const char*) (canon), n))
    return NULL;
  result = sexp_to_string (sexp);
  gcry_sexp_release (sexp);
  return result;
}


/* Print the canonical encoded S-expression in SEXP in advanced
   format.  SEXPLEN may be passed as 0 is SEXP is known to be valid.
   With TEXT of NULL print just the raw S-expression, with TEXT just
   an empty string, print a trailing linefeed, otherwise print an
   entire debug line. */
void
log_printcanon (const char *text, const unsigned char *sexp, size_t sexplen)
{
  if (text && *text)
    log_debug ("%s ", text);
  if (sexp)
    {
      char *buf = canon_sexp_to_string (sexp, sexplen);
      log_printf ("%s", buf? buf : "[invalid S-expression]");
      xfree (buf);
    }
  if (text)
    log_printf ("\n");
}


/* Print the gcryp S-expression in SEXP in advanced format.  With TEXT
   of NULL print just the raw S-expression, with TEXT just an empty
   string, print a trailing linefeed, otherwise print an entire debug
   line. */
void
log_printsexp (const char *text, gcry_sexp_t sexp)
{
  if (text && *text)
    log_debug ("%s ", text);
  if (sexp)
    {
      char *buf = sexp_to_string (sexp);
      log_printf ("%s", buf? buf : "[invalid S-expression]");
      xfree (buf);
    }
  if (text)
    log_printf ("\n");
}


/* Helper function to create a canonical encoded S-expression from a
   Libgcrypt S-expression object.  The function returns 0 on success
   and the malloced canonical S-expression is stored at R_BUFFER and
   the allocated length at R_BUFLEN.  On error an error code is
   returned and (NULL, 0) stored at R_BUFFER and R_BUFLEN.  If the
   allocated buffer length is not required, NULL by be used for
   R_BUFLEN.  */
gpg_error_t
make_canon_sexp (gcry_sexp_t sexp, unsigned char **r_buffer, size_t *r_buflen)
{
  size_t len;
  unsigned char *buf;

  *r_buffer = NULL;
  if (r_buflen)
    *r_buflen = 0;;

  len = gcry_sexp_sprint (sexp, GCRYSEXP_FMT_CANON, NULL, 0);
  if (!len)
    return GPG_ERR_BUG;
  buf = (unsigned char*) xtrymalloc (len);
  if (!buf)
    return gpg_error_from_syserror ();
  len = gcry_sexp_sprint (sexp, GCRYSEXP_FMT_CANON, buf, len);
  if (!len)
    return GPG_ERR_BUG;

  *r_buffer = buf;
  if (r_buflen)
    *r_buflen = len;

  return 0;
}


/* Same as make_canon_sexp but pad the buffer to multiple of 64
   bits.  If SECURE is set, secure memory will be allocated.  */
gpg_error_t
make_canon_sexp_pad (gcry_sexp_t sexp, int secure,
                     unsigned char **r_buffer, size_t *r_buflen)
{
  size_t len;
  unsigned char *buf;

  *r_buffer = NULL;
  if (r_buflen)
    *r_buflen = 0;;

  len = gcry_sexp_sprint (sexp, GCRYSEXP_FMT_CANON, NULL, 0);
  if (!len)
    return GPG_ERR_BUG;
  len += (8 - len % 8) % 8;
  buf = (unsigned char*) (secure? xtrycalloc_secure (1, len) : xtrycalloc (1, len));
  if (!buf)
    return gpg_error_from_syserror ();
  if (!gcry_sexp_sprint (sexp, GCRYSEXP_FMT_CANON, buf, len))
    return GPG_ERR_BUG;

  *r_buffer = buf;
  if (r_buflen)
    *r_buflen = len;

  return 0;
}

/* Return the so called "keygrip" which is the SHA-1 hash of the
   public key parameters expressed in a way depended on the algorithm.

   KEY is expected to be an canonical encoded S-expression with a
   public or private key. KEYLEN is the length of that buffer.

   GRIP must be at least 20 bytes long.  On success 0 is returned, on
   error an error code. */
gpg_error_t
keygrip_from_canon_sexp (const unsigned char *key, size_t keylen,
                         unsigned char *grip)
{
  gpg_error_t err;
  gcry_sexp_t sexp;

  if (!grip)
    return GPG_ERR_INV_VALUE;
  err = gcry_sexp_sscan (&sexp, NULL, (const char *)key, keylen);
  if (err)
    return err;
  if (!gcry_pk_get_keygrip (sexp, grip))
    err = GPG_ERR_INTERNAL;
  gcry_sexp_release (sexp);
  return err;
}


/* Compare two simple S-expressions like "(3:foo)".  Returns 0 if they
   are identical or !0 if they are not.  Note that this function can't
   be used for sorting. */
int
cmp_simple_canon_sexp (const unsigned char *a_orig,
                       const unsigned char *b_orig)
{
  const char *a = (const char *)a_orig;
  const char *b = (const char *)b_orig;
  unsigned long n1, n2;
  char *endp;

  if (!a && !b)
    return 0; /* Both are NULL, they are identical. */
  if (!a || !b)
    return 1; /* One is NULL, they are not identical. */
  if (*a != '(' || *b != '(')
    log_bug ("invalid S-exp in cmp_simple_canon_sexp\n");

  a++;
  n1 = strtoul (a, &endp, 10);
  a = endp;
  b++;
  n2 = strtoul (b, &endp, 10);
  b = endp;

  if (*a != ':' || *b != ':' )
    log_bug ("invalid S-exp in cmp_simple_canon_sexp\n");
  if (n1 != n2)
    return 1; /* Not the same. */

  for (a++, b++; n1; n1--, a++, b++)
    if (*a != *b)
      return 1; /* Not the same. */
  return 0;
}


/* Create a simple S-expression from the hex string at LINE.  Returns
   a newly allocated buffer with that canonical encoded S-expression
   or NULL in case of an error.  On return the number of characters
   scanned in LINE will be stored at NSCANNED.  This functions stops
   converting at the first character not representing a hexdigit. Odd
   numbers of hex digits are allowed; a leading zero is then
   assumed. If no characters have been found, NULL is returned.*/
unsigned char *
make_simple_sexp_from_hexstr (const char *line, size_t *nscanned)
{
  size_t n, len;
  const char *s;
  unsigned char *buf;
  unsigned char *p;
  char numbuf[50], *numbufp;
  size_t numbuflen;

  for (n=0, s=line; hexdigitp (s); s++, n++)
    ;
  if (nscanned)
    *nscanned = n;
  if (!n)
    return NULL;
  len = ((n+1) & ~0x01)/2;
  numbufp = smklen (numbuf, sizeof numbuf, len, &numbuflen);
  buf = (unsigned char*) xtrymalloc (1 + numbuflen + len + 1 + 1);
  if (!buf)
    return NULL;
  buf[0] = '(';
  p = (unsigned char *)stpcpy ((char *)buf+1, numbufp);
  s = line;
  if ((n&1))
    {
      *p++ = xtoi_1 (s);
      s++;
      n--;
    }
  for (; n > 1; n -=2, s += 2)
    *p++ = xtoi_2 (s);
  *p++ = ')';
  *p = 0; /* (Not really neaded.) */

  return buf;
}


/* Return the hash algorithm from a KSBA sig-val. SIGVAL is a
   canonical encoded S-expression.  Return 0 if the hash algorithm is
   not encoded in SIG-VAL or it is not supported by libgcrypt.  */
int
hash_algo_from_sigval (const unsigned char *sigval)
{
  const unsigned char *s = sigval;
  size_t n;
  int depth;
  char buffer[50];

  if (!s || *s != '(')
    return 0; /* Invalid S-expression.  */
  s++;
  n = snext (&s);
  if (!n)
    return 0; /* Invalid S-expression.  */
  if (!smatch (&s, n, "sig-val"))
    return 0; /* Not a sig-val.  */
  if (*s != '(')
    return 0; /* Invalid S-expression.  */
  s++;
  /* Skip over the algo+parameter list.  */
  depth = 1;
  if (sskip (&s, &depth) || depth)
    return 0; /* Invalid S-expression.  */
  if (*s != '(')
    return 0; /* No further list.  */
  /* Check whether this is (hash ALGO).  */
  s++;
  n = snext (&s);
  if (!n)
    return 0; /* Invalid S-expression.  */
  if (!smatch (&s, n, "hash"))
    return 0; /* Not a "hash" keyword.  */
  n = snext (&s);
  if (!n || n+1 >= sizeof (buffer))
    return 0; /* Algorithm string is missing or too long.  */
  memcpy (buffer, s, n);
  buffer[n] = 0;

  return gcry_md_map_name (buffer);
}


/* Create a public key S-expression for an RSA public key from the
   modulus M with length MLEN and the public exponent E with length
   ELEN.  Returns a newly allocated buffer of NULL in case of a memory
   allocation problem.  If R_LEN is not NULL, the length of the
   canonical S-expression is stored there. */
unsigned char *
make_canon_sexp_from_rsa_pk (const void *m_arg, size_t mlen,
                             const void *e_arg, size_t elen,
                             size_t *r_len)
{
  const unsigned char *m = (const unsigned char*) m_arg;
  const unsigned char *e = (const unsigned char*) e_arg;
  int m_extra = 0;
  int e_extra = 0;
  char mlen_str[35];
  char elen_str[35];
  unsigned char *keybuf, *p;
  const char part1[] = "(10:public-key(3:rsa(1:n";
  const char part2[] = ")(1:e";
  const char part3[] = ")))";

  /* Remove leading zeroes.  */
  for (; mlen && !*m; mlen--, m++)
    ;
  for (; elen && !*e; elen--, e++)
    ;

  /* Insert a leading zero if the number would be zero or interpreted
     as negative.  */
  if (!mlen || (m[0] & 0x80))
    m_extra = 1;
  if (!elen || (e[0] & 0x80))
    e_extra = 1;

  /* Build the S-expression.  */
  snprintf (mlen_str, sizeof mlen_str, "%u:", (unsigned int)mlen+m_extra);
  snprintf (elen_str, sizeof elen_str, "%u:", (unsigned int)elen+e_extra);

  keybuf = (unsigned char*) xtrymalloc (strlen (part1) + strlen (mlen_str) + mlen + m_extra
                       + strlen (part2) + strlen (elen_str) + elen + e_extra
                       + strlen (part3) + 1);
  if (!keybuf)
    return NULL;

  p = (unsigned char*) stpcpy ((char*) (keybuf), part1);
  p = (unsigned char*) stpcpy ((char*) (p), mlen_str);
  if (m_extra)
    *p++ = 0;
  memcpy (p, m, mlen);
  p += mlen;
  p = (unsigned char*) stpcpy ((char*) (p), part2);
  p = (unsigned char*) stpcpy ((char*) (p), elen_str);
  if (e_extra)
    *p++ = 0;
  memcpy (p, e, elen);
  p += elen;
  p = (unsigned char*) stpcpy ((char*) (p), part3);

  if (r_len)
    *r_len = p - keybuf;

  return keybuf;
}


/* Return the parameters of a public RSA key expressed as an
   canonical encoded S-expression.  */
gpg_error_t
get_rsa_pk_from_canon_sexp (const unsigned char *keydata, size_t keydatalen,
                            unsigned char const **r_n, size_t *r_nlen,
                            unsigned char const **r_e, size_t *r_elen)
{
  gpg_error_t err;
  const unsigned char *buf, *tok;
  size_t buflen, toklen;
  int depth, last_depth1, last_depth2;
  const unsigned char *rsa_n = NULL;
  const unsigned char *rsa_e = NULL;
  size_t rsa_n_len, rsa_e_len;

  *r_n = NULL;
  *r_nlen = 0;
  *r_e = NULL;
  *r_elen = 0;

  buf = keydata;
  buflen = keydatalen;
  depth = 0;
  if ((err = parse_sexp (&buf, &buflen, &depth, &tok, &toklen)))
    return err;
  if ((err = parse_sexp (&buf, &buflen, &depth, &tok, &toklen)))
    return err;
  if (!tok || toklen != 10 || memcmp ("public-key", tok, toklen))
    return GPG_ERR_BAD_PUBKEY;
  if ((err = parse_sexp (&buf, &buflen, &depth, &tok, &toklen)))
    return err;
  if ((err = parse_sexp (&buf, &buflen, &depth, &tok, &toklen)))
    return err;
  if (!tok || toklen != 3 || memcmp ("rsa", tok, toklen))
    return GPG_ERR_WRONG_PUBKEY_ALGO;

  last_depth1 = depth;
  while (!(err = parse_sexp (&buf, &buflen, &depth, &tok, &toklen))
         && depth && depth >= last_depth1)
    {
      if (tok)
        return GPG_ERR_UNKNOWN_SEXP;
      if ((err = parse_sexp (&buf, &buflen, &depth, &tok, &toklen)))
        return err;
      if (tok && toklen == 1)
        {
          const unsigned char **mpi;
          size_t *mpi_len;

          switch (*tok)
            {
            case 'n': mpi = &rsa_n; mpi_len = &rsa_n_len; break;
            case 'e': mpi = &rsa_e; mpi_len = &rsa_e_len; break;
            default:  mpi = NULL;   mpi_len = NULL; break;
            }
          if (mpi && *mpi)
            return GPG_ERR_DUP_VALUE;

          if ((err = parse_sexp (&buf, &buflen, &depth, &tok, &toklen)))
            return err;
          if (tok && mpi)
            {
              /* Strip off leading zero bytes and save. */
              for (;toklen && !*tok; toklen--, tok++)
                ;
              *mpi = tok;
              *mpi_len = toklen;
            }
        }

      /* Skip to the end of the list. */
      last_depth2 = depth;
      while (!(err = parse_sexp (&buf, &buflen, &depth, &tok, &toklen))
             && depth && depth >= last_depth2)
        ;
      if (err)
        return err;
    }

  if (err)
    return err;

  if (!rsa_n || !rsa_n_len || !rsa_e || !rsa_e_len)
    return GPG_ERR_BAD_PUBKEY;

  *r_n = rsa_n;
  *r_nlen = rsa_n_len;
  *r_e = rsa_e;
  *r_elen = rsa_e_len;
  return 0;
}


/* Return the algo of a public KEY of SEXP. */
int
get_pk_algo_from_key (gcry_sexp_t key)
{
  gcry_sexp_t list;
  const char *s;
  size_t n;
  char algoname[6];
  int algo = 0;

  list = gcry_sexp_nth (key, 1);
  if (!list)
    goto out;
  s = gcry_sexp_nth_data (list, 0, &n);
  if (!s)
    goto out;
  if (n >= sizeof (algoname))
    goto out;
  memcpy (algoname, s, n);
  algoname[n] = 0;

  algo = gcry_pk_map_name (algoname);
  if (algo == GCRY_PK_ECC)
    {
      gcry_sexp_t l1 = gcry_sexp_find_token (list, "flags", 0);
      int i;

      for (i = l1 ? gcry_sexp_length (l1)-1 : 0; i > 0; i--)
	{
	  s = gcry_sexp_nth_data (l1, i, &n);
	  if (!s)
	    continue; /* Not a data element. */

	  if (n == 5 && !memcmp (s, "eddsa", 5))
	    {
	      algo = GCRY_PK_EDDSA;
	      break;
	    }
	}
      gcry_sexp_release (l1);
    }

 out:
  gcry_sexp_release (list);

  return algo;
}


/* This is a variant of get_pk_algo_from_key but takes an canonical
 * encoded S-expression as input.  Returns a GCRYPT public key
 * identiier or 0 on error.  */
int
get_pk_algo_from_canon_sexp (const unsigned char *keydata, size_t keydatalen)
{
  gcry_sexp_t sexp;
  int algo;

  if (gcry_sexp_sscan (&sexp, NULL, (const char*) (keydata), keydatalen))
    return 0;

  algo = get_pk_algo_from_key (sexp);
  gcry_sexp_release (sexp);
  return algo;
}
