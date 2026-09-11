#pragma once
// ARM64 capability bits from FreeBSD sys/arm64/include/elf.h.
#define HWCAP_ASIMDDP (1UL << 20)
#if !defined(TEST_OLD_HWCAP)
#define HWCAP2_SVE2 (1UL << 1)
#define HWCAP2_I8MM (1UL << 13)
#define HWCAP2_SVE2P1 (1ULL << 36)
#endif
