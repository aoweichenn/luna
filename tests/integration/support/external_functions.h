#ifndef LUNA_TEST_EXTERNAL_FUNCTIONS_H
#define LUNA_TEST_EXTERNAL_FUNCTIONS_H

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

#endif
