/*
 * Indexer test.
 * This is the most basic test of anonymous unions and implicit members. The
 * indexer needs to figure out `v1` and `v2` are members of `struct foo`.
 *
 * Also note: the AST viewed from libtooling and libclang differ. The C version
 * doesn't report implicit members or indirect members.
 */

struct foo {
	int sel;
	union {  // <<< full anonymous union decl
		long v1;
		long long v2;
	}; // field only; implicit member name='', type=<anon-union>

	// c++: 2 Indirect field decls, v1, v2
};

int api(struct foo *f);

int
api(struct foo *f)
{
	// member access: (member='v1', parent='struct foo')->long
	f->v1 = 5;
	return f->sel ? (int)f->v1 : (int)f->v2;
}
