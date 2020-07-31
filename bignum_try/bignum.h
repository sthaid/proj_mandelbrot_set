// XXX header ifdefs

typedef struct {
    unsigned long bits[6];
} num_t;

void num_init(num_t *nr, long double v);
long double num_value(num_t *n);
char * num_to_str(char *s, num_t *n);

void num_negate(num_t *n);
void num_lshift(num_t *n, int count);

void num_add(num_t *nr, num_t *n1, num_t *n2) ;

void num_multiply(num_t *nr, num_t *n1, num_t *n2);
void num_multiply2(num_t *nr, num_t *n1, long n2);
