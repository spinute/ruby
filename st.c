/* This is a public domain general purpose hash table package written by Peter Moore @ UCB. */

/* static	char	sccsid[] = "@(#) st.c 5.1 89/12/14 Crucible"; */

#ifdef NOT_RUBY
#include "regint.h"
#include "st.h"
#else
#include "internal.h"
#endif

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>
#include "ccan/list/list.h"

#ifndef ST_UNUSED
#if defined(__GNUC__)
#  define ST_UNUSED  __attribute__((unused))
#else
#  define ST_UNUSED
#endif
#endif

#ifdef ST_DEBUG
#  define st_assert assert
#else
#  define st_assert(...)
#endif

typedef struct st_table_entry st_table_entry;

struct st_table_entry {
    st_idx_t hash;
    st_idx_t next;
    st_data_t key;
    st_data_t record;
};

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(name, expr) typedef int static_assert_##name##_check[(expr) ? 1 : -1]
#endif

    /*
     * DEFAULT_MAX_DENSITY is the default for the largest we allow the
     * average number of items per bin before increasing the number of
     * bins
     *
     * DEFAULT_INIT_TABLE_SIZE is the default for the number of bins
     * allocated initially
     *
     */

#define type_numhash st_hashtype_num
const struct st_hash_type st_hashtype_num = {
    st_numcmp,
    st_numhash,
};

/* extern int strcmp(const char *, const char *); */
static st_index_t strhash(st_data_t);
static const struct st_hash_type type_strhash = {
    strcmp,
    strhash,
};

static st_index_t strcasehash(st_data_t);
static const struct st_hash_type type_strcasehash = {
    st_locale_insensitive_strcasecmp,
    strcasehash,
};

static void st_rehash(st_table *);

#ifdef RUBY
#define malloc ruby_xmalloc
#define calloc ruby_xcalloc
#define sized_realloc(p, new, old) ruby_sized_xrealloc((p), (new), (old))
#define sized_free(p, sz) ruby_sized_xfree((p), (sz))
#else
#error "NOT RUBY"
#define sized_realloc(p, new, old) realloc((p), (new))
#define sized_free(p, sz) free(p)
#endif

#define IDX_NULL (~(st_idx_t)0)
#define IDX_FILL 0xff
#define DELETED (0)
#define Z ((ssize_t)-1)

/* preparation for possible allocation improvements */
#define st_alloc_table() (st_table *)calloc(1, sizeof(st_table))
#define st_dealloc_table(table) sized_free(table, sizeof(st_table))

/* this calculation to satisfy jemalloc/tcmalloc allocation sizes */
static struct st_sizes {
    st_idx_t nentries, nbins;
} const st_sz[] = {
    { 0, 0 },
    { 4, 0 }, { 8, 0 },
/* include sizes generated by following script:
   st_idx_t_sz = ARGV[0].to_i
   st_data_t_sz = ARGV[1].to_i
   size_t_log = st_data_t_sz < 8 ? 31 : 47 # ptrdiff_t should not overflow
   entry_sz = 2 * st_data_t_sz + 2 * st_idx_t_sz
   def memsizes(log); a = 256; while a+a/2 <= 2**log; yield a; yield a+a/2; a*=2; end; end
   def pow2(v); [1,2,4,8,16].each{|i| v|=v>>i}; v+1; end
   sizes = []
   memsizes(size_t_log) do |v|
      nentries = v/entry_sz
      nbins = pow2(nentries <= 256 ? nentries : nentries - 1)
      nentries = (v - nbins * (nentries <= 256 ? 1 : (nentries <= (1<<16) ? 2 : st_idx_t_sz))) / entry_sz
      next if nentries <= 10 || nentries >= (2**(8*st_idx_t_sz)-1) || nbins >= (2**(8*st_idx_t_sz)-1)
      sizes << [nentries, nbins]
   end
   (sizes.size-1).downto(1){|i| if sizes[i][0] * 0.85 < sizes[i-1][0] then sizes.delete_at(i) end }
   sizes.each_slice(2){|a| s=a.map{|m,k| "{ #{m}, #{k} },"}.join(" "); print "    #{s}\n" }
*/
#if SIZEOF_VOIDP == 8 && SIZEOF_ST_IDX_T == 4
#   include "st_4_8.inc"
#elif SIZEOF_VOIDP < 8
#   include "st_4_4.inc"
#else
#   include "st_8_8.inc"
#endif
    { 0, 0 }
};

static inline int
need_shrink(st_idx_t num_entries, unsigned sz)
{
    st_idx_t nen = st_sz[sz].nentries;
    return sz > 1 && num_entries < nen / 2;
}

static inline st_idx_t
do_hash(st_data_t key, const st_table * table)
{
    st_idx_t h = (st_idx_t)(*table->type->hash)(key);
    return h != DELETED ? h : 0x71fe900d;
}
#define hash_pos(h,sz) ((h) & (st_sz[sz].nbins-1))

static inline ssize_t
bin_size(int sz)
{
    st_idx_t nen = st_sz[sz].nentries;
    return nen < 256 ? 1 : (nen < 65536 ? 2 : sizeof(st_idx_t));
}

static inline ssize_t
bins_size(int sz)
{
    return (ssize_t)st_sz[sz].nbins * bin_size(sz);
}

