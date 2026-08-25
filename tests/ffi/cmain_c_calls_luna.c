/* Host C main for the C-calls-Luna FFI case (c_calls_luna.la): linked by the
 * host gcc against the ELF64 ET_REL object emitted by luna-as --emit elf.
 * Returns 42 only when every Luna-exported function behaves as expected
 * across the SysV boundary, otherwise a distinct nonzero code. */

extern int luna_answer(void);
extern int luna_add3(int a, int b, int c);
extern int luna_widen(unsigned char x);
extern long luna_add64(long a, long b);

int main(void) {
    if (luna_answer() != 40) {
        return 1;
    }
    if (luna_add3(10, 20, 12) != 42) {
        return 2;
    }
    /* Zero-extension across the boundary: 200 must not arrive as -56. */
    if (luna_widen(200) != 200) {
        return 3;
    }
    /* Full 64-bit width: the sum overflows a 32-bit int. */
    if (luna_add64(2000000000L, 2000000000L) != 4000000000L) {
        return 4;
    }
    return 42;
}
