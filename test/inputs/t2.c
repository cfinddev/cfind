typedef struct {
	int a;
} foo_t;

typedef struct {
	int a;
	foo_t f;
} bar_t;

int bpi(const bar_t *b);
int api(int a);

int
api(int a)
{
	bar_t b = {
		.a = a,
		// The goal here is to identify the initialization of `foo_t`.
		.f = {
			.a = a * 2,
		},
	};
	return bpi(&b);
}
