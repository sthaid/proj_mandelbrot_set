#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <util_misc.h>
#include <bignum.h>

#define SIGN_BIT (1L << 63)

// -----------------  NUM INIT AND VALUE CONVERSIONS  -------------------

void num_init(num_t *nr, long double v)
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
    if (fraction != 0 && nr->bits[4] == 0) {
        FATAL("fraction=%0.20Lf nr->bits[4]=0x%lx\n", fraction, nr->bits[4]);
    }

    if (isneg) {
        nr->bits[5] = ~nr->bits[5];
        nr->bits[4] = ~nr->bits[4];
    }
}

long double num_value(num_t *n)
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

    dbl = num_value(n);
    sprintf(s, "%0.15Lf  0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx", 
            dbl, n->bits[5], n->bits[4], n->bits[3], n->bits[2], n->bits[1], n->bits[0]);
    return s;
}

// -----------------  MISC  ------------------------------------

void num_negate(num_t *n)
{
    int i;

    for (i = 0; i < 6; i++) {
        n->bits[i] = ~n->bits[i];
    }
}

void num_lshift(num_t *n, int count)
{
    int i;
    unsigned long bits, copy_bits;

    if (count < 0 || count > 63) {
        FATAL("count=%d\n", count);
    }

    bits = 0;
    for (i = 0; i < 6; i++) {
        copy_bits = n->bits[i] >> (64-count);
        n->bits[i] = (n->bits[i] << count) | bits;
        bits = copy_bits;
    }
}

// -----------------  ADDITION  -----------------------------------------

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

    *nr = result;
}

// -----------------  MULTIPLICATION  -----------------------------------

void num_multiply(num_t *nr, num_t *n1_arg, num_t *n2_arg) 
{
    unsigned long carry;
    int i, j;
    bool isneg_n1, isneg_n2;
    num_t n1, n2, r[6];
    
    memset(r,0,sizeof(r));

    n1 = *n1_arg;
    n2 = *n2_arg;

    isneg_n1 = (n1.bits[5] & SIGN_BIT);
    isneg_n2 = (n2.bits[5] & SIGN_BIT);

    if (isneg_n1) {
        num_negate(&n1);
    }
    if (isneg_n2) {
        num_negate(&n2);
    }

    for (i = 0; i < 6; i++) {
        carry = 0;
        for (j = 0; j < 6; j++) {
            unsigned __int128 tmp128;

            if (i + j < 5) continue;

            tmp128   = (__int128)n1.bits[i] * n2.bits[j] + carry;
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

    if (isneg_n1 != isneg_n2) {
        num_negate(nr);
    }
}

// XXX make both numbers positive here too, like above routine
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

    if (isneg) {
        num_negate(nr);
    }
}

// -----------------  UNIT TEST  ----------------------------------------

#ifdef UNIT_TEST

// build for UNIT_TEST:
//   gcc -Wall -DUNIT_TEST -I../util -I. -O2 -o bignum_unit_test -lm bignum.c ../util/util_misc.c

int main()
{
    num_t nr, n1, n2;
    long n2_long;
    int shift_count;
    char s[200];
    long double dbl;

    printf("\ntest num_init...\n");
    dbl = 1.9999;
    num_init(&n1, dbl);
    printf("%Lg = %s\n", dbl, num_to_str(s,&n1));

    printf("\ntest num_init...\n");
    dbl = -0.000001;
    num_init(&n1, dbl);
    printf("%Lg = %s\n", dbl, num_to_str(s,&n1));

    printf("\ntest num_init...\n");
    dbl = -21.75;
    num_init(&n1, dbl);
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

    printf("\ntest num_negate and num_add...\n");
    num_init(&n1, 0);
    num_init(&n2, 1);
    num_negate(&n2);
    num_add(&nr, &n1, &n2);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));
    printf("nr = %s\n", num_to_str(s,&nr));
    
    printf("\ntest num_lshift...\n");
    num_init(&n1, 1.55);
    n1.bits[3] = 0xf000000000000000;
    nr = n1;
    shift_count = 1;
    num_lshift(&nr, shift_count);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("shift_count = %d\n", shift_count);
    printf("nr = %s\n", num_to_str(s,&nr));

    printf("XXX\n");
    memset(&n1, 0xff, sizeof(n1));
    n1.bits[0] = 0xfffffffffffffffe;
    num_multiply(&nr, &n1, &n1);
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("nr = %s\n", num_to_str(s,&nr));

    return 0;
}
#endif