static inline size_t
entries_size(int sz)
{
    return bins_size(sz) + st_sz[sz].nentries*sizeof(st_table_entry);
}

static inline char*
base_ptr(st_table_entry* e, int sz)
{
    return (char*)e - bins_size(sz);
}

static inline st_table_entry*
entries_ptr(char* e, int sz)
{
    return (st_table_entry*)(e + bins_size(sz));
}

static st_idx_t fake_bins[2] = { IDX_NULL, IDX_NULL };

static inline void
st_free_data(st_table_entry* entries, int sz)
{
    if (sz != 0) {
	sized_free(base_ptr(entries, sz), entries_size(sz));
    }
}

static inline st_table_entry*
st_grow_data(st_table_entry *e, int newsz, int oldsz)
{
    if (oldsz == 0) {
	if (newsz != 0) {
	    char* bins = calloc(1, entries_size(newsz));
#ifdef NOTRUBY
	    if (bins == NULL) abort();
#endif
	    memset(bins, IDX_FILL, bins_size(newsz));
	    return entries_ptr(bins, newsz);
	} else {
	    return (st_table_entry*)(fake_bins + 1);
	}
    } else if (newsz == 0) {
	sized_free(base_ptr(e, oldsz), entries_size(oldsz));
	return (st_table_entry*)(fake_bins + 1);
    } else {
	char* vbins = base_ptr(e, oldsz);
	ssize_t bins_old = bins_size(oldsz);
	ssize_t bins_new = bins_size(newsz);
	if (bins_old > bins_new) {
	    MEMMOVE(vbins + bins_new, vbins + bins_old, st_table_entry, st_sz[newsz].nentries);
	    memset(vbins, IDX_FILL, bins_new);
	}
	vbins = sized_realloc(vbins, entries_size(newsz), entries_size(oldsz));
#ifdef NOTRUBY
	if (bins == NULL) abort();
#endif
	if (bins_old < bins_new) {
	    MEMMOVE(vbins + bins_new, vbins + bins_old, st_table_entry, st_sz[oldsz].nentries);
	    memset(vbins, IDX_FILL, bins_new);
	}
	return entries_ptr(vbins, newsz);
    }
}

#ifdef HASH_LOG
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
static struct {
    int all, total, num, str, strcase;
}  collision;
static int init_st = 0;

static void
stat_col(void)
{
    char fname[10+sizeof(long)*3];
    FILE *f;
    if (!collision.total) return;
    f = fopen((snprintf(fname, sizeof(fname), "/tmp/col%ld", (long)getpid()), fname), "w");
    fprintf(f, "collision: %d / %d (%6.2f)\n", collision.all, collision.total,
	    ((double)collision.all / (collision.total)) * 100);
    fprintf(f, "num: %d, str: %d, strcase: %d\n", collision.num, collision.str, collision.strcase);
    fclose(f);
}
#endif

static int
new_sz(st_idx_t size)
{
    int i;
    for (i = 1; st_sz[i].nentries != 0; i++)
        if (st_sz[i].nentries >= size)
            return i;
#ifndef NOT_RUBY
    rb_raise(rb_eRuntimeError, "st_table too big");
#endif
    return -1;			/* should raise exception */
}

st_table*
st_init_table_with_size(const struct st_hash_type *type, st_index_t size)
{
    st_table *tbl;

#ifdef HASH_LOG
# if HASH_LOG+0 < 0
    {
	const char *e = getenv("ST_HASH_LOG");
	if (!e || !*e) init_st = 1;
    }
# endif
    if (init_st == 0) {
	init_st = 1;
	atexit(stat_col);
    }
#endif

    tbl = st_alloc_table();
    tbl->type = type;
    tbl->num_entries = 0;
    /* XXX: assert(size <= st_idx_max) is needed? */
    tbl->sz = new_sz((st_idx_t) size);	/* round up to power-of-two */
    tbl->as.entries = st_grow_data(NULL, tbl->sz, 0);
    tbl->rebuild_num = 0;
    tbl->first = 0;
    tbl->last = 0;

    return tbl;
}

st_table*
st_init_table(const struct st_hash_type *type)
{
    return st_init_table_with_size(type, 0);
}

st_table*
st_init_numtable(void)
{
    return st_init_table(&type_numhash);
}

st_table*
st_init_numtable_with_size(st_index_t size)
{
    return st_init_table_with_size(&type_numhash, size);
}

st_table*
st_init_strtable(void)
{
    return st_init_table(&type_strhash);
}

st_table*
st_init_strtable_with_size(st_index_t size)
{
    return st_init_table_with_size(&type_strhash, size);
}

st_table*
st_init_strcasetable(void)
{
    return st_init_table(&type_strcasehash);
}

st_table*
st_init_strcasetable_with_size(st_index_t size)
{
    return st_init_table_with_size(&type_strcasehash, size);
}

void
st_clear(st_table *table)
{
    st_free_data(table->as.entries, table->sz);
    table->sz = 0;
    table->num_entries = 0;
    table->as.bins = &fake_bins[1];
    table->rebuild_num++;
    table->first = 0;
    table->last = 0;
}

void
st_free_table(st_table *table)
{
    st_clear(table);
    st_dealloc_table(table);
}

size_t
st_memsize(const st_table *table)
{
    return sizeof(st_table) + entries_size(table->sz);
}

