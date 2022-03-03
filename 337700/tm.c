/**
 * @file   tm.c
 * @author [...]
 *
 * @section LICENSE
 *
 * [...]
 *
 * @section DESCRIPTION
 *
 * Implementation of your own transaction manager.
 * You can completely rewrite this file (and create more files) as you wish.
 * Only the interface (i.e. exported symbols and semantic) must be preserved.
**/

// Requested features

#define _GNU_SOURCE
#define _POSIX_C_SOURCE   200809L
#ifdef __STDC_NO_ATOMICS__
    #error Current C11 compiler does not support atomic operations
#endif

// External headers

// Internal headers
#include <tm.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "macros.h"
#define printf(format, ...)
#define set_size 50000
//remember to free everything that you allocated, 

struct word_info{
    int version;
    void* word_adress;
};
struct real_word{
    struct word_control* control_word_start;
};
struct tm_transaction{
    struct word_info read_set[set_size];
    int read_index;
    struct real_word write_words[set_size];
    int write_index; 
    bool success;    
};

struct region
{
    void* start;
    size_t size;                      // Size of the shared memory region (in bytes)
    size_t align;                     // Claimed alignment of the shared memory region (in bytes)
    struct segment* alloc_segments;//need to lock     
};

struct word_control{
    uint64_t valid_copy;
    uint64_t not_valid_copy;
    struct tm_transaction* write_tm;
    pthread_mutex_t word_lock; 
    atomic_int version;   
};

struct segment
{
    void* content;
    struct segment* next;        
};
bool commit(struct tm_transaction * t){
    printf("%lu,enter commit-1\n",pthread_self());
    for(int i = 0; i < t-> read_index; i++){
        struct word_control* wc = (struct word_control *) ((t -> read_set + i) -> word_adress);
        if((t -> read_set + i) -> version != wc -> version){
            return false;
        }
    }
    for(int i = 0; i < t-> write_index; i++){ 
        struct word_control* wc = (t -> write_words + i)-> control_word_start;
        printf("%lu,i:%u---swap wc:%u\n",pthread_self(), i, wc);
        
        printf("%lu,i:%u---wap wc:%u\n",pthread_self(), i, wc);      
        //printf("%lu,validCopy:%u\n",pthread_self(),*((unsigned int*)wc-> valid_copy));
        //printf("%lu,notvalidCopy:%u\n",pthread_self(),*((unsigned int*)wc-> not_valid_copy)); 
        
        uint64_t temp = wc -> valid_copy;
        wc -> valid_copy = wc -> not_valid_copy;
        wc -> not_valid_copy = temp;
        atomic_fetch_add(&(wc -> version),1);  
        //printf("%lu,validCopy:%u\n",pthread_self(),*((unsigned int*)wc -> valid_copy));
        //printf("%lu,notvalidCopy:%u\n",pthread_self(),*((unsigned int*)wc -> not_valid_copy));     
    }
    for(int i = 0; i < t-> write_index; i++){
        //struct word_control* wc = (struct word_control*)(*(unsigned int*)(t -> write_words + i*sizeof(void*)));
        struct word_control* wc = (t -> write_words + i)-> control_word_start;
        printf("%lu,i:%u---change it to NULL: wc:%u\n",pthread_self(), i, wc);
        wc -> write_tm = NULL;     
    }    
    printf("%lu,leave commit\n",pthread_self());
    return true;
}

