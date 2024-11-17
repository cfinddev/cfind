void *xmalloc(unsigned long size);
void xfree(void *buf);

typedef struct {
	int a;
} foo_t;

static foo_t *
make_foo(int a)
{
	foo_t *f = xmalloc(sizeof(foo_t));
	f->a = a;
	return f;
}

static int
consume_foo(foo_t *f)
{
	const int ret = f->a + 1;
	xfree(f);
	return ret;
}

int api(int a);

int
api(int a)
{
	// Here, the goal is to identify the use of `foo_t` even though it isn't
	// explicitly written in the source.
	return consume_foo(make_foo(a));
}
