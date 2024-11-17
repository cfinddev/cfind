int
main(int argc, char **argv)
{
	(void)argv;
	struct foo {
		// Note: `struct bar` is declared in the scope of main() even though it
		// appears in the AST nested under `struct foo`.
		struct bar {
			int c;
		} c3;
		int a;
		int b;
		struct bar c1;
		struct bar c2;
	};

	struct foo a;
	a.a = argc + 1;
	a.c3.c = a.a + 2;

	struct foo b;
	b.b = argc * 2;
	b.c1.c = b.b + 3;

	struct bar b2;
	b2.c = 99;

	return a.a > (b.c1.c + b2.c);
}
