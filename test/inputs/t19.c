struct foo;
char api(struct foo *sf);
int bar(struct foo *sf);
int
bar(struct foo *sf)
{
	union foo {
		int a;
		char b;
	};
	union foo f = {
		.b = api(sf),
	};
	return f.a;
}
