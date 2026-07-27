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
_Static_assert(sizeof(struct LunaTestPair) == 8,
               "C pair layout must match Luna");
_Static_assert(sizeof(struct LunaTestMixed) == 16,
               "C mixed layout must match Luna");
_Static_assert(_Alignof(struct LunaTestMixed) == 8,
               "C mixed alignment must match Luna");
_Static_assert(__builtin_offsetof(struct LunaTestMixed, tag) == 8,
               "C mixed field offset must match Luna");
_Static_assert(sizeof(struct LunaTestFloatPair) == 16,
               "C float pair layout must match Luna");
_Static_assert(_Alignof(struct LunaTestFloatPair) == 8,
               "C float pair alignment must match Luna");
_Static_assert(sizeof(struct LunaTestTaggedWeight) == 16,
               "C tagged weight layout must match Luna");
_Static_assert(_Alignof(struct LunaTestTaggedWeight) == 8,
               "C tagged weight alignment must match Luna");
_Static_assert(__builtin_offsetof(struct LunaTestTaggedWeight, weight) == 8,
               "C tagged weight field offset must match Luna");
_Static_assert(sizeof(struct LunaTestTriple) == 12,
               "C triple layout must match Luna");
_Static_assert(_Alignof(struct LunaTestTriple) == 4,
               "C triple alignment must match Luna");
_Static_assert(sizeof(struct LunaTestBig) == 24,
               "C big layout must match Luna");
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

unsigned int luna_test_initialized_value = 20U;
unsigned int luna_test_zero_initialized_value;
unsigned int *luna_test_initialized_pointer = &luna_test_initialized_value;

unsigned int c_data_bss_round_trip(void) {
    luna_test_zero_initialized_value = *luna_test_initialized_pointer + 22U;
    return luna_test_zero_initialized_value;
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

int c_stack_integer(int first, int second, int third, int fourth, int fifth,
                    int sixth, signed char seventh, long long eighth) {
    return first == 10 && second == 11 && third == 12 && fourth == 13 &&
                   fifth == 14 && sixth == 15 && seventh == -101 &&
                   eighth == -5000000000LL
               ? 42
               : 1;
}

double c_stack_float(double first, double second, double third, double fourth,
                     double fifth, double sixth, double seventh, double eighth,
                     double ninth, double tenth) {
    return first == 1.0 && second == 2.0 && third == 3.0 && fourth == 4.0 &&
                   fifth == 5.0 && sixth == 6.0 && seventh == 7.0 &&
                   eighth == 8.0 && ninth == 9.0 && tenth == 10.0
               ? 42.0
               : 1.0;
}

_Bool c_stack_mixed(int i0, float f0, int i1, double f1, int i2, float f2,
                    int i3, double f3, int i4, float f4, int i5, double f5,
                    int i6, float f6, double f7, float f8) {
    return i0 == 10 && f0 == 1.0F && i1 == 11 && f1 == 2.0 && i2 == 12 &&
           f2 == 3.0F && i3 == 13 && f3 == 4.0 && i4 == 14 && f4 == 5.0F &&
           i5 == 15 && f5 == 6.0 && i6 == 16 && f6 == 7.0F && f7 == 8.0 &&
           f8 == 9.0F;
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

int c_sum_pair(struct LunaTestPair value) {
    return value.left + value.right;
}

struct LunaTestPair c_make_pair(int left, int right) {
    return (struct LunaTestPair){
        .left = left,
        .right = right,
    };
}

struct LunaTestMixed c_adjust_mixed(struct LunaTestMixed value) {
    value.weight += 1.5;
    value.tag += 2;
    return value;
}

struct LunaTestFloatPair c_echo_float_pair(struct LunaTestFloatPair value) {
    return value;
}

struct LunaTestTaggedWeight
c_echo_tagged_weight(struct LunaTestTaggedWeight value) {
    return value;
}

struct LunaTestTriple c_echo_triple(struct LunaTestTriple value) {
    return value;
}

union LunaTestNumberBits c_echo_number_bits(union LunaTestNumberBits value) {
    return value;
}

struct LunaTestBig c_echo_big(struct LunaTestBig value) {
    return value;
}

struct LunaTestBig c_make_big(long long first, long long second,
                              long long third) {
    return (struct LunaTestBig){
        .first = first,
        .second = second,
        .third = third,
    };
}

int c_rollback_pair(int first, int second, int third, int fourth, int fifth,
                    int sixth, struct LunaTestPair value) {
    return first + second + third + fourth + fifth + sixth + value.left +
           value.right;
}
