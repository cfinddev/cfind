extern int foo;
struct s1 {
};
struct s2 {
	int a;
};
struct s3 {
	int a;
	struct ex1 {
	};
};
struct s4 {
	int a;
	struct ex2 {
	};
	struct {
	} b;
};
struct s5 {
	int a;
	struct ex3 {
		int b;
	};
	struct {
	} b;
	struct {
	};
};
struct s6 {
	int a;
	struct ex4 {
		int b;
	};
};
struct s7 {
	int a;
	struct ex5 {
		int b;
		struct {
			int c;
		};
	};
};
struct s8 {
	int a;
	struct ex6 {
		int b;
		struct {
			int c;
		};
	};
	struct {
		int d;
		struct {
			int e;
		};
	} f;
	int g;
};
static struct {
	int a;
} s9;
typedef struct {
	int a;
	struct ex7 {
		int b;
		struct {
			struct {
				int c;
			};
		} d;
	} e;
	int f;
} s10;
struct {
	int a;
};
struct {
	struct ex8 {
		int a;
	};
};
static struct ex8 s11;
