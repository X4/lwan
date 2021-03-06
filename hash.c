/*
 * Based on libkmod-hash.c from libkmod - interface to kernel module operations
 * Copyright (C) 2011-2012  ProFUSION embedded systems
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "hash.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct hash_entry {
	const char *key;
	const void *value;
};

struct hash_bucket {
	struct hash_entry *entries;
	unsigned int used;
	unsigned int total;
};

struct hash {
	unsigned int count;
	unsigned int step;
	unsigned int n_buckets;
	unsigned int (*hash_value)(const void *key, unsigned int len);
	int (*key_compare)(const void *k1, const void *k2, size_t len);
	int (*entry_compare)(const void *e1, const void *e2);
	int (*key_length)(const void *key);
	void (*free_value)(void *value);
	void (*free_key)(void *value);
	struct hash_bucket buckets[];
};

#define get_unaligned(ptr)			\
({						\
	struct __attribute__((packed)) {	\
		typeof(*(ptr)) __v;		\
	} *__p = (typeof(__p)) (ptr);		\
	__p->__v;				\
})

static inline unsigned int hash_int(const void *keyptr, unsigned int len __attribute__((unused)))
{
	/* http://www.concentric.net/~Ttwang/tech/inthash.htm */
	int key = (int)(long)keyptr;
	int c2 = 0x27d4eb2d; // a prime or an odd constant

	key = (key ^ 61) ^ (key >> 16);
	key += key << 3;
	key ^= key >> 4;
	key *= c2;
	key ^= key >> 15;
	return key;
}

static inline unsigned int hash_superfast(const void *keyptr, unsigned int len)
{
	/* Paul Hsieh (http://www.azillionmonkeys.com/qed/hash.html)
	 * used by WebCore (http://webkit.org/blog/8/hashtables-part-2/)
	 * EFL's eina and possible others.
	 */
	unsigned int tmp, hash = len, rem = len & 3;
	const char *key = keyptr;

	len /= 4;

	/* Main loop */
	for (; len > 0; len--) {
		hash += get_unaligned((uint16_t *) key);
		tmp = (get_unaligned((uint16_t *)(key + 2)) << 11) ^ hash;
		hash = (hash << 16) ^ tmp;
		key += 4;
		hash += hash >> 11;
	}

	/* Handle end cases */
	switch (rem) {
	case 3:
		hash += get_unaligned((uint16_t *) key);
		hash ^= hash << 16;
		hash ^= key[2] << 18;
		hash += hash >> 11;
		break;

	case 2:
		hash += get_unaligned((uint16_t *) key);
		hash ^= hash << 11;
		hash += hash >> 17;
		break;

	case 1:
		hash += *key;
		hash ^= hash << 10;
		hash += hash >> 1;
	}

	/* Force "avalanching" of final 127 bits */
	hash ^= hash << 3;
	hash += hash >> 5;
	hash ^= hash << 4;
	hash += hash >> 17;
	hash ^= hash << 25;
	hash += hash >> 6;

	return hash;
}

static int hash_str_entry_cmp(const void *pa, const void *pb)
{
	const struct hash_entry *a = pa;
	const struct hash_entry *b = pb;
	return strcmp(a->key, b->key);
}

static int hash_int_entry_cmp(const void *pa, const void *pb)
{
	const struct hash_entry *a = pa;
	const struct hash_entry *b = pb;
	return (int)(long)(a->key) - (int)(long)(b->key);
}

static inline int hash_int_key_cmp(const void *k1, const void *k2, size_t len __attribute__((unused)))
{
	int a = (int)(long)k1;
	int b = (int)(long)k2;
	return a - b;
}

static int hash_int_length(const void *key __attribute__((unused)))
{
	return sizeof(int);
}

