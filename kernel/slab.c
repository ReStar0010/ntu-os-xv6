#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "riscv.h"
#include "defs.h"
#include "slab.h"
#include "file.h"
#define PAGE_SIZE 4096

uint64 sys_printfslab(void){
  print_kmem_cache(file_cache, fileprint_metadata);
  return 0;
}

void print_kmem_cache(struct kmem_cache *cache, void (*slab_obj_printer)(void *))
{
  // TODO: Implement print_kmem_cache
  // TODO: Implement In_cache print
  acquire(&cache->lock);
  printf("[SLAB] kmem_cache { name: %s, object_size: %d, at: %p, in_cache_obj: %d }\n", cache->name, cache->object_size, cache, cache->in_cache_object);
  if(cache->in_cache_object != 0){
    printf("[SLAB]  [ cache slabs ]\n");
    printf("[SLAB]    [ slab %p ] { freelist: %p, nxt: %p }\n", cache, cache->freelist, NULL);
    int object_num = (PAGE_SIZE - sizeof(struct kmem_cache)) / cache->object_size;
    void *cache_freelist = (void *)((char *)cache + sizeof(struct kmem_cache));
    for(int i=0;i<object_num;i++){
      void *object_addr = (void *)(((char *)cache_freelist) + (cache->object_size * i));
      struct run * ptr = (struct run *)object_addr;
      printf("[SLAB]      [ idx %d ] { addr: %p, as_ptr: %p, as_obj: { ", i, ptr, ptr->next);
      slab_obj_printer(object_addr);
      printf(" } }\n");
    }
  }
  if(!list_empty(&cache->partial)){
    printf("[SLAB]  [ partial slabs ]\n");
    struct slab *entry;
    int object_num = (PAGE_SIZE - sizeof(struct slab)) / cache->object_size;
    list_for_each_entry(entry, &cache->partial, list) {
      if(entry->list.next != &cache->partial)
        printf("[SLAB]    [ slab %p ] { freelist: %p, nxt: %p }\n", entry, entry->freelist, entry->list.next);
      else
        printf("[SLAB]    [ slab %p ] { freelist: %p, nxt: %p }\n", entry, entry->freelist, NULL);
      for(int i=0;i<object_num;i++){
        //*TODO - We can use bit opertation to calculate offset in freeslist
        void *object_addr = (void *)(((char *)entry) + sizeof(struct slab) + (cache->object_size * i));
        struct run * ptr = (struct run *)object_addr;
        printf("[SLAB]      [ idx %d ] { addr: %p, as_ptr: %p, as_obj: { ", i, ptr, ptr->next);
        slab_obj_printer(object_addr);
        printf(" } }\n");
      }
    }
  }
  printf("[SLAB] print_kmem_cache end\n");
  release(&cache->lock);
}

struct kmem_cache *kmem_cache_create(char *name, uint object_size)
{
  // TODO: Implement kmem_cache_create
  struct kmem_cache * cache = (struct kmem_cache *)kalloc();
  initlock(&cache->lock, cache->name);
  strncpy(cache->name, name, sizeof(cache->name));
  cache->object_size = object_size;
  cache->in_cache_object = (PAGE_SIZE - sizeof(struct kmem_cache)) / object_size;
  cache->full_num = 0;
  cache->partial_num = 0;
  cache->free_num = 0;
  cache->cache_in_use = -1;
  INIT_LIST_HEAD(&cache->free);
  INIT_LIST_HEAD(&cache->partial);
  INIT_LIST_HEAD(&cache->full);
  size_t meta_size = sizeof(struct slab);
  int max_objects_per_slab = (PAGE_SIZE - meta_size) / object_size;
  printf("[SLAB] New kmem_cache (name: %s, object size: %d bytes, at: %p, max objects per slab: %d, support in cache obj: %d) is created\n"
          ,name, object_size, cache, max_objects_per_slab, cache->in_cache_object);
  return cache;
}

void kmem_cache_destroy(struct kmem_cache *cache)
{
  // TODO: Implement kmem_cache_destroy (will not be tested)
  printf("[SLAB] TODO: kmem_cache_destroy is not yet implemented \n");
}

