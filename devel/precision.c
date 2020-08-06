// gcc -o precision -Wall precision.c

#include <stdio.h>

int main()
{
    float       w = (long double)1./3;
    double      x = (long double)1./3;
    long double y = (long double)1./3;

    printf("%0.30f\n",  w);
    printf("%0.30lf\n", x);
    printf("%0.30Lf\n", y);

    return 0;
}
