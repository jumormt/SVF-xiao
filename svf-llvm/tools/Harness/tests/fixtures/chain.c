/* chain.c — deep store/load chain for vfpath step-elision tests.
 * At each level, the pointer is stored to a local slot and loaded again.
 * This forces the SVFG to produce a StoreVFGNode -> (IntraInd) ->
 * LoadVFGNode pair per level, giving roughly 3-4 SVFG steps per hop.
 * With 6 levels the path length exceeds 10, so max_steps=10 triggers
 * middle elision.
 *
 * Source: malloc return in alloc() (line 11).
 * Sink:   the dereference of q on line 25 (return *q). */
#include <stdlib.h>
char* alloc(void) { return (char*)malloc(4); }
char* hop0(char* p) { char* t = p; return t; }
char* hop1(char* p) { char* t = p; return t; }
char* hop2(char* p) { char* t = p; return t; }
char* hop3(char* p) { char* t = p; return t; }
char* hop4(char* p) { char* t = p; return t; }
char* hop5(char* p) { char* t = p; return t; }
char* hop6(char* p) { char* t = p; return t; }
char* hop7(char* p) { char* t = p; return t; }
char* hop8(char* p) { char* t = p; return t; }
char* hop9(char* p) { char* t = p; return t; }
int main(void)
{
    char* q = hop9(hop8(hop7(hop6(hop5(hop4(hop3(hop2(hop1(hop0(alloc()))))))))));
    return *q;
}
