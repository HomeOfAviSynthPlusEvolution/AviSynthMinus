#pragma once
#define AT_HWCAP 16
#if !defined(TEST_FREEBSD) || !defined(TEST_OLD_HWCAP)
#define AT_HWCAP2 26
#endif
#if defined(TEST_FREEBSD)
int elf_aux_info(int, void*, int);
#else
// uint64_t models AArch64 unsigned long even on Windows test hosts (LLP64).
uint64_t getauxval(unsigned long);
#endif
