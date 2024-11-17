typedef struct foo {
	int a;
} foo_t;

typedef struct bar {
	foo_t f;
} bar_t;

typedef union ufoo {
	int a;
	foo_t f;
} ufoo_t;

struct foo *make_foo(int a);
foo_t *make_foo_t(int a);

int yes_index_init(int a);
int no_index_init(int a);

static const foo_t f_global = {
	.a = 123,
};

// index everything in here
int
yes_index_init(int a)
{
	struct foo yes1 = {
		.a = a,
	};

	foo_t yes2 = {
		.a = a,
	};

	struct foo yes3[1] = {0};

	foo_t yes4[1] = {0};

	struct foo yes5 = (struct foo) {
		.a = a,
	};

	struct foo yes6 = (foo_t) {
		.a = a,
	};

	foo_t yes7 = (foo_t) {
		.a = a,
	};

	foo_t yes8 = (struct foo) {
		.a = a,
	};

	bar_t yes9 = {
		.f = {
			.a = a,
		},
	};

	bar_t yes10[1] = {
		[0] = {
			.f = {
				.a = a,
			},
		 },
	};

	bar_t yes11 = (bar_t) {
		.f = {
			.a = a,
		},
	};

	union ufoo yes12 = {
		.a = a,
	};

	union ufoo yes13 = {
		.f = {
			.a = a,
		},
	};

	ufoo_t yes14 = {
		.a = a,
	};

	ufoo_t yes15 = {
		.f = {
			.a = a,
		},
	};

	union ufoo yes16[1] = {
		[0] = {
			.f = {
				.a = a,
			},
		},
	};

	union ufoo yes17[1] = {
		[0] = {
			.f = {
				.a = a,
			},
		},
	};

	return f_global.a + yes1.a + yes2.a + yes3[0].a + yes4[0].a + yes5.a +
			yes6.a + yes7.a + yes8.a + yes9.f.a + yes10[0].f.a + yes11.f.a +
			yes12.a + yes13.f.a + yes14.a + yes15.f.a + yes16[0].f.a +
			yes17[0].f.a;
}

// index *nothing* in here
int
no_index_init(int a)
{
	int no1 = {a};

	struct foo *no2 = make_foo(a);

	foo_t *no3 = make_foo_t(a);

	int no4[] = {
		a,
	};

	struct foo *no5[1] = {
		no2,
	};

	foo_t *no6[1] = {
		no3,
	};

	return no1 + no2->a + no3->a + no4[0] + no5[0]->a + no6[0]->a;
}