/** [thread-safe] Memory allocation in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param size   Allocation requested size (in bytes), must be a positive multiple of the alignment
 * @param target Pointer in private memory receiving the address of the first byte of the newly allocated, aligned segment
 * @return Whether the whole transaction can continue (success/nomem), or not (abort_alloc)
**/
alloc_t tm_alloc(shared_t shared, tx_t tx, size_t size, void** target) {
    // TODO: tm_alloc(shared_t, tx_t, size_t, void**)
    printf("%lu,enter_tm_alloc-1\n",pthread_self());
    
    struct region* region = (struct region*) shared;
    unsigned int num = size / region -> align;
    
    //struct tm_transaction* t = &(region-> transaction_list[tx]);
    struct tm_transaction* t = (struct tm_transaction*)tx;
    
    void * temp_real_word;
    if (unlikely(posix_memalign(&temp_real_word, sizeof(struct real_word), num*sizeof(struct real_word)) != 0) ){
        // Allocation failed
        printf("%lu,Allocation failed\n",pthread_self());
        t -> success = false;
        tm_end(shared, tx);
        return nomem_alloc;    
    }
    struct real_word *real_word = (struct real_word *)temp_real_word;
    struct word_control* control_block = (struct word_control*)malloc(sizeof(struct word_control) * num);
    if (unlikely(!control_block) ){
        // Allocation failed
        printf("%lu,Allocation failed\n",pthread_self());
        t -> success = false;
        tm_end(shared, tx);
        free(real_word);
        return nomem_alloc;    
    }

    for(unsigned int i = 0; i < num; i++){
        (real_word + i) -> control_word_start = control_block + i;
        
        (control_block+ i)-> valid_copy = 0;
        (control_block+ i)-> not_valid_copy = 0;
        (control_block+ i)-> write_tm = NULL;

        atomic_init(&((control_block+ i)-> version),0); 
        if (unlikely(pthread_mutex_init(&((control_block+ i)->word_lock), NULL) != 0))
        {
            pthread_mutex_destroy(&((control_block+ i)->word_lock));
            free(real_word);
            free(control_block);
            printf("%lu,initalize_control_block[i].access_set_lock fail\n",pthread_self());
            t -> success = false;
            tm_end(shared, tx);
            return abort_alloc;
        }     
    }

    struct segment* sn = malloc(sizeof(struct segment));
    if(unlikely(!sn)){
        free(real_word);
        free(control_block); 
        t -> success = false;
        tm_end(shared, tx);
        return abort_alloc;  
    }

    sn -> content = real_word;

    sn -> next = region -> alloc_segments;
    region -> alloc_segments = sn;
    
    *target = real_word;
    
    printf("%lu,leave_tm_alloc\n",pthread_self());
    return success_alloc;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment to deallocate
 * @return Whether the whole transaction can continue
**/

bool tm_free(shared_t unused(shared), tx_t unused(tx), void* unused(target)) {
    return true;
}

/** Create (i.e. allocate + init) a new shared memory region, with one first non-free-able allocated segment of the requested size and alignment.
 * @param size  Size of the first shared segment of memory to allocate (in bytes), must be a positive multiple of the alignment
 * @param align Alignment (in bytes, must be a power of 2) that the shared memory region must support
 * @return Opaque shared memory region handle, 'invalid_shared' on failure
**/
shared_t tm_create(size_t size, size_t align) {
    printf("enter_tm_create:real word:%u\n",sizeof(struct real_word));
    printf("enter_tm_create:align:%u\n",align);
    printf("enter_tm_create:size:%u\n",size);
    struct region* region = (struct region*) malloc(sizeof(struct region));
    
    if (unlikely(!region)) {
        return invalid_shared;
    }

    region->size        = size;
    region->align       = align;
    region->alloc_segments = NULL;
    unsigned int num = size / region -> align;

    void* temp_real_word;
    if (unlikely(posix_memalign(&temp_real_word, sizeof(struct real_word), num*sizeof(struct real_word)) != 0)) {
        // Allocation failed
        printf("%lu,Allocation failed\n",pthread_self());
        free(region);
        return invalid_shared;    
    }
    struct real_word *real_word = (struct real_word *)temp_real_word;
    struct word_control* control_block = (struct word_control*)malloc(sizeof(struct word_control) * num);
    if (unlikely(!control_block)){
        // Allocation failed
        printf("%lu,Allocation failed\n",pthread_self());
        free(real_word);
        free(region);
        return invalid_shared;    
    }
    printf("%lu,size:%u\n",pthread_self(),size);
    for(unsigned int i = 0; i < num; i++){

        (real_word + i) -> control_word_start = control_block + i;
        (control_block+ i)-> valid_copy = 0;
        (control_block+ i)-> not_valid_copy = 0;
        (control_block+ i)-> write_tm = NULL;
        
        atomic_init(&((control_block+ i)-> version),0);
        if (unlikely(pthread_mutex_init(&((control_block+ i)->word_lock), NULL) != 0))
        {
            pthread_mutex_destroy(&((control_block+ i)->word_lock));
            free(real_word);
            free(control_block);

            free(region);
            printf("%lu,initalize_control_block[i].access_set_lock fail\n",pthread_self());
            return invalid_shared;
        } 
    
    }
    region -> start = real_word;
    printf("region start:%u\n", region -> start);
    return region;
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
**/
void tm_destroy(shared_t shared) {
    // TODO: tm_destroy(shared_t)
    
    struct region* region = (struct region *)shared;
    struct real_word* rw = (struct real_word*)region->start;
    struct word_control* wc = rw -> control_word_start;

    free(wc);
    free(region->start);
    struct segment* sn = region ->alloc_segments;
    while(sn){
        struct segment* next = sn->next;
        struct real_word* rw = (struct real_word*)sn -> content;
        struct word_control* wc = rw -> control_word_start;
        free(wc);
        free(sn -> content);//Do I need to destroy the lock?
        free(sn);       
        sn = next;
    }
    free(region);
}

/** [thread-safe] Begin a new transaction on the given shared memory region.
 * @param shared Shared memory region to start a transaction on
 * @param is_ro  Whether the transaction is read-only
 * @return Opaque transaction ID, 'invalid_tx' on failure
**/
tx_t tm_begin(shared_t unused(shared), bool unused(is_ro)) {
    printf("%lu,enter_tm_begin\n",pthread_self());
    struct tm_transaction* t = (struct tm_transaction*)malloc(sizeof(struct tm_transaction));
    printf("%lu,enter_tm_begin t:%u\n",pthread_self(), t);  
    t -> success = true;
    t -> read_index = 0;
    t -> write_index = 0;
    return (tx_t)t;
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
**/
bool tm_end(shared_t unused(shared), tx_t tx) {
    // TODO: tm_end(shared_t, tx_t)
    printf("%lu,enter_tm_end\n",pthread_self());
    //struct tm_transaction* t = &(region-> transaction_list[tx]);
    struct tm_transaction* t = (struct tm_transaction*)tx;
    bool ret = t -> success;
    if(ret){
        //try to commit
        ret = commit(t);
    }
    if(!ret)
    {//abort
        for(int i = 0; i < t-> write_index; i++){
            struct word_control* wc = (t -> write_words + i)-> control_word_start;
            printf("%lu,i:%u---change it to NULL: wc:%u\n",pthread_self(), i, wc);
            wc -> write_tm = NULL;   
        }        
    }
    printf("%lu,free t:%u\n",pthread_self(),t);
    free(t);
    //printf("%lu,leave_tm_end\n",pthread_self());
    return ret;
}


/** [thread-safe] Read operation in the given transaction, source in the shared region and target in a private region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in the shared region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the alignment
 * @param target Target start address (in a private region)
 * @return Whether the whole transaction can continue
**/

bool read_word(tx_t tx, struct word_control* wc, void* target){
    printf("%lu,enter_read_word:%u\n",pthread_self(),wc);
    //struct tm_transaction* t = &(region-> transaction_list[tx]);
    struct tm_transaction* t = (struct tm_transaction*)tx;

    if(!wc->write_tm){
        printf("%lu,t->read_index:%d\n",pthread_self(),t->read_index);
        (t -> read_set + t->read_index) -> version = wc -> version;
        //memcpy(target, wc -> valid_copy, size);
        *((uint64_t*)target) = wc -> valid_copy;
        (t -> read_set + t->read_index) ->word_adress = wc;
        ++(t->read_index);
        printf("%lu,1-Read value:%u\n",pthread_self(),*((unsigned int*)target));
        return true;          
    }
    else{
        if(wc -> write_tm == t){
            //memcpy(target, wc -> not_valid_copy, size);
            *((uint64_t*)target) = wc -> not_valid_copy;
            printf("%lu,2-Read value:%u\n",pthread_self(),*((unsigned int*)target));
            return true;           
        }
        else{
            printf("%lu,read word fail!!!!!\n",pthread_self());
            return false;
        }
    }    
}

bool tm_read(shared_t shared, tx_t tx, void const* source, size_t size, void* target) {
    // TODO: tm_read(shared_t, tx_t, void const*, size_t, void*)
    printf("%lu,enter_tm_read\n",pthread_self());
    //struct word_control* wc = findWordControl((struct region*)shared,source);
    struct real_word* rw = (struct real_word*)source;
    struct word_control* wc = rw -> control_word_start;
    printf("%lu,enter_tm_read:%u\n",pthread_self(),wc);
    
    struct region *region = (struct region *)shared;
    size_t num = size / region -> align;
    //struct tm_transaction* t = &(region-> transaction_list[tx]);
    struct tm_transaction* t = (struct tm_transaction*)tx;
    for(size_t i = 0; i < num; i++){
        bool result = read_word(tx, wc + i, target + i * region-> align); 
        if(!result){
            printf("%lu,read fail!!!!!\n",pthread_self());
            t -> success = false;
            tm_end(shared, tx);
            return false;
        }       
    }
    printf("%lu,leave_tm_read\n",pthread_self());
    return true;
}


/** [thread-safe] Return the size (in bytes) of the first allocated segment of the shared memory region.
 * @param shared Shared memory region to query
 * @return First allocated segment size
**/
size_t tm_size(shared_t shared) {
    // TODO: tm_size(shared_t)
    return ((struct region *)shared)->size;
}

/** [thread-safe] Return the alignment (in bytes) of the memory accesses on the given shared memory region.
 * @param shared Shared memory region to query
 * @return Alignment used globally
**/
size_t tm_align(shared_t shared) {
    // TODO: tm_align(shared_t)
    return ((struct region *)shared)->align;
}

/** [thread-safe] Return the start address of the first allocated segment in the shared memory region.
 * @param shared Shared memory region to query
 * @return Start address of the first allocated segment
**/
void* tm_start(shared_t shared) {
    printf("enter_tm_start\n");
    printf("tm_start:%p\n", ((struct region*) shared)->start);
    return ((struct region*) shared)->start;
}

bool write_word( tx_t tx, struct word_control* wc, const void* source){
    printf("%lu,enter_write_word:%u\n",pthread_self(),wc);
    printf("write_word-1\n");
    struct tm_transaction* t = (struct tm_transaction*)tx;
    pthread_mutex_lock(&((wc)->word_lock));
    if(wc -> write_tm){
        printf("write_word-2\n");
        if(t == wc -> write_tm){
            pthread_mutex_unlock(&((wc)->word_lock));          
            printf("write_word-3\n");
            //memcpy(wc -> not_valid_copy, source, size);
            wc -> not_valid_copy = *((uint64_t*)source);
            printf("%lu,1-write value:%u\n",pthread_self(),*((unsigned int*)source));
            return true;
        }
        else{
            pthread_mutex_unlock(&((wc)->word_lock));
            printf("%lu,1-write word fail!!!!!\n",pthread_self());
            return false;
        }
    } 
    else{
        printf("write_word-4\n");
        wc -> write_tm = t;
        pthread_mutex_unlock(&((wc)->word_lock));
        printf("%lu,t->write_index:%d\n",pthread_self(),t->write_index);
        (t -> write_words + t->write_index)-> control_word_start = wc;
        ++(t->write_index);

        printf("write size:%u\n", size);
        printf("%lu,write to address:%u\n",pthread_self(),wc -> not_valid_copy);           
        //memcpy(wc -> not_valid_copy, source, size);
        wc -> not_valid_copy = *((uint64_t*)source);
        printf("%lu,2-write value:%u\n",pthread_self(),*((unsigned int*)source));
        printf("write_word-8\n");           
        printf("leave_write_word\n");
         
        return true;
    } 
}


/** [thread-safe] Write operation in the given transaction, source in a private region and target in the shared region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in a private region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the alignment
 * @param target Target start address (in the shared region)
 * @return Whether the whole transaction can continue
**/
bool tm_write(shared_t shared, tx_t tx, void const* source, size_t size, void* target) {
    // TODO: tm_write(shared_t, tx_t, void const*, size_t, void*)
    printf("%lu,enter_tm_write\n",pthread_self());
    //struct word_control* wc = findWordControl((struct region*)shared, target);
    struct real_word* rw = (struct real_word*)target;
    struct word_control* wc = rw -> control_word_start;
    printf("%lu,enter_tm_write:%u\n",pthread_self(),wc);
    struct tm_transaction* t = (struct tm_transaction*)tx;
    struct region *region = (struct region *)shared;
    size_t num = size / region -> align;
    for(size_t i = 0; i < num; i++){        
        bool result = write_word(tx, wc + i, source + i * region-> align);       
        if(!result){
            printf("%lu,write fail!!!!!\n",pthread_self());
            t -> success = false;
            tm_end(shared, tx);
            return false;
        }
    }
    printf("%lu,leave_tm_write\n",pthread_self());
    return true;
}