// example input
// nested anonymous types get added to the current named parent
struct foo {
	struct n1 {
		struct {
			int foo_i1;
		};
	} n;
	struct {
		struct {
			int foo_i1; // different from `struct n1::foo_i1`
		};
		int foo_i2;
	};
	int foo_i3;
};