static inline st_idx_t
bin_get(const st_table *table, st_idx_t bin_pos)
{
    st_idx_t idx;
    if (st_sz[table->sz].nentries < 256) {
	idx = table->as.smallbins[Z-bin_pos];
	if (idx == 0xff) idx = IDX_NULL;
    } else if (st_sz[table->sz].nentries < 65536) {
	idx = table->as.medbins[Z-bin_pos];
	if (idx == 0xffff) idx = IDX_NULL;
    } else {
	idx = table->as.bins[Z-bin_pos];
    }
    return idx;
}

static inline st_idx_t
bin_get_ptr(const st_table *table, st_idx_t bin_pos, ptrdiff_t *pidx)
{
    st_idx_t idx;
    if (st_sz[table->sz].nentries < 256) {
	idx = table->as.smallbins[Z-bin_pos];
	*pidx = (char*)&table->as.smallbins[Z-bin_pos] - (char*)table->as.bins;
	if (idx == 0xff) idx = IDX_NULL;
    } else if (st_sz[table->sz].nentries < 65536) {
	idx = table->as.medbins[Z-bin_pos];
	*pidx = (char*)&table->as.medbins[Z-bin_pos] - (char*)table->as.bins;
	if (idx == 0xffff) idx = IDX_NULL;
    } else {
	idx = table->as.bins[Z-bin_pos];
	*pidx = (char*)&table->as.bins[Z-bin_pos] - (char*)table->as.bins;
    }
    return idx;
}

static inline void
bin_set(const st_table *table, st_idx_t bin_pos, st_idx_t idx)
{
    if (st_sz[table->sz].nentries < 256) {
	table->as.smallbins[Z-bin_pos] = (uint8_t)idx;
    } else if (st_sz[table->sz].nentries < 65536) {
	table->as.medbins[Z-bin_pos] = (uint16_t)idx;
    } else {
	table->as.bins[Z-bin_pos] = idx;
    }
}

static inline void
set_idx(const st_table *table, ptrdiff_t pidx, st_idx_t idx)
{
    if (pidx < 0 && st_sz[table->sz].nentries < 256) {
	*(uint8_t*)((char*)table->as.bins + pidx) = (uint8_t)idx;
    } else if (pidx < 0 && st_sz[table->sz].nentries < 65536) {
	*(uint16_t*)((char*)table->as.bins + pidx) = (uint16_t)idx;
    } else {
	*(st_idx_t*)((char*)table->as.bins + pidx) = idx;
    }
}

#ifdef HASH_LOG
static void
count_collision(const struct st_hash_type *type)
{
    collision.all++;
    if (type == &type_numhash) {
	collision.num++;
    }
    else if (type == &type_strhash) {
	collision.strcase++;
    }
    else if (type == &type_strcasehash) {
	collision.str++;
    }
}
#define COLLISION (collision_check ? count_collision(table->type) : (void)0)
#define FOUND_ENTRY (collision_check ? collision.total++ : (void)0)
#define collision_check 0
#else
#define COLLISION
#define FOUND_ENTRY
#endif

static inline int
EQUAL(const st_table *table, st_data_t key, st_table_entry* ptr) {
    return key == ptr->key || (*table->type->compare)(key, ptr->key) == 0;
}
static inline int
PTR_NOT_EQUAL(const st_table *table, st_idx_t idx, st_idx_t hash_val, st_data_t key)
{
    if (idx != IDX_NULL) {
	    st_table_entry *ptr = &table->as.entries[idx];
	    return ptr->hash != hash_val || !EQUAL(table, key, ptr);
    } else {
	    return 0;
    }
}

static inline st_idx_t
find_entry(const st_table *table, st_data_t key, st_idx_t hash_val)
{
    unsigned rebuild_num ST_UNUSED = table->rebuild_num;
    if (st_sz[table->sz].nbins == 0) {
	st_idx_t idx = table->first, last = table->last;
	st_table_entry* ptr = &table->as.entries[idx];
	for (; idx < last; idx++, ptr++) {
	    if (ptr->hash == hash_val && EQUAL(table, key, ptr)) {
		return idx;
	    }
	}
	st_assert(rebuild_num == table->rebuild_num);
	return IDX_NULL;
    } else {
	st_idx_t bin_pos = hash_pos(hash_val, table->sz);
	st_idx_t idx = bin_get(table, bin_pos);
	FOUND_ENTRY;
	while (PTR_NOT_EQUAL(table, idx, hash_val, key)) {
	    COLLISION;
	    idx = table->as.entries[idx].next;
	}
	st_assert(rebuild_num == table->rebuild_num);
	return idx;
    }
}

static inline st_idx_t
find_entry_ptr(const st_table *table, st_data_t key, st_idx_t hash_val, ptrdiff_t *pidx)
{
    unsigned rebuild_num ST_UNUSED = table->rebuild_num;
    if (st_sz[table->sz].nbins == 0) {
	st_idx_t idx = table->first, last = table->last;
	st_table_entry* ptr = &table->as.entries[idx];
	*pidx = 0;
	for (; idx < last; idx++, ptr++) {
	    if (ptr->hash == hash_val && EQUAL(table, key, ptr)) {
		return idx;
	    }
	}
	st_assert(rebuild_num == table->rebuild_num);
	return IDX_NULL;
    } else {
	st_idx_t bin_pos = hash_pos(hash_val, table->sz);
	st_idx_t idx = bin_get_ptr(table, bin_pos, pidx);
	FOUND_ENTRY;
	while (PTR_NOT_EQUAL(table, idx, hash_val, key)) {
	    COLLISION;
	    *pidx = (char*)&table->as.entries[idx].next - (char*)table->as.bins;
	    idx = table->as.entries[idx].next;
	}
	st_assert(rebuild_num == table->rebuild_num);
	return idx;
    }
}

