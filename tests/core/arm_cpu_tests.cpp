// Compile the actual ARM detection path on any build host. Only the OS query
// boundary and platform headers are replaced; no ARM instructions are executed.
#include <gtest/gtest.h>
#include <avs/config.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#undef X86_32
#undef X86_64
#undef AVS_WINDOWS
#undef AVS_MACOS
#undef AVS_LINUX
#undef AVS_BSD
#undef __FreeBSD__
#define ARM64
#if defined(TEST_FREEBSD)
#define AVS_BSD
#define __FreeBSD__ 1
#else
#define AVS_LINUX
#endif

static uint64_t test_hwcap, test_hwcap2;
static int failed_query;
#include "core/cpuid.cpp"

#if defined(TEST_FREEBSD)
int elf_aux_info(int key, void* buffer, int size) {
  EXPECT_EQ(size, sizeof(uint64_t));
  // A failed query must not leave partially written capabilities enabled.
  *static_cast<uint64_t*>(buffer) = key == AT_HWCAP ? test_hwcap : test_hwcap2;
  return key == failed_query ? -1 : 0;
}
#else
uint64_t getauxval(unsigned long key) {
  return key == AT_HWCAP ? test_hwcap : test_hwcap2;
}
#endif

TEST(ArmCpuDetection, DecodesCapabilitiesIndependently) {
  failed_query = 0;
  test_hwcap = test_hwcap2 = 0;
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON);
  test_hwcap = 1UL << 20;
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON | CPUF_ARM_DOTPROD);
  test_hwcap = 0;
  test_hwcap2 = 1UL << 1;
#if defined(TEST_OLD_HWCAP)
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON);
#else
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON | CPUF_ARM_SVE2);
  test_hwcap2 = 1UL << 13;
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON | CPUF_ARM_I8MM);
#endif
}

TEST(ArmCpuDetection, DecodesSve21AboveBit31WhenHeadersSupportIt) {
  failed_query = 0;
  test_hwcap = 0;
  test_hwcap2 = uint64_t{1} << 36;
#if defined(TEST_OLD_HWCAP)
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON);
#else
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON | CPUF_ARM_SVE2_1);
  test_hwcap = 1UL << 20;
  test_hwcap2 |= (1UL << 1) | (1UL << 13);
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON | CPUF_ARM_DOTPROD |
      CPUF_ARM_SVE2 | CPUF_ARM_I8MM | CPUF_ARM_SVE2_1);
#endif
}

#if defined(TEST_FREEBSD)
TEST(ArmCpuDetection, FailedAuxiliaryQueriesDoNotEnableFeatures) {
  test_hwcap = 1UL << 20;
  test_hwcap2 = 0;
  failed_query = AT_HWCAP;
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON);
#if !defined(TEST_OLD_HWCAP)
  test_hwcap = 0;
  test_hwcap2 = ~uint64_t{0};
  failed_query = AT_HWCAP2;
  EXPECT_EQ(ARMCheckForExtensions(), CPUF_ARM_NEON);
#endif
  failed_query = 0;
}
#endif
