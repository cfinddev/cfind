typedef struct {
	int a;
} foo_t;

static const foo_t *
global_foo(void)
{
	static const foo_t f = {
		.a = 99,
	};
	return &f;
}

static foo_t
make_foo(void)
{
	return (foo_t) {
		.a = 123,
	};
}

int api(const foo_t *f);

int
api(const foo_t *f)
{
	return f->a + global_foo()->a + make_foo().a + ((foo_t) {.a = 5}).a;
}
