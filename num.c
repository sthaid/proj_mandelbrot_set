// XXX code was reviewed
// XXX should long double or double be used

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "num.h"

#define SIGN_BIT (1L << 63)

// ----------------------------------------------------------------------

void num_init_from_long(num_t *nr, long v)
{
    memset(nr,0,sizeof(num_t));
    nr->bits[5] = v;
}

void num_init_from_double(num_t *nr, long double v)
{
    bool isneg = v < 0;
    unsigned long integer;
    long double fraction;

    if (isneg) {
        v = -v;
    }

    integer = v;
    fraction = v - integer;

    memset(nr,0,sizeof(num_t));
    nr->bits[5] = integer;
    nr->bits[4] = fraction * ((long double)UINT64_MAX + 1);
    //XXX assert [4]  if fraction then [4] must not be 0

    if (isneg) {
        nr->bits[5] = ~nr->bits[5];
        nr->bits[4] = ~nr->bits[4];
    }
}

long double num_to_double(num_t *n)
{
    bool isneg;
    unsigned long integer;
    unsigned long fraction;
    long double dbl;

    isneg = (n->bits[5] & SIGN_BIT);

    if (!isneg) {
        integer  = n->bits[5];
        fraction = n->bits[4];
    } else {
        integer  = ~n->bits[5];
        fraction = ~n->bits[4];
    }

    dbl = integer + fraction / ((long double)UINT64_MAX + 1);
    if (isneg) {
        dbl = - dbl;
    }

    return dbl;
}

char * num_to_str(char *s, num_t *n)
{
    long double dbl;

    dbl = num_to_double(n);
    sprintf(s, "%0.15Lf  0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx", 
            dbl, n->bits[5], n->bits[4], n->bits[3], n->bits[2], n->bits[1], n->bits[0]);
    return s;
}

// ----------------------------------------------------------------------

void num_add(num_t *nr, num_t *n1, num_t *n2) 
{
    int i;
    num_t result;
    bool carry = false;

    for (i = 0; i < 6; i++) {
        if (!carry) {
            result.bits[i] = n1->bits[i] + n2->bits[i];
            carry = (result.bits[i] < n1->bits[i]);
        } else {
            result.bits[i] = n1->bits[i] + n2->bits[i];
            carry = (result.bits[i] < n1->bits[i]);
            result.bits[i]++;
            if (result.bits[i] == 0) carry = true;
        }
    }

    memcpy(nr, &result, sizeof(num_t));
}

// ----------------------------------------------------------------------

void num_multiply(num_t *nr, num_t *n1, num_t *n2_arg) 
{
    unsigned long carry;
    int i, j;
    bool isneg;
    num_t n2, r[6];
    
    memset(r,0,sizeof(r));

    isneg = (n2_arg->bits[5] & SIGN_BIT);
    
    memcpy(&n2, n2_arg, sizeof(num_t));
    if (isneg) { //XXX need a routine
        for (i = 0; i < 6; i++) {
            n2.bits[i] = ~n2.bits[i];
        }
    }

    for (i = 0; i < 6; i++) {
        carry = 0;
        for (j = 0; j < 6; j++) {
            unsigned __int128 tmp128;

            if (i + j < 5) continue;

            tmp128   = (__int128)n1->bits[i] * n2.bits[j] + carry;
            r[i].bits[i+j-5] = tmp128;
            carry = (tmp128 >> 64);
        }

        if (i+j-5 <= 5) {
            r[i].bits[i+j-5] = carry;
        }
    }

    memset(nr,0,sizeof(num_t));
    for (i = 0; i < 6; i++) {
        num_add(nr, nr, &r[i]);
    }

    if (isneg) { //XXX need a routine
        for (i = 0; i < 6; i++) {
            nr->bits[i] = ~nr->bits[i];
        }
    }
}

void num_multiply2(num_t *nr, num_t *n1, long n2) 
{
    unsigned long carry;
    unsigned __int128 tmp128;
    int i;
    bool isneg = (n2 < 0);
    
    if (isneg) {
        n2 = -n2;
    }
    
    carry = 0;
    for (i = 0; i < 6; i++) {
        tmp128 = (__int128)n1->bits[i] * n2 + carry;
        nr->bits[i] = tmp128;
        carry = (tmp128 >> 64);
    }

    if (isneg) { //XXX need a routine
        for (i = 0; i < 6; i++) {
            nr->bits[i] = ~nr->bits[i];
        }
    }
}

// ----------------------------------------------------------------------

#ifdef UNIT_TEST

// build for UNIT_TEST:
//   gcc -Wall -DUNIT_TEST -O2 -o num -lm num.c