static st_idx_t
find_exact_key(const st_table *table, st_data_t key, st_idx_t hash_val)
{
    st_idx_t idx;
    st_table_entry* ptr;
    if (st_sz[table->sz].nbins == 0) {
	idx = table->first;
	ptr = table->as.entries + idx;
	while (idx < table->last && (ptr->hash != hash_val || ptr->key != key))
	    idx++;
	return idx < table->last ? idx : IDX_NULL;
    } else {
	st_idx_t bin_pos = hash_pos(hash_val, table->sz);
	idx = bin_get(table, bin_pos);
	while (idx != IDX_NULL) {
	    ptr = table->as.entries + idx;
	    if (ptr->hash == hash_val && ptr->key == key) {
		break;
	    }
	    idx = ptr->next;
	}
	return idx;
    }
}

int
st_lookup(st_table *table, register st_data_t key, st_data_t *value)
{
    st_idx_t hash_val = do_hash(key, table);
    st_idx_t i = find_entry(table, key, hash_val);

    if (i == IDX_NULL) {
	return 0;
    }
    else {
	if (value != 0) *value = table->as.entries[i].record;
	return 1;
    }
}

int
st_get_key(st_table *table, register st_data_t key, st_data_t *result)
{
    st_idx_t hash_val = do_hash(key, table);
    st_idx_t i = find_entry(table, key, hash_val);

    if (i == IDX_NULL) {
	return 0;
    }
    else {
	if (result != 0)  *result = table->as.entries[i].key;
	return 1;
    }
}

#undef collision_check
#define collision_check 1

static inline void
add_direct(st_table *table, st_data_t key, st_data_t value,
	   st_idx_t hash_val)
{
    register st_table_entry *entry;
    st_idx_t en_idx, bin_pos;
    if (table->last == st_sz[table->sz].nentries) {
        st_rehash(table);
    }

    en_idx = table->last;
    table->last++;
    entry = &table->as.entries[en_idx];
    if (st_sz[table->sz].nbins != 0) {
        bin_pos = hash_pos(hash_val, table->sz);
	entry->next = bin_get(table, bin_pos);
	bin_set(table, bin_pos, en_idx);
    }
    entry->hash = hash_val;
    entry->key = key;
    entry->record = value;
    table->num_entries++;
}

int
st_insert(register st_table *table, register st_data_t key, st_data_t value)
{
    st_idx_t hash_val, idx;

    hash_val = do_hash(key, table);
    idx = find_entry(table, key, hash_val);

    if (idx == IDX_NULL) {
	add_direct(table, key, value, hash_val);
	return 0;
    }
    else {
	table->as.entries[idx].record = value;
	return 1;
    }
}

int
st_insert2(register st_table *table, register st_data_t key, st_data_t value,
	   st_data_t (*func)(st_data_t))
{
    st_idx_t hash_val, idx;
    unsigned rebuild_num ST_UNUSED = table->rebuild_num;

    hash_val = do_hash(key, table);
    idx = find_entry(table, key, hash_val);

    if (idx == IDX_NULL) {
	key = (*func)(key);
	st_assert(rebuild_num == table->rebuild_num);
	add_direct(table, key, value, hash_val);
	return 0;
    }
    else {
	table->as.entries[idx].record = value;
	return 1;
    }
}

void
st_add_direct(st_table *table, st_data_t key, st_data_t value)
{
    st_idx_t hash_val = do_hash(key, table);
    add_direct(table, key, value, hash_val);
}

#ifdef ST_DEBUG
static void
st_check_table(st_table *table) {
    if (st_sz[table->sz].nbins > 0) {
	st_idx_t idx = table->first;
	st_idx_t bin_mask = st_sz[table->sz].nbins - 1;
	st_table_entry* ptr = table->as.entries + table->first;
	st_table_entry* end = table->as.entries + table->last;
	for (; ptr != end; idx++, ptr++) {
	    if (ptr->hash != DELETED) {
		st_idx_t head = bin_get(table, (ptr->hash&bin_mask));
		while (head != idx) head = table->as.entries[head].next;
	    }
	}
    }
}
#else
#define st_check_table(x)
#endif

static void
st_reclaim_without_bins(register st_table *table)
{
    st_idx_t i;
    st_table_entry *ptr, *optr;
    
    ptr = table->as.entries;
    optr = &table->as.entries[table->first];
    for (i = table->last - table->first; i; i--, optr++) {
	if (optr->hash != DELETED) {
	    if (ptr != optr) *ptr = *optr;
	    ptr++;
	}
    }

    table->first = 0;
    /* XXX: ptr-table->as.entries never overflow as st_idx_t? */
    table->last = (st_idx_t) (ptr - table->as.entries);
}

