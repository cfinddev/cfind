typedef struct {
	int a;
} foo_t;

static foo_t
make_foo(int a)
{
	return (foo_t) {
		.a = a + 1,
	};
}

static void
mutate_foo(foo_t *f)
{
	// The goal is to be able to find access to `foo_t::a` even though there's
	// other variables named `a`.
	f->a += 1;
}

static void
opaque(foo_t *f)
{
	mutate_foo(f);
}

int bpi(const foo_t *f);
int api(int a);

int
api(int a)
{
	foo_t f = make_foo(a);
	opaque(&f);
	return bpi(&f);
}
