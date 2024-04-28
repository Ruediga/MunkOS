#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "kheap.h"

/*
 * |============|
 * | HOW TO USE |
 * |============|
 * 
 * 1) Define the type the vector should have:
 *    - Must only consist of symbols C allows for variable names
 *    - This means, that e.g. unsigned long long * or struct xy has to be typedef'd
 * typedef struct xy my_struct;
 * typedef unsigned long long * ullptr;
 *
 * 2) Initialize the templates for the array
 *    - DECL in a header file
 *    - TMPL in a c file
 *    - structs MUST be TMPL'ed as NON_NATIVE, natives CAN be (possibly worsening performance)
 *    - NON_NATIVE implementation expects there to be no padding between struct members (?)
 * VECTOR_DECL_TYPE_NON_NATIVE(my_struct)
 * VECTOR_TMPL_TYPE_NON_NATIVE(my_struct)
 * VECTOR_DECL_TYPE(ullptr)
 * VECTOR_TMPL_TYPE(ullptr)
 * 
 * 3) Usage
 * vector_my_struct_t = VECTOR_INIT(my_struct);
*/

#define CONCAT(x, y) x##y

#define VECTOR_NOT_FOUND (size_t)-1

#define __VECTOR_DEFINE_DECLARATIONS_COMMON(T) \
    void __vector_internal_reset_##T(struct CONCAT(vector_##T, _t) *); \
    void __vector_internal_remove_##T(struct CONCAT(vector_##T, _t) *, size_t);

#define __VECTOR_DEFINE_DECLARATIONS(T) \
    __VECTOR_DEFINE_DECLARATIONS_COMMON(T) \
    size_t __vector_internal_push_back_##T(struct CONCAT(vector_##T, _t) *, T); \
    size_t __vector_internal_find_##T(struct CONCAT(vector_##T, _t) *, T); \

#define __VECTOR_DEFINE_DECLARATIONS_NON_NATIVE(T) \
    __VECTOR_DEFINE_DECLARATIONS_COMMON(T) \
    size_t __vector_internal_push_back_##T(struct CONCAT(vector_##T, _t) *, T *); \
    size_t __vector_internal_find_##T(struct CONCAT(vector_##T, _t) *, T *);

