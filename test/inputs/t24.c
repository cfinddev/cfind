/*
 * Indexer test. Extended version of "t23.c" with function example.
 *
 * Anonymous structs are only allowed when nested in struct decls.
 * Other structs at global/function scope that *look* like anonymous structs
 * are in fact 'unnamed with no instances'.
 *
 * Such a struct, and its member variables, must not be indexed. Its children
 * struct decls must be indexed and are allowed to be used elsewhere.
 */

struct { // unnamed with no instances; not indexed
	struct foo { // global scope
		int a;
		int b;
	};
	int c;
	struct { // anonymous
		int a;
		int b;
	};
};

struct bar {
	struct foo f; // declared above
	long c;
};

int api(int a);

int
api(int a)
{
	struct { // unnamed with no instances
		int b;
		int c;
		struct foo { // function scope; shadows global 'foo'
			int c;
			int d;
		};
	};

	struct foo f;
	f.c = a + 1;
	f.d = a + 2;

	return f.c + f.d;
}