void *kmem_cache_alloc(struct kmem_cache *cache)
{
  //TODO: Implement kmem_cache_alloc  
  acquire(&cache->lock); // acquire the lock before modification
  printf("[SLAB] Alloc request on cache %s\n", cache->name);
  if(cache->cache_in_use == -1){
    //*NOTE - Init in cache slab
    int meta_size = sizeof(struct kmem_cache);
    char *obj_start_add = ((char *)cache + meta_size);
    for(int i=0;i<cache->in_cache_object-1;i++){
      struct run *cur = (struct run *)(obj_start_add + i*cache->object_size);
      struct run *next = (struct run *)(obj_start_add + (i+1)*cache->object_size);
      cur->next = next;
    }
    struct run *tail = (struct run *)(obj_start_add + (cache->in_cache_object - 1)*cache->object_size);
    tail->next = NULL;
    cache->freelist = (struct run *)obj_start_add;
    cache->cache_in_use = 0;
  }
  if(cache->cache_in_use == cache->in_cache_object){
    if(list_empty(&cache->partial)){ //*NOTE - partial is empty
      if(list_empty(&cache->free)){ //*NOTE -  free is empty
          //* create new slab
          void * new_page = kalloc(); // new pages
          struct slab * new_slab = (struct slab *)new_page; // meta data
          INIT_LIST_HEAD(&new_slab->list);
          printf("[SLAB] A new slab %p (%s) is allocated\n", new_slab, cache->name);
          //* build freelist
          size_t meta_size = sizeof(struct slab);
          char *obj_start_addr = ((char *)new_slab) + meta_size; // inital freelist object
          int obj_num = (PAGE_SIZE - meta_size) / cache->object_size; // calculate object nums
          for(int i=0;i<obj_num-1;i++){ // initial free list
            struct run *cur = (struct run *)(obj_start_addr + i*cache->object_size);
            struct run *next = (struct run *)(obj_start_addr + (i+1)*cache->object_size);
            cur->next = next;
          }
          //* tail point to NULL
          struct run *tail = (struct run *)(obj_start_addr + (obj_num-1)*cache->object_size); // last object 
          tail->next = NULL;  // last object point ot NULL
          list_add(&new_slab->list, &cache->partial); //*NOTE - move the slab into partial
          //* return the object
          new_slab->freelist = (struct run *)obj_start_addr; // freelist complete
          new_slab->freelist->in_use = 0;
          //* save the in_use
          int in_use = new_slab->freelist->in_use + 1;
          struct run *r = new_slab->freelist;
          new_slab->freelist  = new_slab->freelist->next;
          new_slab->freelist->in_use = in_use;
          printf("[SLAB] Object %p in slab %p (%s) is allocated and initialized\n", r, new_slab, cache->name);
          //* maintain partial_num
          cache->partial_num++;
          release(&cache->lock); // release the lock before return
          return (void *)r;
      }
      else{ //*NOTE -  free is not empty
        struct slab * to_partial_slab = list_first_entry(&cache->free, struct slab, list);
        list_add(&to_partial_slab->list, &cache->partial);
        list_del(&to_partial_slab->list);
        // *NOTE - move to partial
        int in_use = to_partial_slab->freelist->in_use + 1;
        struct run *r = to_partial_slab->freelist;
        to_partial_slab->freelist = to_partial_slab->freelist->next;
        to_partial_slab->freelist->in_use = in_use;
        printf("[SLAB] Object %p in slab %p (%s) is allocated and initialized\n", r, to_partial_slab, cache->name);
        //* maintain partial_num free_num
        cache->free_num--; cache->partial_num++;
        release(&cache->lock); // release the lock before return
        return (void *)r;
      }
    }
    else{ //*NOTE -  partial is not empty
      struct slab * partial_slab = list_first_entry(&cache->partial, struct slab, list);
      int meta_size = sizeof(struct slab);
      int obj_nums = (PAGE_SIZE - meta_size) / cache->object_size; // calculate object nums
      int in_use = partial_slab->freelist->in_use + 1;
      if(in_use == obj_nums){ // move the slab into full
        list_del(&partial_slab->list);
        list_add(&partial_slab->list, &cache->full);
        cache->full_num++; cache->partial_num--;
      }
      struct run *r = partial_slab->freelist;
      partial_slab->freelist = partial_slab->freelist->next; //*NOTE - point to NULL
      if(partial_slab->freelist != NULL)
        partial_slab->freelist->in_use = in_use;
      printf("[SLAB] Object %p in slab %p (%s) is allocated and initialized\n", r, partial_slab, cache->name);
      release(&cache->lock); // release the lock before return
      return (void *)r;
    }
  }
  else{
    //*NOTE - alloc from in cache
    struct run *r = cache->freelist;
    cache->freelist = cache->freelist->next;
    cache->cache_in_use++;
    printf("[SLAB] Object %p in slab %p (%s) is allocated and initialized\n", r, cache, cache->name);
    release(&cache->lock);
    return (void *)r;
  }
  release(&cache->lock); // release the lock before return
  return 0;
}

static inline void *get_slab_from_obj(void *obj) {
  return (void *)((uint64)obj & ~(PAGE_SIZE - 1));
}

void kmem_cache_free(struct kmem_cache *cache, void *obj)
{
  // TODO: Implement kmem_cache_free
  acquire(&cache->lock); // acquire the lock before modification
  struct slab *entry;
  entry = (struct slab *)get_slab_from_obj(obj);
  printf("[SLAB] Free %p in slab %p (%s)\n", obj, entry, cache->name);  
  if((void *)entry == (void *)cache){
    //*NOTE - entry is cache
    struct run *prev = cache->freelist;
    cache->freelist = (struct run *)obj;
    cache->freelist->next = prev;
    cache->cache_in_use--;
    printf("[SLAB] End of free\n");
    release(&cache->lock); // release the lock before return
  }
  else{
    // NOTE: put the obj into slab's freelist
    int object_nums = (PAGE_SIZE - sizeof(struct slab)) / cache->object_size;
    struct run *prev = entry->freelist;
    int in_use;
    if(prev == NULL)
      in_use = object_nums - 1;
    else
      in_use = prev->in_use - 1;
    entry->freelist = (struct run *)obj;
    entry->freelist->next = prev;
    entry->freelist->in_use = in_use;
    //*NOTE - check inuse
    int in_free_slab = 0;
    if(entry->freelist->in_use == 0){ //*NOTE - move from partial to free
      list_del(&entry->list);
      list_add(&entry->list, &cache->free); 
      cache->partial_num--; cache->free_num++;
      in_free_slab = 1;
    }
    else{
      if(entry->freelist->in_use == object_nums - 1){ //*NOTE - move from full to partial
        list_del(&entry->list);
        list_add(&entry->list, &cache->partial);
        cache->full_num--; cache->partial_num++;
      }
    }
    //*NOTE - check MP2_MIN
    if(in_free_slab && (cache->free_num + cache->partial_num) > MP2_MIN_AVAIL_SLAB){
      // *NOTE - restore the current free slab
      printf("[SLAB] slab %p (%s) is freed due to save memory\n", entry, cache->name);
      list_del(&entry->list);
      cache->free_num--;
      kfree((void *)entry);
    }
    printf("[SLAB] End of free\n");
    release(&cache->lock); // release the lock before return
  }
}
