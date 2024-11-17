#include "t2.h"

struct foo {
	char a1;
	long b2;
};

void undef(struct foo *f);

int
api2(int a)
{
	struct foo f;
	undef(&f);

	int b;
	{
		struct foo {
			long a;
			long b;
		} l;
		l.a = f.a1 > 1 ? 0 : 1;
		l.b = f.b2;
		b = l.a + l.b;
	}
	return a + f.a1 + f.b2 + b;
}
