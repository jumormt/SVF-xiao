/* dup_b.c */
static int helper(int x) { return x - 1; }
int entry_b(int x) { return helper(x); }
int main(void) { return entry_b(1); }