int main()
{
    num_t nr, n1, n2;
    long n2_long;
    char s[200];
    long double dbl;

    printf("\ntest num_init_from_double...\n");
    dbl = 1.9999;
    num_init_from_double(&n1, dbl);
    printf("%Lg = %s\n", dbl, num_to_str(s,&n1));

    printf("\ntest num_init_from_double...\n");
    dbl = -0.000001;
    num_init_from_double(&n1, dbl);
    printf("%Lg = %s\n", dbl, num_to_str(s,&n1));

    printf("\ntest num_init_from_double...\n");
    dbl = -21.75;
    num_init_from_double(&n1, dbl);
    printf("%Lg = %s\n", dbl, num_to_str(s,&n1));

    printf("\ntest num_add ...\n");
    memset(&n1, 0xff, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n2.bits[0] = 2;
    num_add(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_add ...\n");
    memset(&n1, 0xff, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n2.bits[5] = -9999;
    n2.bits[4] = 0x8000000000000000;
    num_add(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply ...\n");
    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.bits[5] = 1;
    n1.bits[4] = 0x8000000000000000;
    n2.bits[5] = 2;
    n2.bits[4] = 0xc000000000000000;
    num_multiply(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply ...\n");
    memset(&n1, 0xff, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.bits[5] = 0;
    n2.bits[5] = 7;
    num_multiply(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply ...\n");
    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.bits[5] = 22;
    n1.bits[4] = 0x8000000000000000;
    n1.bits[3] = 0x8000000000000000;
    n1.bits[2] = 0x8000000000000000;
    n1.bits[1] = 0x8000000000000000;
    n1.bits[0] = 0x8000000000000000;
    n2.bits[5] = 3;
    n2.bits[4] = 0;
    n2.bits[3] = 0x8000000000000000;
    n2.bits[2] = 0x8000000000000000;
    n2.bits[1] = 0x8000000000000000;
    n2.bits[0] = 0x8000000000000000;
    num_multiply(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply ...\n");
    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.bits[5] = 22;
    n1.bits[4] = 0x8000000000000000;
    n1.bits[3] = 0x8000000000000000;
    n1.bits[2] = 0x8000000000000000;
    n1.bits[1] = 0x8000000000000000;
    n1.bits[0] = 0x8000000000000000;
    n2.bits[5] = -3;
    n2.bits[4] = 0;
    n2.bits[3] = 0;
    n2.bits[2] = 0;
    n2.bits[1] = 0;
    n2.bits[0] = 0;
    num_multiply(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply ...\n");
    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.bits[5] = -22;
    n1.bits[4] = 0x4000000000000000;
    n1.bits[3] = 0x8000000000000000;
    n1.bits[2] = 0x8000000000000000;
    n1.bits[1] = 0x8000000000000000;
    n1.bits[0] = 0x8000000000000000;
    n2.bits[5] = -3;
    n2.bits[4] = 0;
    n2.bits[3] = 0;
    n2.bits[2] = 0;
    n2.bits[1] = 0;
    n2.bits[0] = 0;
    num_multiply(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply ...\n");
    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.bits[5] = -22;
    n1.bits[4] = 0x4000000000000000;
    n1.bits[3] = 0x8000000000000000;
    n1.bits[2] = 0x8000000000000000;
    n1.bits[1] = 0x8000000000000000;
    n1.bits[0] = 0x8000000000000000;
    n2.bits[5] = 3;
    n2.bits[4] = 0;
    n2.bits[3] = 0;
    n2.bits[2] = 0;
    n2.bits[1] = 0;
    n2.bits[0] = 0;
    num_multiply(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply2 ...\n");
    memset(&n1, 0, sizeof(n1));
    n1.bits[5] = 1;
    n1.bits[4] = 0xf000000000000000;
    n1.bits[3] = 0x8000000000000000;
    n1.bits[2] = 0x8000000000000000;
    n1.bits[1] = 0x8000000000000000;
    n1.bits[0] = 0x8000000000000000;
    n2_long = -3;
    num_multiply2(&nr, &n1, n2_long);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %ld\n", n2_long);
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply2 ...\n");
    memset(&n1, 0, sizeof(n1));
    n1.bits[5] = 1;
    n1.bits[4] = 0xf000000000000000;
    n1.bits[3] = 0x8000000000000000;
    n1.bits[2] = 0x8000000000000000;
    n1.bits[1] = 0x8000000000000000;
    n1.bits[0] = 0x8000000000000000;
    n2_long = 3;
    num_multiply2(&nr, &n1, n2_long);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %ld\n", n2_long);
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply2 ...\n");
    memset(&n1, 0, sizeof(n1));
    n1.bits[5] = -1;
    n1.bits[4] = 0xf000000000000000;
    n1.bits[3] = 0x8000000000000000;
    n1.bits[2] = 0x8000000000000000;
    n1.bits[1] = 0x8000000000000000;
    n1.bits[0] = 0x8000000000000000;
    n2_long = 3;
    num_multiply2(&nr, &n1, n2_long);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %ld\n", n2_long);
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("\ntest num_multiply2 ...\n");
    memset(&n1, 0, sizeof(n1));
    n1.bits[5] = -1;
    n1.bits[4] = 0xf000000000000000;
    n1.bits[3] = 0x8000000000000000;
    n1.bits[2] = 0x8000000000000000;
    n1.bits[1] = 0x8000000000000000;
    n1.bits[0] = 0x8000000000000000;
    n2_long = -3;
    num_multiply2(&nr, &n1, n2_long);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %ld\n", n2_long);
    printf("nr = %s\n", num_to_str(s,&nr));

    return 0;
}
#endif



#if 0  //XXX
07/29/20 16:20:09.500 INFO pane_hndlr: ZOOM pixel_size 0.000029296875000  0x0 0x1eb851eb851eb 0x8000000000000000 0x0 0x0 0x0
07/29/20 16:20:09.508 INFO pane_hndlr:    step1 B = 0.999970703125000  0x0 0xfffe147ae147ae14 0x8000000000000000 0x0 0x0 0x0
07/29/20 16:20:09.508 INFO pane_hndlr:    step2 B = 0.999970703125000  0x0 0xfffe147ae147ae14 0x8000000000000000 0x0 0x0 0x0
    memset(&n1, 0, sizeof(n1));
    n1.bits[5] = 0;
    n1.bits[4] = 0x1eb851eb851eb;
    n1.bits[3] = 0x8000000000000000;
    n2_long = -1; 
    num_multiply2(&nr, &n1, n2_long);
    printf("nr = %s\n", num_to_str(s,&nr));
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %ld\n", n2_long);

    return 0;
#endif