static void
st_fix_bins(st_table *table)
{
    if (st_sz[table->sz].nbins != 0) {
	st_idx_t bin_mask;
	st_idx_t i, bin_pos;
	st_table_entry* ptr;

	bin_mask = st_sz[table->sz].nbins - 1;
	memset(base_ptr(table->as.entries, table->sz), IDX_FILL, bins_size(table->sz));
	ptr = table->as.entries + table->first;
	for (i = table->first; i < table->last; i++, ptr++) {
	    if (ptr->hash != DELETED) {
		bin_pos = ptr->hash & bin_mask;
		ptr->next = bin_get(table, bin_pos);
		bin_set(table, bin_pos, i);
	    }
	}
	st_check_table(table);
    }
}

static void
st_shrink(st_table *table)
{
    unsigned sz = table->sz;
    while (need_shrink(table->num_entries, sz))
	sz--;
    sz++;
    table->as.entries = st_grow_data(table->as.entries, sz, table->sz);
    table->sz = sz;
}

static void
st_rehash(register st_table *table)
{
    table->rebuild_num++;
    if (need_shrink(table->num_entries, table->sz)) {
	st_reclaim_without_bins(table);
	if (need_shrink(table->num_entries, table->sz-1)) {
	    st_shrink(table);
	}
	if (st_sz[table->sz].nbins != 0) {
	    st_fix_bins(table);
	}
	return;
    }

    /* else grow table */
    if (st_sz[table->sz + 1].nentries == 0) {
#ifndef NOT_RUBY
	rb_raise(rb_eRuntimeError, "hash is too big");
#else
	abort();
#endif
    }

    table->as.entries = st_grow_data(table->as.entries, table->sz+1, table->sz);
    table->sz++;

    if (bins_size(table->sz) != bins_size(table->sz-1)) {
	st_fix_bins(table);
    }
}

st_table*
st_copy(st_table *old_table)
{
    st_table *new_table;

    new_table = st_alloc_table();
    *new_table = *old_table;

    if (old_table->as.entries) {
	new_table->as.entries = malloc(entries_size(new_table->sz));
	memcpy(new_table->as.entries, base_ptr(old_table->as.entries, old_table->sz),
		entries_size(new_table->sz));
	new_table->as.entries = entries_ptr((char*)new_table->as.entries, new_table->sz);
    }

    return new_table;
}

static inline void
remove_entry(st_table *table, st_idx_t idx)
{
    st_table_entry *ptr;

    table->num_entries--;
    ptr = &table->as.entries[idx];
    ptr->hash = DELETED;
    ptr->key = ptr->record = 0;
    if (table->first == idx) {
	table->first++;
	ptr++;
	while (table->first < table->last && ptr->hash == DELETED) {
	    table->first++;
	    ptr++;
	}
    }
}

static inline void
fix_deleted_bin(st_table *table, st_idx_t idx)
{
    st_idx_t cur, hash;
    ptrdiff_t prev;
    if (st_sz[table->sz].nbins != 0) {
	hash = table->as.entries[idx].hash;
	cur = bin_get_ptr(table, hash_pos(hash, table->sz), &prev);
	while (cur != idx) {
	    prev = (char*)&table->as.entries[cur].next - (char*)table->as.bins;
	    cur = table->as.entries[cur].next;
	}
	set_idx(table, prev, table->as.entries[cur].next);
    }
}

int
st_delete(register st_table *table, register st_data_t *key, st_data_t *value)
{
    st_idx_t hash_val, idx;
    register st_table_entry *ptr;
    unsigned rebuild_num ST_UNUSED = table->rebuild_num;
    ptrdiff_t pidx;

    hash_val = do_hash(*key, table);
    idx = find_entry_ptr(table, *key, hash_val, &pidx);

    if (idx != IDX_NULL) {
	ptr = &table->as.entries[idx];
	if (value != 0) *value = ptr->record;
	*key = ptr->key;
	remove_entry(table, idx);
	if (pidx != 0) {
	    set_idx(table, pidx, ptr->next);
	}
	st_assert(rebuild_num == table->rebuild_num);
	return 1;
    }

    if (value != 0) *value = 0;
    return 0;
}

int
st_delete_safe(register st_table *table, register st_data_t *key, st_data_t *value, st_data_t never ST_UNUSED)
{
    return st_delete(table, key, value);
}

int
st_shift(register st_table *table, register st_data_t *key, st_data_t *value)
{
    st_table_entry *ptr;
    st_idx_t idx;

    if (table->num_entries == 0) {
        if (value != 0) *value = 0;
        return 0;
    }

    idx = table->first;
    ptr = &table->as.entries[idx];
    st_assert(ptr->hash != DELETED);
    if (value != 0) *value = ptr->record;
    *key = ptr->key;
    fix_deleted_bin(table, idx);
    remove_entry(table, idx);
    return 1;
}

void
st_cleanup_safe(st_table *table, st_data_t never ST_UNUSED)
{
    st_idx_t head;
    if (table->sz == 0)
	return;
    if (need_shrink(table->num_entries, table->sz-1)) {
	st_rehash(table);
	return;
    }
    head = table->first;
    while (head < table->last && table->as.entries[head].hash == DELETED)
	head++;
    table->first = head;
}