#define __VECTOR_DEFINE_TYPE(T) \
    typedef struct CONCAT(vector_##T, _t) { \
        T *data; \
        size_t size; \
        size_t capacity; \
        size_t (*push_back)(struct CONCAT(vector_##T, _t) *, T); \
        void (*reset)(struct CONCAT(vector_##T, _t) *); \
        void (*remove)(struct CONCAT(vector_##T, _t) *, size_t); \
        size_t (*find)(struct CONCAT(vector_##T, _t) *, T); \
    } CONCAT(vector_##T, _t);

#define __VECTOR_DEFINE_TYPE_NON_NATIVE(T) \
    typedef struct CONCAT(vector_##T, _t) { \
        T *data; \
        size_t size; \
        size_t capacity; \
        size_t (*push_back)(struct CONCAT(vector_##T, _t) *, T *); \
        void (*reset)(struct CONCAT(vector_##T, _t) *); \
        void (*remove)(struct CONCAT(vector_##T, _t) *, size_t); \
        size_t (*find)(struct CONCAT(vector_##T, _t) *, T *); \
    } CONCAT(vector_##T, _t);

#define VECTOR_INIT(T) { NULL, 0ul, 0ul, __vector_internal_push_back_##T, __vector_internal_reset_##T, \
    __vector_internal_remove_##T, __vector_internal_find_##T }

#define VECTOR_REINIT(vec, T) do { \
    vec.data = NULL; \
    vec.size = vec.capacity = 0; \
    vec.push_back = __vector_internal_push_back_##T; \
    vec.reset = __vector_internal_reset_##T; \
    vec.remove = __vector_internal_remove_##T; \
    vec.find = __vector_internal_find_##T; \
} while(0)

#define __VECTOR_DEFINE_PUSH_BACK(T) \
    size_t __vector_internal_push_back_##T(CONCAT(vector_##T, _t) *vec, T value) { \
        if (vec->size >= vec->capacity) { \
            vec->capacity = vec->capacity ? vec->capacity * 2 : 8; \
            vec->data = krealloc(vec->data, vec->capacity * sizeof(T)); \
        } \
        vec->data[vec->size++] = value; \
        return vec->size - 1; \
    }

#define __VECTOR_DEFINE_REMOVE(T) \
    void __vector_internal_remove_##T(CONCAT(vector_##T, _t) *vec, size_t idx) { \
        if (idx >= vec->size) return; \
        for (size_t start = idx; start < vec->size - 1; start++) { \
             vec->data[start] = vec->data[start + 1]; \
        } \
        vec->size--; \
    }

#define __VECTOR_DEFINE_FIND(T) \
    size_t __vector_internal_find_##T(CONCAT(vector_##T, _t) *vec, T value) { \
        for (size_t i = 0; i < vec->size; i++) { \
            if (vec->data[i] == value) return i; \
        } \
        return VECTOR_NOT_FOUND; \
    }

#define __VECTOR_DEFINE_PUSH_BACK_NON_NATIVE(T) \
    size_t __vector_internal_push_back_##T(CONCAT(vector_##T, _t) *vec, T *value) { \
        if (vec->size >= vec->capacity) { \
            vec->capacity = vec->capacity ? vec->capacity * 2 : 8; \
            vec->data = krealloc(vec->data, vec->capacity * sizeof(T)); \
        } \
        memcpy(vec->data + vec->size, value, sizeof(T)); \
        vec->size++; \
        return vec->size - 1; \
    }

#define __VECTOR_DEFINE_REMOVE_NON_NATIVE(T) \
    void __vector_internal_remove_##T(CONCAT(vector_##T, _t) *vec, size_t idx) { \
        if (idx >= vec->size) return; \
        memmove(vec->data + idx, vec->data + idx + 1, sizeof(T) * (vec->size - idx - 1)); \
        vec->size--; \
    }

#define __VECTOR_DEFINE_FIND_NON_NATIVE(T) \
    size_t __vector_internal_find_##T(CONCAT(vector_##T, _t) *vec, T *value) { \
        for (size_t i = 0; i < vec->size; i++) { \
            if (!memcmp(vec->data + i, value, sizeof(T))) return i; \
        } \
        return VECTOR_NOT_FOUND; \
    }

#define __VECTOR_DEFINE_RESET(T) \
    void __vector_internal_reset_##T(CONCAT(vector_##T, _t) *vec) { \
        kfree((vec)->data); \
        (vec)->data = NULL; \
        (vec)->size = (vec)->capacity = 0; \
    }

#define __VECTOR_TMPL_TYPE_COMMON(T) \
    __VECTOR_DEFINE_RESET(T)

#define __VECTOR_TMPL_TYPE_NON_NATIVE_EXTRA(T) \
    __VECTOR_DEFINE_REMOVE_NON_NATIVE(T) \
    __VECTOR_DEFINE_FIND_NON_NATIVE(T) \
    __VECTOR_DEFINE_PUSH_BACK_NON_NATIVE(T)

#define __VECTOR_TMPL_TYPE_EXTRA(T) \
    __VECTOR_DEFINE_REMOVE(T) \
    __VECTOR_DEFINE_FIND(T) \
    __VECTOR_DEFINE_PUSH_BACK(T)

#define VECTOR_TMPL_TYPE(T) \
    __VECTOR_TMPL_TYPE_COMMON(T) \
    __VECTOR_TMPL_TYPE_EXTRA(T)

#define VECTOR_TMPL_TYPE_NON_NATIVE(T) \
    __VECTOR_TMPL_TYPE_COMMON(T) \
    __VECTOR_TMPL_TYPE_NON_NATIVE_EXTRA(T)

#define VECTOR_DECL_TYPE(T) \
    __VECTOR_DEFINE_TYPE(T) \
    __VECTOR_DEFINE_DECLARATIONS(T)

#define VECTOR_DECL_TYPE_NON_NATIVE(T) \
    __VECTOR_DEFINE_TYPE_NON_NATIVE(T) \
    __VECTOR_DEFINE_DECLARATIONS_NON_NATIVE(T) 
