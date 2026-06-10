/* Function-pointer table -> indirect call that Andersen must resolve.
 * NOTE: the table is filled by explicit element stores, NOT an initializer
 * list. With clang 10, `op_t ops[2] = {dbl, neg};` lowers to llvm.memcpy
 * from a constant global aggregate, and SVF's MEMCPY summary does not
 * propagate the function pointers through that copy (callees stay
 * unresolved). Explicit stores keep the fixture portable across clangs. */
typedef int (*op_t)(int);
static int dbl(int x) { return x * 2; }
static int neg(int x) { return -x; }
int apply(int which, int x)
{
    op_t ops[2];
    ops[0] = dbl;
    ops[1] = neg;
    return ops[which](x);
}
int main(void) { return apply(0, 21); }