int
st_update(st_table *table, st_data_t key, st_update_callback_func *func, st_data_t arg)
{
    st_idx_t hash_val, idx;
    register st_table_entry *ptr;
    unsigned rebuild_num ST_UNUSED = table->rebuild_num;
    ptrdiff_t pidx;
    st_data_t value = 0, old_key;
    int retval, existing = 0;

    hash_val = do_hash(key, table);
    idx = find_entry_ptr(table, key, hash_val, &pidx);

    if (idx != IDX_NULL) {
	ptr = &table->as.entries[idx];
	key = ptr->key;
	value = ptr->record;
	existing = 1;
    }
    old_key = key;
    retval = (*func)(&key, &value, arg, existing);
    /*
     * Note, that previously this code wrote into deallocated
     * memory (valgrind see it), but usually still worked.
	   a = {}; 10.times{|i| a[i] = i}; b = a.dup
	   a.merge!(b){|k,v,w| a.delete[k]; a[k+10]=v}
     * now this code will fail
     */
    assert(rebuild_num == table->rebuild_num);
    switch (retval) {
      case ST_CONTINUE:
	if (!existing) {
	    add_direct(table, key, value, hash_val);
	    break;
	}
	if (old_key != key) {
	    ptr->key = key;
	}
	ptr->record = value;
	break;
      case ST_DELETE:
	if (!existing) break;
	if (ptr->hash != DELETED) {
	    if (pidx != 0) {
		set_idx(table, pidx, ptr->next);
	    }
	    remove_entry(table, idx);
	}
	break;
    }
    return existing;
}

int
st_foreach(st_table *table, int (*func)(ANYARGS), st_data_t arg)
{
    st_table_entry *ptr = 0;
    enum st_retval retval;
    unsigned rebuild_num = table->rebuild_num;
    st_idx_t idx, hash;
    st_data_t key;

    ptr = table->as.entries + table->first;
    for (idx = table->first; idx < table->last; idx++, ptr++) {
	if (ptr->hash != DELETED) {
	    key = ptr->key;
	    hash = ptr->hash;
	    retval = (*func)(key, ptr->record, arg, 0);
	    if (rebuild_num != table->rebuild_num) {
		/* call func with error notice */
		idx = find_exact_key(table, key, hash);
		if (idx == IDX_NULL) {
		    /* case for st_foreach_safe */
		    assert(retval == ST_CHECK); /* explicitely not st_assert */
		    (*func)(0, 0, arg, 1);
		    st_check_table(table);
		    return 1;
		} else {
		    rebuild_num = table->rebuild_num;
		    ptr = &table->as.entries[idx];
		}
	    }
	    switch (retval) {
	      case ST_CHECK:	/* check if hash is modified during iteration */
	        break;
	      case ST_CONTINUE:
		break;
	      case ST_STOP:
		return 0;
	      case ST_DELETE:
		st_assert(table->rebuild_num == rebuild_num);
		if (ptr->hash != DELETED) {
		    fix_deleted_bin(table, idx);
		    remove_entry(table, idx);
		}
		if (table->num_entries == 0) return 0;
	    }
	}
    }
    return 0;
}


int
st_foreach_check(st_table *table, int (*func)(ANYARGS), st_data_t arg, st_data_t never ST_UNUSED)
{
    return st_foreach(table, func, arg);
}

static st_index_t
get_keys(const st_table *table, st_data_t *keys, st_index_t size)
{
    st_data_t *keys_start = keys;
    st_data_t *keys_end = keys + size;
    
    st_table_entry *ptr = table->as.entries + table->first;
    st_table_entry *end = table->as.entries + table->last;

    for (; ptr != end; ptr++) {
	if (ptr->hash != DELETED) {
	    if (keys >= keys_end) break;
	    *keys++ = ptr->key;
	}
    }

    return keys - keys_start;
}

st_index_t
st_keys(st_table *table, st_data_t *keys, st_index_t size)
{
    return get_keys(table, keys, size);
}

st_index_t
st_keys_check(st_table *table, st_data_t *keys, st_index_t size,
	st_data_t never ST_UNUSED)
{
    return get_keys(table, keys, size);
}

static st_index_t
get_values(const st_table *table, st_data_t *values, st_index_t size)
{
    st_data_t *values_start = values;
    st_data_t *values_end = values + size;

    st_table_entry *ptr = table->as.entries + table->first;
    st_table_entry *end = table->as.entries + table->last;

    for (; ptr != end; ptr++) {
	if (ptr->hash != DELETED) {
	    if (values >= values_end) break;
	    *values++ = ptr->record;
	}
    }

    return values - values_start;
}

st_index_t
st_values(st_table *table, st_data_t *values, st_index_t size)
{
    return get_values(table, values, size);
}

st_index_t
st_values_check(st_table *table, st_data_t *values, st_index_t size,
	st_data_t never ST_UNUSED)
{
    return get_values(table, values, size);
}


static st_index_t st_seed[2];
static int st_need_seed = 1;
void
st_hash_seed(st_index_t seed[2])
{
    assert(st_need_seed);
    st_seed[0] = seed[0];
    st_seed[1] = seed[1];
    st_need_seed = 0;
}

#define FNV1_32A_INIT 0x811c9dc5

/*
 * 32 bit magic FNV-1a prime
 */
#define FNV_32_PRIME 0x01000193

#ifndef UNALIGNED_WORD_ACCESS
# if defined(__i386) || defined(__i386__) || defined(_M_IX86) || \
     defined(__x86_64) || defined(__x86_64__) || defined(_M_AMD64) || \
     defined(__powerpc64__) || \
     defined(__mc68020__)
