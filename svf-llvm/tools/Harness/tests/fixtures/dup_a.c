/* dup_a.c */
static int helper(int x) { return x + 1; }
int entry_a(int x) { return helper(x); }