static struct hash *hash_internal_new(unsigned int n_buckets,
					unsigned int (*hash_value)(const void *key, unsigned int len),
					int (*key_compare)(const void *k1, const void *k2, size_t len),
					int (*entry_compare)(const void *e1, const void *e2),
					int (*key_length)(const void *key),
					void (*free_key)(void *value),
					void (*free_value)(void *value))
{
	struct hash *hash = calloc(1, sizeof(struct hash) +
				n_buckets * sizeof(struct hash_bucket));
	if (hash == NULL)
		return NULL;
	hash->n_buckets = n_buckets;
	hash->hash_value = hash_value;
	hash->key_compare = key_compare;
	hash->entry_compare = entry_compare;
	hash->key_length = key_length;
	hash->free_value = free_value;
	hash->free_key = free_key;
	hash->step = n_buckets / 32;
	if (hash->step == 0)
		hash->step = 4;
	else if (hash->step > 64)
		hash->step = 64;
	return hash;
}

struct hash *hash_int_new(unsigned int n_buckets,
					void (*free_key)(void *value),
					void (*free_value)(void *value))
{
	return hash_internal_new(n_buckets,
			hash_int,
			hash_int_key_cmp,
			hash_int_entry_cmp,
			hash_int_length,
			free_key,
			free_value);
}

struct hash *hash_str_new(unsigned int n_buckets,
					void (*free_key)(void *value),
					void (*free_value)(void *value))
{
	return hash_internal_new(n_buckets,
			hash_superfast,
			(int (*)(const void *, const void *, size_t))strncmp,
			hash_str_entry_cmp,
			(int (*)(const void *))strlen,
			free_key,
			free_value);
}

void hash_free(struct hash *hash)
{
	struct hash_bucket *bucket, *bucket_end;

	if (hash == NULL)
		return;

	bucket = hash->buckets;
	bucket_end = bucket + hash->n_buckets;
	for (; bucket < bucket_end; bucket++) {
		if (hash->free_value) {
			struct hash_entry *entry, *entry_end;
			entry = bucket->entries;
			entry_end = entry + bucket->used;
			for (; entry < entry_end; entry++) {
				hash->free_value((void *)entry->value);
				if (hash->free_key)
					hash->free_key((void *)entry->key);
			}
		}
		free(bucket->entries);
	}
	free(hash);
}

/*
 * add or replace key in hash map.
 *
 * none of key or value are copied, just references are remembered as is,
 * make sure they are live while pair exists in hash!
 */
int hash_add(struct hash *hash, const void *key, const void *value)
{
	unsigned int keylen = hash->key_length(key);
	unsigned int hashval = hash->hash_value(key, keylen);
	unsigned int pos = hashval % hash->n_buckets;
	struct hash_bucket *bucket = hash->buckets + pos;
	struct hash_entry *entry, *entry_end;

	if (bucket->used + 1 >= bucket->total) {
		unsigned new_total = bucket->total + hash->step;
		size_t size = new_total * sizeof(struct hash_entry);
		struct hash_entry *tmp = realloc(bucket->entries, size);
		if (tmp == NULL)
			return -errno;
		bucket->entries = tmp;
		bucket->total = new_total;
	}

	entry = bucket->entries;
	entry_end = entry + bucket->used;
	for (; entry < entry_end; entry++) {
		int c = hash->key_compare(key, entry->key, keylen);
		if (c == 0) {
			if (hash->free_value)
				hash->free_value((void *)entry->value);
			entry->value = value;
			return 0;
		} else if (c < 0) {
			memmove(entry + 1, entry,
				(entry_end - entry) * sizeof(struct hash_entry));
			break;
		}
	}

	entry->key = key;
	entry->value = value;
	bucket->used++;
	hash->count++;
	return 0;
}

