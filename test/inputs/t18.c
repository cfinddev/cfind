typedef struct {
	long a;
} foo_t;

typedef struct {
	foo_t *fp;
	long c;
} bar_t;

typedef struct {
	foo_t f;
	bar_t b;
} glue_t;

void make_glue(glue_t *out);
void
make_glue(glue_t *out)
{
	*out = (glue_t) {
		.f = {
			.a = 1,
		},
		.b = {
			.fp = &out->f,
			.c = 2,
		},
	};
}
