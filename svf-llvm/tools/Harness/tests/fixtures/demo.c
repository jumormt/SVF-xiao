#include <stdlib.h>
#include <string.h>

char* make_buf(int n) { return (char*)malloc(n); }   // source: malloc ret
void fill(char* p) { memset(p, 0, 8); }
int use_after_free(void)
{
    char* b = make_buf(8);
    fill(b);
    free(b);
    return b[0];                                      // sink: load after free
}
int main(void) { return use_after_free(); }
/* long-ir fixture: a call with 20 arguments so the CallICFGNode's toString()
 * exceeds the ~200-byte evidence "ir" cap — exercises ir truncation in cfg
 * results (Task 4.3 test obligation). Appended BELOW the original functions
 * so the line numbers above (4/8/9/10/11) used by tests stay stable. */
int long_ir_helper(int a0, int a1, int a2, int a3, int a4, int a5, int a6,
                   int a7, int a8, int a9, int a10, int a11, int a12, int a13,
                   int a14, int a15, int a16, int a17, int a18, int a19)
{
    return a0 + a19;
}
int long_ir(void)
{
    return long_ir_helper(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                          15, 16, 17, 18, 19);
}
