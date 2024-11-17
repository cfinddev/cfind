#include "t16.h"
int api(int *a, spinlock_t *lock);
int
api(int *a, spinlock_t *lock)
{
	spin_lock(lock);
	const int save = *a;
	spin_unlock(lock);
	return save;
}
