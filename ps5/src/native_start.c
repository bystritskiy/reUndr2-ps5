/* reUndr2 PS5, MIT. Native application entry, SysV loader argument block. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <xmmintrin.h>
typedef void (*Nfsu2Init)(void);
extern void _init_env(void *);
extern Nfsu2Init __init_array_start[], __init_array_end[];
extern Nfsu2Init __fini_array_start[], __fini_array_end[];
extern int main(int, char **);
void _init(void) { for(Nfsu2Init *p=__init_array_start;p<__init_array_end;p++)(*p)(); }
void _fini(void) { for(Nfsu2Init *p=__fini_array_end;p>__fini_array_start;)(*--p)(); }
__attribute__((noreturn)) void _start(void *arguments, Nfsu2Init teardown)
{
    _mm_setcsr(0x1f80); __asm__ volatile("fninit");
    _init_env(arguments);
    if(teardown)atexit(teardown);
    atexit(_fini);_init();
    exit(main(*(int *)arguments,(char **)((uint8_t *)arguments+8)));
}