#   define UNALIGNED_WORD_ACCESS 1
# endif
#endif
#ifndef UNALIGNED_WORD_ACCESS
# define UNALIGNED_WORD_ACCESS 0
#endif

/* MurmurHash described in https://en.wikipedia.org/wiki/MurmurHash */
/* 32bit is almost exactly MurmurHash3,
 * 64bit version is extrapolation of 32bit version */
#define BIG_CONSTANT(x,y) ((st_index_t)(x)<<32|(st_index_t)(y))
#define ROTL(x,n) ((x)<<(n)|(x)>>(SIZEOF_ST_INDEX_T*CHAR_BIT-(n)))

static inline st_index_t
murmur_step(st_index_t h, st_index_t k)
{
#if ST_INDEX_BITS <= 32
#define r1 (15)
#define r2 (13)
    const st_index_t c1 = 0xcc9e2d51;
    const st_index_t c2 = 0x1b873593;
    const st_index_t a = 0xe6546b64;
#else
#define r1 (31)
#define r2 (27)
    const st_index_t c1 = BIG_CONSTANT(0x87c37b91,0x114253d5);
    const st_index_t c2 = BIG_CONSTANT(0x4cf5ad43,0x2745937f);
    const st_index_t a = BIG_CONSTANT(0xe6546b64,0x38495ab5);
#endif
    k ^= st_seed[0];
    k *= c1;
    k = ROTL(k, r1);
    k *= c2;

    h ^= k;
    h = ROTL(h, r2);
    h = h*5 + a;
    return h;
#undef r1
#undef r2
}

static inline st_index_t
murmur_finish(st_index_t h)
{
#if ST_INDEX_BITS <= 32
#define r1 (16)
#define r2 (13)
#define r3 (16)
    const st_index_t c1 = 0x85ebca6b;
    const st_index_t c2 = 0xc2b2ae35;
#else
/* values are taken from Mix13 on http://zimbry.blogspot.ru/2011/09/better-bit-mixing-improving-on.html */
#define r1 (30)
#define r2 (27)
#define r3 (31)
    const st_index_t c1 = BIG_CONSTANT(0xbf58476d,0x1ce4e5b9);
    const st_index_t c2 = BIG_CONSTANT(0x94d049bb,0x133111eb);
#endif
    h ^= st_seed[0];
    h ^= h >> r1;
    h *= c1;
    h ^= h >> r2;
    h *= c2;
    h ^= h >> r3;
#if ST_INDEX_BITS > 64
    h *= c1;
    h ^= h >> r2;
    h *= c2;
    h ^= h >> r3;
#endif
    return h ^ st_seed[1];
}

st_index_t
st_hash(const void *ptr, size_t len, st_index_t h)
{
    const char *data = ptr;
    st_index_t t = 0;
    size_t l = len;
    assert(!st_need_seed);

#define data_at(n) (st_index_t)((unsigned char)data[(n)])
#define UNALIGNED_ADD_4 UNALIGNED_ADD(2); UNALIGNED_ADD(1); UNALIGNED_ADD(0)
#if SIZEOF_ST_INDEX_T > 4
#define UNALIGNED_ADD_8 UNALIGNED_ADD(6); UNALIGNED_ADD(5); UNALIGNED_ADD(4); UNALIGNED_ADD(3); UNALIGNED_ADD_4
#if SIZEOF_ST_INDEX_T > 8
#define UNALIGNED_ADD_16 UNALIGNED_ADD(14); UNALIGNED_ADD(13); UNALIGNED_ADD(12); UNALIGNED_ADD(11); \
    UNALIGNED_ADD(10); UNALIGNED_ADD(9); UNALIGNED_ADD(8); UNALIGNED_ADD(7); UNALIGNED_ADD_8
#define UNALIGNED_ADD_ALL UNALIGNED_ADD_16
#endif
#define UNALIGNED_ADD_ALL UNALIGNED_ADD_8
#else
#define UNALIGNED_ADD_ALL UNALIGNED_ADD_4
#endif
    if (len >= sizeof(st_index_t)) {
#if !UNALIGNED_WORD_ACCESS
	int align = (int)((st_data_t)data % sizeof(st_index_t));
	if (align) {
	    st_index_t d = 0;
	    int sl, sr, pack;

	    switch (align) {
#ifdef WORDS_BIGENDIAN
# define UNALIGNED_ADD(n) case SIZEOF_ST_INDEX_T - (n) - 1: \
		t |= data_at(n) << CHAR_BIT*(SIZEOF_ST_INDEX_T - (n) - 2)
#else
# define UNALIGNED_ADD(n) case SIZEOF_ST_INDEX_T - (n) - 1:	\
		t |= data_at(n) << CHAR_BIT*(n)
#endif
		UNALIGNED_ADD_ALL;
#undef UNALIGNED_ADD
	    }

#ifdef WORDS_BIGENDIAN
	    t >>= (CHAR_BIT * align) - CHAR_BIT;
#else
	    t <<= (CHAR_BIT * align);
#endif

	    data += sizeof(st_index_t)-align;
	    len -= sizeof(st_index_t)-align;

	    sl = CHAR_BIT * (SIZEOF_ST_INDEX_T-align);
	    sr = CHAR_BIT * align;

	    while (len >= sizeof(st_index_t)) {
		d = *(st_index_t *)data;
#ifdef WORDS_BIGENDIAN
		t = (t << sr) | (d >> sl);
#else
		t = (t >> sr) | (d << sl);
#endif
		h = murmur_step(h, t);
		t = d;
		data += sizeof(st_index_t);
		len -= sizeof(st_index_t);
	    }

	    pack = len < (size_t)align ? (int)len : align;
	    d = 0;
	    switch (pack) {
#ifdef WORDS_BIGENDIAN
# define UNALIGNED_ADD(n) case (n) + 1: \
		d |= data_at(n) << CHAR_BIT*(SIZEOF_ST_INDEX_T - (n) - 1)
#else
# define UNALIGNED_ADD(n) case (n) + 1: \
		d |= data_at(n) << CHAR_BIT*(n)
#endif
		UNALIGNED_ADD_ALL;
#undef UNALIGNED_ADD
	    }
#ifdef WORDS_BIGENDIAN
	    t = (t << sr) | (d >> sl);
#else
	    t = (t >> sr) | (d << sl);
#endif

	    if (len < (size_t)align) goto skip_tail;
	    h = murmur_step(h, t);
	    data += pack;
	    len -= pack;
	}
	else
#endif
	{
	    do {
		h = murmur_step(h, *(st_index_t *)data);
		data += sizeof(st_index_t);
		len -= sizeof(st_index_t);
	    } while (len >= sizeof(st_index_t));
	}
    }

    t = 0;
    switch (len) {
#ifdef WORDS_BIGENDIAN
# define UNALIGNED_ADD(n) case (n) + 1: \
	t |= data_at(n) << CHAR_BIT*(SIZEOF_ST_INDEX_T - (n) - 1)
#else
# define UNALIGNED_ADD(n) case (n) + 1: \
	t |= data_at(n) << CHAR_BIT*(n)
#endif
	UNALIGNED_ADD_ALL;
#undef UNALIGNED_ADD
# if !UNALIGNED_WORD_ACCESS
      skip_tail:
# endif
	h = murmur_step(h, t);
    }
    h ^= l;

    return murmur_finish(h);
}

