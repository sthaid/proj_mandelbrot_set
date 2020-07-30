// XXX review these again
typedef struct {
    unsigned long bits[6];
} num_t;

void num_init_from_long(num_t *nr, long v);  // XXX long ?
void num_init_from_double(num_t *nr, long double v);
long double num_to_double(num_t *n);
char * num_to_str(char *s, num_t *n);

void num_add(num_t *nr, num_t *n1, num_t *n2) ;
void num_multiply(num_t *nr, num_t *n1, num_t *n2);
void num_multiply2(num_t *nr, num_t *n1, long n2);