/* similar to hash_add(), but fails if key already exists */
int hash_add_unique(struct hash *hash, const void *key, const void *value)
{
	unsigned int keylen = hash->key_length(key);
	unsigned int hashval = hash->hash_value(key, keylen);
	unsigned int pos = hashval % hash->n_buckets;
	struct hash_bucket *bucket = hash->buckets + pos;
	struct hash_entry *entry, *entry_end;

	if (bucket->used + 1 >= bucket->total) {
		unsigned new_total = bucket->total + hash->step;
		size_t size = new_total * sizeof(struct hash_entry);
		struct hash_entry *tmp = realloc(bucket->entries, size);
		if (tmp == NULL)
			return -errno;
		bucket->entries = tmp;
		bucket->total = new_total;
	}

	entry = bucket->entries;
	entry_end = entry + bucket->used;
	for (; entry < entry_end; entry++) {
		int c = hash->key_compare(key, entry->key, keylen);
		if (c == 0)
			return -EEXIST;
		else if (c < 0) {
			memmove(entry + 1, entry,
				(entry_end - entry) * sizeof(struct hash_entry));
			break;
		}
	}

	entry->key = key;
	entry->value = value;
	bucket->used++;
	hash->count++;
	return 0;
}

static inline struct hash_entry *hash_find_entry(const struct hash *hash,
								const char *key,
								unsigned int hashval,
								unsigned int keylen)
{
	unsigned int pos = hashval % hash->n_buckets;
	const struct hash_bucket *bucket = hash->buckets + pos;
	size_t lower_bound = 0;
	size_t upper_bound = bucket->used;

	while (lower_bound < upper_bound) {
		size_t idx = (lower_bound + upper_bound) / 2;
		const struct hash_entry *ptr = bucket->entries + idx;
		int cmp = hash->key_compare(key, ptr->key, keylen);
		if (!cmp)
			return (void *)ptr;
		if (cmp > 0)
			lower_bound = idx + 1;
		else
			upper_bound = idx;
	}

	return NULL;
}

void *hash_find(const struct hash *hash, const void *key)
{
	const struct hash_entry *entry;
	unsigned int keylen = hash->key_length(key);

	entry = hash_find_entry(hash, key, hash->hash_value(key, keylen), keylen);
	if (entry)
		return (void *)entry->value;
	return NULL;
}

int hash_del(struct hash *hash, const void *key)
{
	unsigned int keylen = hash->key_length(key);
	unsigned int hashval = hash->hash_value(key, keylen);
	unsigned int pos = hashval % hash->n_buckets;
	unsigned int steps_used, steps_total;
	struct hash_bucket *bucket = hash->buckets + pos;
	struct hash_entry *entry, *entry_end;

	entry = hash_find_entry(hash, key, hashval, keylen);
	if (entry == NULL)
		return -ENOENT;

	if (hash->free_value)
		hash->free_value((void *)entry->value);
	if (hash->free_key)
		hash->free_key((void *)entry->key);

	entry_end = bucket->entries + bucket->used;
	memmove(entry, entry + 1,
		(entry_end - entry) * sizeof(struct hash_entry));

	bucket->used--;
	hash->count--;

	steps_used = bucket->used / hash->step;
	steps_total = bucket->total / hash->step;
	if (steps_used + 1 < steps_total) {
		size_t size = (steps_used + 1) *
			hash->step * sizeof(struct hash_entry);
		struct hash_entry *tmp = realloc(bucket->entries, size);
		if (tmp) {
			bucket->entries = tmp;
			bucket->total = (steps_used + 1) * hash->step;
		}
	}

	return 0;
}

unsigned int hash_get_count(const struct hash *hash)
{
	return hash->count;
}

void hash_iter_init(const struct hash *hash, struct hash_iter *iter)
{
	iter->hash = hash;
	iter->bucket = 0;
	iter->entry = -1;
}

bool hash_iter_next(struct hash_iter *iter, const void **key,
							const void **value)
{
	const struct hash_bucket *b = iter->hash->buckets + iter->bucket;
	const struct hash_entry *e;

	iter->entry++;

	if (iter->entry >= b->used) {
		iter->entry = 0;

		for (iter->bucket++; iter->bucket < iter->hash->n_buckets;
							iter->bucket++) {
			b = iter->hash->buckets + iter->bucket;

			if (b->used > 0)
				break;
		}

		if (iter->bucket >= iter->hash->n_buckets)
			return false;
	}

	e = b->entries + iter->entry;

	if (value != NULL)
		*value = e->value;
	if (key != NULL)
		*key = e->key;

	return true;
}
