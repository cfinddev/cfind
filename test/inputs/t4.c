typedef struct {
	union {
		struct {
			int aaa;
			char bbb;
		} a;
		char b[5];
	};
} foo_t;

static foo_t
make_foo(int a)
{
	return (foo_t) {
		.a = {
			.aaa = a + 1,
			.bbb = 0,
		},
	};
}

static void
mutate_foo(foo_t *f)
{
	// The goal is to be able to find mutation of `foo_t`s anonymous union.
	// With only frontend information, it may be hard to tell `f->b[4]` aliases
	// `f->a.bbb`.
	f->b[4] = 1;
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
