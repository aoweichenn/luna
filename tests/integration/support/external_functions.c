#include "external_functions.h"

_Static_assert(sizeof(_Bool) == 1, "x86-64 C ABI requires one-byte _Bool");
_Static_assert(sizeof(signed char) == 1,
               "x86-64 C ABI requires one-byte signed char");
_Static_assert(sizeof(short) == 2, "x86-64 C ABI requires two-byte short");
_Static_assert(sizeof(int) == 4, "x86-64 C ABI requires four-byte int");
_Static_assert(sizeof(long) == 8, "x86-64 C ABI requires eight-byte long");
_Static_assert(sizeof(long long) == 8,
               "x86-64 C ABI requires eight-byte long long");
_Static_assert(sizeof(float) == 4, "x86-64 C ABI requires binary32 float");
_Static_assert(sizeof(double) == 8, "x86-64 C ABI requires binary64 double");
_Static_assert(sizeof(void *) == 8, "x86-64 C ABI requires 64-bit pointers");
_Static_assert(sizeof(struct LunaTestPayload) == 16,
               "C payload layout must match Luna");
_Static_assert(_Alignof(struct LunaTestPayload) == 8,
               "C payload alignment must match Luna");
_Static_assert(__builtin_offsetof(struct LunaTestPayload, wide) == 8,
               "C payload field offset must match Luna");
_Static_assert(sizeof(union LunaTestNumberBits) == 8,
               "C union layout must match Luna");
_Static_assert(__builtin_offsetof(union LunaTestNumberBits, low) == 0,
               "C union field offset must match Luna");
_Static_assert(sizeof(struct LunaTestState) == 48,
               "C state layout must match Luna");
_Static_assert(_Alignof(struct LunaTestState) == 8,
               "C state alignment must match Luna");
_Static_assert(__builtin_offsetof(struct LunaTestState, kind) == 0,
               "C state kind offset must match Luna");
_Static_assert(__builtin_offsetof(struct LunaTestState, payload) == 8,
               "C state payload offset must match Luna");
_Static_assert(__builtin_offsetof(struct LunaTestState, bits) == 24,
               "C state union offset must match Luna");
_Static_assert(__builtin_offsetof(struct LunaTestState, values) == 32,
               "C state array offset must match Luna");
_Static_assert(__builtin_offsetof(struct LunaTestState, next) == 40,
               "C state pointer offset must match Luna");

_Bool c_bool_flip(_Bool value) {
    return !value;
}

signed char c_i8_identity(signed char value) {
    return value;
}

short c_i16_identity(short value) {
    return value;
}

int c_i32_identity(int value) {
    return value;
}

long long c_i64_identity(long long value) {
    return value;
}

long c_isize_identity(long value) {
    return value;
}

unsigned char c_u8_identity(unsigned char value) {
    return value;
}

unsigned short c_u16_identity(unsigned short value) {
    return value;
}

unsigned int c_u32_identity(unsigned int value) {
    return value;
}

unsigned long long c_u64_identity(unsigned long long value) {
    return value;
}

unsigned long c_usize_identity(unsigned long value) {
    return value;
}

float c_f32_identity(float value) {
    return value;
}

double c_f64_identity(double value) {
    return value;
}

long long c_promote_i8(signed char value) {
    return value;
}

long long c_promote_i16(short value) {
    return value;
}

int *c_pointer_identity(int *value) {
    return value;
}

void *c_void_pointer_identity(void *value) {
    return value;
}

void c_store_i32(int *pointer, int value) {
    *pointer = value;
}

_Bool c_mixed_register_banks(signed char first, unsigned short second,
                             int third, unsigned long long fourth, void *fifth,
                             long sixth, float seventh, double eighth) {
    return first == -7 && second == 60000U && third == -1234567 &&
           fourth == 18000000000000000000ULL && fifth != (void *)0 &&
           sixth == -4096L && seventh == 1.25F && eighth == -2.5;
}

_Bool c_validate_aggregate_layout(const struct LunaTestState *state) {
    if (state == (const struct LunaTestState *)0 || state->kind != 7U ||
        state->payload.small != 3U || state->payload.wide != 34ULL ||
        state->bits.whole != 42ULL || state->bits.low != 42U ||
        state->values[0] != 0U || state->values[1] != 8U ||
        state->next != state) {
        return 0;
    }

    const unsigned char *bytes = (const unsigned char *)state;
    for (unsigned int index = 1U; index < 8U; index += 1U) {
        if (bytes[index] != 0U) {
            return 0;
        }
    }
    for (unsigned int index = 9U; index < 16U; index += 1U) {
        if (bytes[index] != 0U) {
            return 0;
        }
    }
    for (unsigned int index = 36U; index < 40U; index += 1U) {
        if (bytes[index] != 0U) {
            return 0;
        }
    }
    return 1;
}
