/* Exercise the actual private look helpers over the complete claimed domain. */
#include <assert.h>
#include <stdio.h>
#include "../../../engine/look.c"

int main(void)
{
    for (int d = 1; d < 256; d++) {
        uint32_t reciprocal = se_recip24(d);
        for (unsigned n = 0; n < 65536; n++)
            assert(SE_DIV24(n, reciprocal) == (int)(n / (unsigned)d));
    }
    puts("PASS: exact reciprocal division for all 16,711,680 numerator/divisor pairs");
    return 0;
}
