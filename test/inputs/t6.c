struct foo {
	int a;
};

struct bar {
	// NOTE: typedefs cannot appear in struct decls
	// typedef struct foo foo_t;
	struct foo f;
	int b;
};

int bpi(const struct bar *b);
int api(int a);

int
api(int a)
{
	// NOTE: typedefs *can* appear in function bodies
	// they're confined to the scope of the function
	typedef struct bar bar_t;
	bar_t b = {
		.f = {
			.a = a + 1,
		},
		.b = a + 2,
	};
	return bpi(&b);
}
