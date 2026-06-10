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
