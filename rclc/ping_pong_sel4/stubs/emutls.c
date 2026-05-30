#include <stdint.h>
#include <stddef.h>

struct __emutls_object
{
    size_t size;
    size_t align;
    union {
        uintptr_t offset;
        void *ptr;
    } loc;
    void *templ;
};

static char emutls_storage[4096] __attribute__((aligned(16)));
static char *emutls_next = emutls_storage;

void *__emutls_get_address(struct __emutls_object *obj)
{
    if (!obj->loc.ptr) {
        uintptr_t addr = (uintptr_t)emutls_next;
        if (obj->align > 1) {
            addr = (addr + obj->align - 1) & ~(obj->align - 1);
        }
        obj->loc.ptr = (void *)addr;
        emutls_next = (char *)(addr + obj->size);
    }
    return obj->loc.ptr;
}
