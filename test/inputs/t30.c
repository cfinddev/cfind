/*
 * Test nested named structures are added to global scope even when they're
 * nested within anonymous structures.
 */
struct foo {
	struct { // anon
		int a;
		struct ex1 { // global
			char a;
			char b;
			struct { // anon
				struct ex2 { // global
					long a;
				} c;
			};
		};
		struct ex1 b;
		int c;
	};
};

static struct foo g_f;
static struct ex1 g_e1;
static struct ex2 g_e2;
