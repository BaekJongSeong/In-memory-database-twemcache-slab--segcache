#include "slab.h"

#include <hash/cc_murmur3.h>
#include <cc_mm.h>
#include <stdio.h>
#include <wchar.h>

static uint32_t murmur3_iv = 0x3ac5d673;
extern FILE* fp;
extern FILE* file_item;
extern uint32_t real_set;
extern uint32_t real_delete;
extern uint32_t real_evict;
/*
 * Allocate table given size
 */
static struct item_slh *
_hashtable_alloc(uint64_t size)
{
    struct item_slh *table;
    uint32_t i;

    table = cc_alloc(sizeof(*table) * size);

    if (table != NULL) {
        for (i = 0; i < size; ++i) {
            SLIST_INIT(&table[i]);
        }
    }

    return table;
}

struct hash_table *
hashtable_create(uint32_t hash_power)
{
    struct hash_table *ht;
    uint64_t size;

    ASSERT(hash_power > 0);

    /* alloc struct */
    ht = cc_alloc(sizeof(struct hash_table));

    if (ht == NULL) {
        return NULL;
    }

    /* init members */
    ht->table = NULL;
    ht->hash_power = hash_power;
    ht->nhash_item = 0;
    size = HASHSIZE(ht->hash_power);

    /* alloc table */
    ht->table = _hashtable_alloc(size);
    if (ht->table == NULL) {
        cc_free(ht);
        return NULL;
    }
printf("create hashtable %ld bucket, (info: 1 bucket got 1 slist)\n",size);
    return ht;
}

void
hashtable_destroy(struct hash_table **ht_p)
{
    struct hash_table *ht = *ht_p;
    if (ht != NULL && ht->table != NULL) {
        cc_free(ht->table);
    }

    *ht_p = NULL;
}

static struct item_slh *
_get_bucket(const char *key, size_t klen, struct hash_table *ht)
{
    uint32_t hv;

    hash_murmur3_32(key, klen, murmur3_iv, &hv);

    return &(ht->table[hv & HASHMASK(ht->hash_power)]);
}

void
hashtable_put(struct item *it, struct hash_table *ht)
{
    struct item_slh *bucket;

    ASSERT(hashtable_get(item_key(it), it->klen, ht) == NULL);

    bucket = _get_bucket(item_key(it), it->klen, ht);
    SLIST_INSERT_HEAD(bucket, it, i_sle);

/*uint32_t hv; char* key=item_key(it);
//hash_murmur3_32(key, it->klen, murmur3_iv, &hv);
//fprintf(fp, "item_key: %s %ld %d %d => bucket name %ld ||",it->end,item_cas_size(),it->olen, it->klen,hv & HASHMASK(ht->hash_power));           
//fprintf(fp, "slab_id: %d => item_info %ld\n",it->id,ITEM_HDR_SIZE+ item_cas_size()+it->olen+it->vlen+it->klen);//(hv&((1ULL << (ht->hash_power))-1)));
*/
//FILE* fp = fopen("./1000_log/1000_slab_num0_0930.txt", "a");
/*size_t len_ = item_ntotal(it->klen,it->vlen,it->olen);
struct slab *slabslab =  item_to_slab(it);
fprintf(fp, "hashtable put= slabclass: %d, slab locate: %p, item size: %ld\n",slab_id(item_ntotal(it->klen, it->vlen, it->olen)), &(*slabslab) ,len_);
//fclose(fp);
*/
/*fprintf(file_item, "%S", item_key(it));
//fwprintf(file_item, L"%S", item_key(it));
fprintf(file_item,"\n");*/

    ++(ht->nhash_item);
    INCR(slab_metrics, hash_insert);

real_set+=1;
}

void
hashtable_delete(const char *key, uint32_t klen, struct hash_table *ht)
{
    struct item_slh *bucket;
    struct item *it, *prev;

    ASSERT(hashtable_get(key, klen, ht) != NULL);

    bucket = _get_bucket(key, klen, ht);
    for (prev = NULL, it = SLIST_FIRST(bucket); it != NULL;
        prev = it, it = SLIST_NEXT(it, i_sle)) {
        INCR(slab_metrics, hash_traverse);

        /* iterate through bucket to find item to be removed */
        if ((klen == it->klen) && cc_memcmp(key, item_key(it), klen) == 0) {
            /* found item */
            break;
        }
    }

    if (prev == NULL) {
        SLIST_REMOVE_HEAD(bucket, i_sle);
    } else {
        SLIST_REMOVE_AFTER(prev, i_sle);
    }
    
//FILE* fp = fopen("./1000_log/1000_slab_num0_0930.txt", "a");
/*size_t len_ = item_ntotal(it->klen,it->vlen,it->olen);
struct slab *slabslab =  item_to_slab(it);
fprintf(fp, "hashtable delete= slabclass: %d, slab locate: %p, item size: %ld\n",slab_id(item_ntotal(it->klen, it->vlen, it->olen)), &(*slabslab) ,len_);
//fclose(fp);*/

    --(ht->nhash_item);
    INCR(slab_metrics, hash_remove);
    real_delete+=1;
}

struct item *
hashtable_get(const char *key, uint32_t klen, struct hash_table *ht)
{
    struct item_slh *bucket;
    struct item *it;

    ASSERT(key != NULL);
    ASSERT(klen != 0);

    INCR(slab_metrics, hash_lookup);

    bucket = _get_bucket(key, klen, ht);
    /* iterate through bucket looking for item */
    for (it = SLIST_FIRST(bucket); it != NULL; it = SLIST_NEXT(it, i_sle)) {
        INCR(slab_metrics, hash_traverse);

        if ((klen == it->klen) && cc_memcmp(key, item_key(it), klen) == 0) {
            /* found item */
            return it;
        }
    }

    return NULL;
}

/*
 * Expand the hashtable to the next power of 2.
 * This is an expensive operation and should _not_ be used in production or
 * during latency-related tests. It is included mostly for simulation around
 * the storage component.
 */
struct hash_table *
hashtable_double(struct hash_table *ht)
{
    struct hash_table *new_ht;
    uint32_t new_hash_power;
    uint64_t new_size;

    new_hash_power = ht->hash_power + 1;
    new_size = HASHSIZE(new_hash_power);

    new_ht = hashtable_create(new_size);
    if (new_ht == NULL) {
        return ht;
    }

    /* copy to new hash table */
    for  (uint32_t i = 0; i < HASHSIZE(ht->hash_power); ++i) {
        struct item *it, *next;
        struct item_slh *bucket, *new_bucket;

        bucket = &ht->table[i];
        SLIST_FOREACH_SAFE(it, bucket, i_sle, next) {
            new_bucket = _get_bucket(item_key(it), it->klen, new_ht);
            SLIST_REMOVE(bucket, it, item, i_sle);
            SLIST_INSERT_HEAD(new_bucket, it, i_sle);
        }
    }

    hashtable_destroy(&ht);

    return new_ht;
}

