/* LibTomCrypt, modular cryptographic library -- Tom St Denis
 *
 * LibTomCrypt is a library that provides various cryptographic
 * algorithms in a highly modular and flexible manner.
 *
 * The library is free for all purposes without any express
 * guarantee it works.
 */

/**
   @file ocb_shift_xor.c
   OCB implementation, internal function, by Tom St Denis
*/
#include "tomcrypt_private.h"

#ifdef LTC_OCB_MODE

/**
   Compute the shift/xor for OCB (internal function)
   @param ocb  The OCB state
   @param Z    The destination of the shift
*/
void ocb_shift_xor(ocb_state *ocb, unsigned char *Z)
{
   int x, y;
   y = ocb_ntz(ocb->block_index++);
   for (x = 0; x < ocb->block_len; x++) {
       ocb->Li[x] ^= ocb->Ls[y][x];
       Z[x]        = ocb->Li[x] ^ ocb->R[x];
   }
}

#endif

/* ref:         HEAD -> develop */
/* git commit:  e01e4c5c972ba5337d7ab897173fde6e5f0dd046 */
/* commit time: 2019-06-11 07:55:21 +0200 */
