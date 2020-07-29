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

void num_multiply(num_t *nr, num_t *n1, num_t *n2) 
{
    unsigned long *bi = n1->bits;
    unsigned long *bj = n2->bits;
    unsigned long carry;
    int i, j;
    num_t r[6];
    
    memset(r,0,sizeof(r));

    for (i = 0; i < 6; i++) {
        carry = 0;
        for (j = 0; j < 6; j++) {
            unsigned __int128 tmp128;
            unsigned long tmp_high, tmp_low;

            if (i + j < 5) continue;

            tmp128   = (__int128)bi[i] * bj[j] + carry;
            tmp_high = (tmp128 >> 64);  // XXX remove tmp_high/low
            tmp_low  = tmp128;

            r[i].bits[i+j-5] = tmp_low;
            carry = tmp_high;
        }

        if (i+j-5 <= 5) {
            r[i].bits[i+j-5] = carry;
        }
    }

    memset(nr,0,sizeof(num_t));
    for (i = 0; i < 6; i++) {
        //XXX char s[200];
        //XXX printf("i=%d - %s\n", i, num_to_str(s,&r[i]));
        num_add(nr, nr, &r[i]);
    }
}

// ----------------------------------------------------------------------

#ifdef UNIT_TEST

// XXX clean this up
int main()
{
    num_t nr, n1, n2;
    char s[200];
    long double dbl;

    dbl = 1.9999;
    num_init_from_double(&n1, dbl);
    printf("%Lg = %s\n", dbl, num_to_str(s,&n1));

    dbl = -0.000001;
    num_init_from_double(&n1, dbl);
    printf("%Lg = %s\n", dbl, num_to_str(s,&n1));

    memset(&n1, 0xff, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n2.bits[0] = 2;
    num_add(&nr, &n1, &n2);
    printf("nr = %s\n", num_to_str(s,&nr));
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));

    memset(&n1, 0, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.bits[5] = 1;
    n1.bits[4] = 0x8000000000000000;
    n2.bits[5] = 2;
    n2.bits[4] = 0xc000000000000000;
    num_multiply(&nr, &n1, &n2);
    printf("nr = %s\n", num_to_str(s,&nr));
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));

    memset(&n1, 0xff, sizeof(n1));
    memset(&n2, 0, sizeof(n2));
    n1.bits[5] = 0;
    n2.bits[5] = 7;
    num_multiply(&nr, &n1, &n2);
    printf("nr = %s\n", num_to_str(s,&nr));
    printf("n1 = %s\n", num_to_str(s,&n1));
    printf("n2 = %s\n", num_to_str(s,&n2));


    return 0;
}
#endif
