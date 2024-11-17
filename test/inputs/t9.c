// multiple incomplete definitions are allowed
struct foo;
struct foo;

struct foo {
	int a;
	int b;
};

void *xmalloc(unsigned long);

struct foo *api(int a);
struct foo *api(int a) {
	struct foo {
		char c;
	};

	struct foo f = {
		.c = a + 1,
	};
	if (f.c > 1)  {
		// NOTE: sizeof() is 1; it uses the local `foo` definition
		// the return value is cast to the global `foo` version
		return xmalloc(sizeof(struct foo));
	}
	return (void *)0;
}
