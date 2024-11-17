typedef enum {
	cond_ok,
	cond_error,
} cond_t;

cond_t classify(int a);
int foo(int a);

int
foo(int a)
{
	// The goal here is to identify temporary variable of type `cond_t`.
	switch (classify(a)) {
		case cond_ok:
			return 5;
		case cond_error:
			return 10;
	}
}