st_index_t
st_hash_uint32(st_index_t h, uint32_t i)
{
    return murmur_step(h, i);
}

st_index_t
st_hash_uint(st_index_t h, st_index_t i)
{
    i += h;
/* no matter if it is BigEndian or LittleEndian,
 * we hash just integers */
#if SIZEOF_ST_INDEX_T*CHAR_BIT > 8*8
    h = murmur_step(h, i >> 8*8);
#endif
    h = murmur_step(h, i);
    return h;
}

st_index_t
st_hash_end(st_index_t h)
{
    h = murmur_finish(h);
    return h;
}

st_index_t
st_hash_start(st_index_t h)
{
    return h + st_seed[1];
}

static st_index_t
strhash(st_data_t arg)
{
    register const char *string = (const char *)arg;
    return st_hash(string, strlen(string), st_seed[1]);
}

int
st_locale_insensitive_strcasecmp(const char *s1, const char *s2)
{
    unsigned int c1, c2;

    while (1) {
        c1 = (unsigned char)*s1++;
        c2 = (unsigned char)*s2++;
        if (c1 == '\0' || c2 == '\0') {
            if (c1 != '\0') return 1;
            if (c2 != '\0') return -1;
            return 0;
        }
        if ((unsigned int)(c1 - 'A') <= ('Z' - 'A')) c1 += 'a' - 'A';
        if ((unsigned int)(c2 - 'A') <= ('Z' - 'A')) c2 += 'a' - 'A';
        if (c1 != c2) {
            if (c1 > c2)
                return 1;
            else
                return -1;
        }
    }
}

int
st_locale_insensitive_strncasecmp(const char *s1, const char *s2, size_t n)
{
    unsigned int c1, c2;

    while (n--) {
        c1 = (unsigned char)*s1++;
        c2 = (unsigned char)*s2++;
        if (c1 == '\0' || c2 == '\0') {
            if (c1 != '\0') return 1;
            if (c2 != '\0') return -1;
            return 0;
        }
        if ((unsigned int)(c1 - 'A') <= ('Z' - 'A')) c1 += 'a' - 'A';
        if ((unsigned int)(c2 - 'A') <= ('Z' - 'A')) c2 += 'a' - 'A';
        if (c1 != c2) {
            if (c1 > c2)
                return 1;
            else
                return -1;
        }
    }
    return 0;
}

PUREFUNC(static st_index_t strcasehash(st_data_t));
static st_index_t
strcasehash(st_data_t arg)
{
    register const char *string = (const char *)arg;
    register st_index_t hval = FNV1_32A_INIT;

    /*
     * FNV-1a hash each octet in the buffer
     */
    while (*string) {
	unsigned int c = (unsigned char)*string++;
	if ((unsigned int)(c - 'A') <= ('Z' - 'A')) c += 'a' - 'A';
	hval ^= c;

	/* multiply by the 32 bit FNV magic prime mod 2^32 */
	hval *= FNV_32_PRIME;
    }
    return hval;
}

int
st_numcmp(st_data_t x, st_data_t y)
{
    return x != y;
}

st_index_t
st_numhash(st_data_t n)
{
    return (n << 3) ^ (n >> 11) ^ (n >> 3);
}
