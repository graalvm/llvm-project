#include "../assembly.h"

#ifdef __x86_64__

.text
.balign 4
DEFINE_COMPILERRT_FUNCTION(__x86_return_thunk)
        pop     %r11
        push    %rcx
        mov     (%r11),%ecx
        add     $0x05e1f00d,%ecx  // negated ENDBR64
        jne     1f
        lfence
        pop     %rcx
        jmpq    *%r11
1:
        int3
END_COMPILERRT_FUNCTION(__x86_return_thunk)

#endif // __x86_64__
