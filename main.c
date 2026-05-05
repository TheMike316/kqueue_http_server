#include <stdio.h>
#include <stdlib.h>

// maybe I want an init function just for api symmetry reasons; we'll see
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} charray;

int charray_append(charray *arr, const char c) {
    if (!arr) {
        fprintf(stderr, "charray_append: NULL pointer\n");
        return -1;
    }
    if (arr->len >= arr->cap) {
        arr->cap = arr->cap == 0 ? 1024 : arr->cap * 2;
        char *new_buf = realloc(arr->buf, arr->cap);
        if (!new_buf) {
            fprintf(stderr, "realloc failed\n");
            return -1;
        }
        arr->buf = new_buf;
    }
    arr->buf[arr->len++] = c;

    return 0;
}

void charray_reset(charray *arr) {
    arr->len = 0;
}

void charray_destroy(charray *arr) {
    free(arr->buf);
    arr->buf = 0;
    arr->cap = 0;
    arr->len = 0;
}

int main(void) {
    printf("Hello, World!\n");
    return 0;
}
