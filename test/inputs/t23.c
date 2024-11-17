/*
 * Indexer test.
 *
 * Unnamed structs with no instances shouldn't be indexed. But children
 * RecordDecls (`struct foo` below) must be.
 *
 * A bad indexer implementation could remove `struct foo` just because its
 * parent has no instances.
 */
struct {
	int a_a;
	struct foo {
		int f_a;
		int f_b;
	} f;
};

int api(struct foo *f);

int
api(struct foo *f)
{
	return f->f_a + f->f_b;
}
