// Wikipedia says ...
//   set of complec numbers c for which the function
//     Fc(z) = z^2 + c
//   does not diverge when iterated from z = 0

#include <stdio.h>
#include <math.h>
#include <complex.h>

#define MIN_IMAG   -1.5
#define MAX_IMAG    1.5
#define STEP_IMAG   0.05

#define MIN_REAL   -2.0
#define MAX_REAL    0.75
#define STEP_REAL   0.015

#define MAX_ITER   1000

int mandelbrot_iterations(complex c);

int main(int argc, char **argv)
{
    double real, imag;
    int    iter;

    for (imag = MAX_IMAG; imag > MIN_IMAG; imag -= STEP_IMAG) {
        for (real = MIN_REAL; real < MAX_REAL; real += STEP_REAL) {
            iter = mandelbrot_iterations(real + imag * I);
            printf("%c", (iter >= MAX_ITER ? '*' : ' '));
        }
        printf("\n");
    }

    return 0;

}

int mandelbrot_iterations(complex c)
{
    complex z = 0;
    int i;

    for (i = 0; i < MAX_ITER; i++) {
        z = z * z + c;
        if (cabs(z) >= 2) {
            break;
        }
    }

    return i;
}
