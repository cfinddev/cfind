/*
 * The test case here uses files ["t16.h", "t16_a.c", "t16_b.c"].
 * The goal is to recognize that a single header file is included from two TUs,
 * despite each one using a different FS path.
 */
#pragma once
typedef struct {
	unsigned long word;
} spinlock_t;

void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
