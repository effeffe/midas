/********************************************************************\

  Name:         khyt1331.h
  Created by:   Stefan Ritt

  Contents:     IOCTL operation code definitions

  $Id: khyt1331.h 3065 2006-03-28 06:00:41Z ritt $

\********************************************************************/

/* IOCTL operation codes */
#define OP_CRATE_ZINIT         1
#define OP_CRATE_CLEAR         2
#define OP_CRATE_CHECK         3

#define OP_CNAF8              10
#define OP_CNAF16             11
#define OP_CNAF24             12

#define OP_CNAF8R             15
#define OP_CNAF16R            16
#define OP_CNAF24R            17

#define OP_INHIBIT_SET        20
#define OP_INHIBIT_CLEAR      21
#define OP_INHIBIT_TEST       22
#define OP_LAM_ENABLE         23
#define OP_LAM_DISABLE        24
#define OP_LAM_READ           25
#define OP_LAM_CLEAR          26
#define OP_INTERRUPT_ENABLE   27
#define OP_INTERRUPT_DISABLE  28
#define OP_INTERRUPT_TEST     29

/* OP_CNAFxR repeat modes */
#define RM_COUNT               1
#define RM_QMODE               2
#define RM_ASCAN               3
#define RM_NSCAN               4

#ifndef EINAVL
#define EINVAL                22
#endif

typedef struct {
   int c, n, a, f, x, q, r, m;
   unsigned long d;
} CNAF_BUF;
