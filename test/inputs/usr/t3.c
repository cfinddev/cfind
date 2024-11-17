#include "t.h"
#include "t2.h"

int api3(int a);

int
api3(int a)
{
	struct foo f;
	f.a = a;
	f.b = api2(5);
	return api(&f, 10);
}
