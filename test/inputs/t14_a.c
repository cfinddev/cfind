/*
 * The goal here is to be able to tell that the incomplete `foo_t` used in this
 * file is the same struct defined in "t14.c".
 */
#include "t14.h"

int api(int a);

int
api(int a)
{
	foo_t *f = make_foo(a);
	return a + 1;
}
