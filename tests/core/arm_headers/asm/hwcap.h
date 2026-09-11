#pragma once
#if defined(TEST_FREEBSD)
#error FreeBSD must not include Linux ARM capability headers
#endif
// Linux ARM64 uses the same capability bit assignments as FreeBSD.
#include <machine/elf.h>
