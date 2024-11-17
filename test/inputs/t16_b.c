#include ".//////t16.h"
long bpi(long *a, spinlock_t *lock);
long
bpi(long *a, spinlock_t *lock)
{
	spin_lock(lock);
	const long save = *a;
	spin_unlock(lock);
	return save;
}
