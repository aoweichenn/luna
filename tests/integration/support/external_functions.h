#ifndef LUNA_TEST_EXTERNAL_FUNCTIONS_H
#define LUNA_TEST_EXTERNAL_FUNCTIONS_H

struct LunaTestPayload {
    unsigned char small;
    unsigned long long wide;
};

union LunaTestNumberBits {
    unsigned long long whole;
    unsigned char low;
};

struct LunaTestState {
    unsigned char kind;
    struct LunaTestPayload payload;
    union LunaTestNumberBits bits;
    unsigned short values[2];
    struct LunaTestState *next;
};

_Bool c_bool_flip(_Bool value);
signed char c_i8_identity(signed char value);
short c_i16_identity(short value);
int c_i32_identity(int value);
long long c_i64_identity(long long value);
long c_isize_identity(long value);
unsigned char c_u8_identity(unsigned char value);
unsigned short c_u16_identity(unsigned short value);
unsigned int c_u32_identity(unsigned int value);
unsigned long long c_u64_identity(unsigned long long value);
unsigned long c_usize_identity(unsigned long value);
float c_f32_identity(float value);
double c_f64_identity(double value);
long long c_promote_i8(signed char value);
long long c_promote_i16(short value);
int *c_pointer_identity(int *value);
void *c_void_pointer_identity(void *value);
void c_store_i32(int *pointer, int value);
_Bool c_mixed_register_banks(signed char first, unsigned short second,
                             int third, unsigned long long fourth, void *fifth,
                             long sixth, float seventh, double eighth);
int c_stack_integer(int first, int second, int third, int fourth, int fifth,
                    int sixth, signed char seventh, long long eighth);
double c_stack_float(double first, double second, double third, double fourth,
                     double fifth, double sixth, double seventh, double eighth,
                     double ninth, double tenth);
_Bool c_stack_mixed(int i0, float f0, int i1, double f1, int i2, float f2,
                    int i3, double f3, int i4, float f4, int i5, double f5,
                    int i6, float f6, double f7, float f8);
_Bool c_validate_aggregate_layout(const struct LunaTestState *state);

#endif
