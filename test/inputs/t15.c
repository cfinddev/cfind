/*
 * A type can be implicitly forward-declared as a part of a typedef.
 * This appears in the AST as two nodes:
 *   RecordDecl struct foo
 *   TypedefDecl foo_t 'struct foo'
 *
 * Note: it's not possible to forward-declare an anonymous type. So
 *   typedef struct foo_t;
 * emits a warning (-Wmissing-declarations) and doesn't create any typedef.
 */
typedef struct foo foo_t;

foo_t *make_foo(int a);
int get_foo(foo_t *f);
int api(int a);

int
api(int a)
{
	return get_foo(make_foo(a));
}
