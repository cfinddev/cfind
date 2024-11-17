// global incomplete type
struct foo;
struct foo *bpi(int a);

int api(int a);
int api(int a) {
	// *new* local type shadows (but does not complete) global type
	struct foo {
		char c;
	};

	// 'warning: incompatible pointer types ...'
	struct foo *f = bpi(a + 1);
	return f->c + a;
}
