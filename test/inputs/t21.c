/*
 * Harder version of t20.c
 * Multiple recursively nested anonymous types add IndirectFields to their
 * ancestor type.
 */
struct foo {
	int sel;
	union {
		struct {
			long v1;
			long long v2;
		};
		struct {
			int v3;
			int v4;
		};
	};

	// c++: 4 Indirect field decls, v1..v4
};

int api(struct foo *f);

int
api(struct foo *f)
{
	f->v1 = 1;
	f->v2 = 2;
	f->v3 = 3;
	f->v4 = 4;
	return f->sel ? (int)f->v1 : (int)f->v2;
}

