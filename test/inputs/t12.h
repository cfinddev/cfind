#ifndef LIB_FOO_H
#define LIB_FOO_H
struct foo {
#if defined(GOOD_CODE)
	long a;
#else
	int a;
#endif // GOOD_CODE
};

int api(struct foo *f);

#endif // LIB_FOO_H
