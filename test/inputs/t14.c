#include "t14.h"

struct foo {
	int a;
	int b;
};

foo_t *
make_foo(int a)
{
	static foo_t g_f;

	g_f = (foo_t) {
		.a = a,
		.b = a * 2,
	};
	return &g_f;
}
