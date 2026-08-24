/*
 * Freestanding C fixture for the end-to-end gcc FFI test (slice F3).
 * tools/selfhost.py compiles this with the host gcc:
 *
 *   gcc -ffreestanding -fno-stack-protector -fno-pic -fno-common
 *       -fno-asynchronous-unwind-tables -mcmodel=small -O1 -Wall -Werror -c
 *
 * and luna-link consumes the resulting ELF64 ET_REL object through the Luna
 * ELF reader. Every libc symbol referenced here is defined by the Luna shim
 * module tests/ffi/shims.luna; nothing else external may be referenced.
 *
 * Typical gcc output under these flags (what the reader must absorb):
 *   .text / .rodata / .data / .bss, .rodata.str1.1 merge sections for the
 *   string literals, .note.gnu.property (SHT_NOTE, dropped), .comment and
 *   .note.GNU-stack (non-allocated, dropped); R_X86_64_PLT32 for the calls,
 *   R_X86_64_32S for absolute global addressing, R_X86_64_64 for the
 *   initialized pointer table entries, R_X86_64_PC32 for rip-relative loads.
 */

#include <stddef.h>
#include <stdarg.h>

/* Declared directly rather than via the hosted headers: these prototypes are
 * the exact freestanding contract the Luna shims implement. */
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
void *malloc(size_t size);
void free(void *memory);
void *calloc(size_t count, size_t size);
void abort(void);

typedef struct FixturePair {
    int left;
    int right;
} FixturePair;

static int fixture_counter;                        /* .bss, zero-initialized */
int fixture_data_global = 42;                      /* .data */
const char *fixture_data_pointer = "fixture-data"; /* .data, R_X86_64_64 */
/* Fully const: lands in .rodata with R_X86_64_64 entries against the string
 * literals' merge section. */
static const char *const fixture_names[] = { "alpha", "beta", "gamma" };

int fixture_string_check(const char *text)
{
    char buffer[32];
    size_t length = strlen(text);
    if (length >= sizeof(buffer)) {
        return -1;
    }
    memcpy(buffer, text, length + 1);
    if (strcmp(buffer, "luna-ffi-fixture") != 0) {
        return -2;
    }
    if (strncmp(buffer, "luna-ffi", 8) != 0) {
        return -3;
    }
    memmove(buffer + 8, buffer, 9); /* overlapping, destination above source */
    if (memcmp(buffer + 8, "luna-ffi", 8) != 0) {
        return -4;
    }
    memset(buffer, 0, sizeof(buffer));
    if (buffer[0] != 0 || buffer[31] != 0) {
        return -5;
    }
    return (int)length;
}

int fixture_malloc_check(void)
{
    unsigned char *memory = (unsigned char *)malloc(64);
    if (memory == (void *)0) {
        return -1;
    }
    memset(memory, 7, 64);
    int total = 0;
    for (int index = 0; index < 64; index++) {
        total += memory[index];
    }
    free(memory);
    return total; /* 64 * 7 = 448 */
}

int fixture_calloc_check(void)
{
    int *values = (int *)calloc(8, sizeof(int));
    if (values == (void *)0) {
        return -1;
    }
    int total = 0;
    for (int index = 0; index < 8; index++) {
        total += values[index]; /* must be zero-filled */
    }
    if (total != 0) {
        free(values);
        return -2;
    }
    values[3] = 42;
    total = values[3];
    free(values);
    return total; /* 42 */
}

FixturePair fixture_pair_make(int left, int right)
{
    FixturePair pair;
    pair.left = left;
    pair.right = right;
    return pair; /* two INTEGER eightbytes: returned in %rax */
}

int fixture_pair_sum(FixturePair pair)
{
    return pair.left + pair.right;
}

const char *fixture_name(unsigned int index)
{
    if (index >= 3) {
        return (const char *)0;
    }
    return fixture_names[index]; /* absolute scaled-index load: R_X86_64_32S */
}

int fixture_counter_next(void)
{
    fixture_counter += 1;
    return fixture_counter;
}

int fixture_data_check(void)
{
    return fixture_data_global;
}

const char *fixture_data_string(void)
{
    return fixture_data_pointer;
}

int fixture_sum(int count, ...)
{
    va_list arguments;
    va_start(arguments, count);
    int total = 0;
    for (int index = 0; index < count; index++) {
        total += va_arg(arguments, int);
    }
    va_end(arguments);
    return total;
}

double fixture_sumf(int count, ...)
{
    va_list arguments;
    va_start(arguments, count);
    double total = 0.0;
    for (int index = 0; index < count; index++) {
        total += va_arg(arguments, double);
    }
    va_end(arguments);
    return total;
}

void fixture_require(int condition)
{
    if (!condition) {
        abort();
    }
}
