typedef struct {
	int (*read)(int fd, void *buf, unsigned long nbytes);
} file_ops_t;

// The goal is to identify global scope `file_ops_t` instances.
static const file_ops_t nop_fs = {0};

int install_ops(int fs_id, const file_ops_t *f);
int api(void);

int
api(void)
{
	return install_ops(1, &nop_fs);
}
