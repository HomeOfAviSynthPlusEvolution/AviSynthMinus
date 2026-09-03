#pragma once

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace avsut::test {

enum class IsaRequirement {
  Scalar,
  Sse2,
  Ssse3,
  Sse41,
  Avx2,
  Avx2Fma,
  Avx512F,
  Avx512Base,
  Avx512Vbmi,
  Avx512Fast,
};

struct CpuFeatures {
  bool sse2{};
  bool ssse3{};
  bool sse41{};
  bool avx2{};
  bool fma3{};
  bool avx512f{};
  bool avx512cd{};
  bool avx512bw{};
  bool avx512dq{};
  bool avx512vl{};
  bool avx512vbmi{};
  bool avx512vnni{};
  bool avx512vbmi2{};
  bool avx512bitalg{};
  bool avx512vpopcntdq{};

  bool supports(IsaRequirement requirement) const noexcept {
    switch (requirement) {
      case IsaRequirement::Scalar:
        return true;
      case IsaRequirement::Sse2:
        return sse2;
      case IsaRequirement::Ssse3:
        return ssse3;
      case IsaRequirement::Sse41:
        return sse41;
      case IsaRequirement::Avx2:
        return avx2;
      case IsaRequirement::Avx2Fma:
        return avx2 && fma3;
      case IsaRequirement::Avx512F:
        return avx512f;
      case IsaRequirement::Avx512Base:
        return avx512f && avx512cd && avx512bw && avx512dq && avx512vl;
      case IsaRequirement::Avx512Vbmi:
        return avx512vbmi;
      case IsaRequirement::Avx512Fast:
        return supports(IsaRequirement::Avx512Base) && avx512vnni && avx512vbmi && avx512vbmi2 &&
               avx512bitalg && avx512vpopcntdq;
    }
    return false;
  }

  static CpuFeatures detect() {
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    // GCC and Clang include OS support for the vector register state here.
    __builtin_cpu_init();
    return {static_cast<bool>(__builtin_cpu_supports("sse2")),
            static_cast<bool>(__builtin_cpu_supports("ssse3")),
            static_cast<bool>(__builtin_cpu_supports("sse4.1")),
            static_cast<bool>(__builtin_cpu_supports("avx2")),
            static_cast<bool>(__builtin_cpu_supports("fma")),
            static_cast<bool>(__builtin_cpu_supports("avx512f")),
            static_cast<bool>(__builtin_cpu_supports("avx512cd")),
            static_cast<bool>(__builtin_cpu_supports("avx512bw")),
            static_cast<bool>(__builtin_cpu_supports("avx512dq")),
            static_cast<bool>(__builtin_cpu_supports("avx512vl")),
            static_cast<bool>(__builtin_cpu_supports("avx512vbmi")),
            static_cast<bool>(__builtin_cpu_supports("avx512vnni")),
            static_cast<bool>(__builtin_cpu_supports("avx512vbmi2")),
            static_cast<bool>(__builtin_cpu_supports("avx512bitalg")),
            static_cast<bool>(__builtin_cpu_supports("avx512vpopcntdq"))};
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int cpu_info[4]{};
    __cpuidex(cpu_info, 0, 0);
    const unsigned max_basic_leaf = static_cast<unsigned>(cpu_info[0]);

    bool sse2 = false;
    bool ssse3 = false;
    bool sse41 = false;
    bool avx2 = false;
    bool fma3 = false;
    bool avx512f = false;
    bool avx512cd = false;
    bool avx512bw = false;
    bool avx512dq = false;
    bool avx512vl = false;
    bool avx512vbmi = false;
    bool avx512vnni = false;
    bool avx512vbmi2 = false;
    bool avx512bitalg = false;
    bool avx512vpopcntdq = false;

    bool osxsave = false;
    bool avx_cpu = false;
    bool fma_cpu = false;
    unsigned __int64 xcr0 = 0;

    if (max_basic_leaf >= 1) {
      __cpuidex(cpu_info, 1, 0);
      const unsigned ecx = static_cast<unsigned>(cpu_info[2]);
      const unsigned edx = static_cast<unsigned>(cpu_info[3]);
      sse2 = (edx & (1u << 26)) != 0;
      ssse3 = (ecx & (1u << 9)) != 0;
      sse41 = (ecx & (1u << 19)) != 0;
      fma_cpu = (ecx & (1u << 12)) != 0;
      osxsave = (ecx & (1u << 27)) != 0;
      avx_cpu = (ecx & (1u << 28)) != 0;
    }

    // AVX instructions are usable only when the OS saves XMM/YMM state.
    if (osxsave) xcr0 = _xgetbv(0);
    const bool avx_os_support = osxsave && ((xcr0 & 0x6) == 0x6);
    fma3 = fma_cpu && avx_cpu && avx_os_support;

    // AVX-512 additionally needs the opmask and ZMM state enabled by the OS.
    const bool avx512_os_support = avx_cpu && avx_os_support && ((xcr0 & 0xe0) == 0xe0);
    if (max_basic_leaf >= 7) {
      __cpuidex(cpu_info, 7, 0);
      const unsigned ebx = static_cast<unsigned>(cpu_info[1]);
      avx2 = avx_cpu && avx_os_support && (ebx & (1u << 5)) != 0;
      avx512f = avx512_os_support && (ebx & (1u << 16)) != 0;
      avx512dq = avx512_os_support && (ebx & (1u << 17)) != 0;
      avx512cd = avx512_os_support && (ebx & (1u << 28)) != 0;
      avx512bw = avx512_os_support && (ebx & (1u << 30)) != 0;
      avx512vl = avx512_os_support && (ebx & (1u << 31)) != 0;
      avx512vbmi = avx512_os_support && (ebx & (1u << 1)) != 0;
      avx512vbmi2 = avx512_os_support && (ebx & (1u << 6)) != 0;
      avx512vnni = avx512_os_support && (ebx & (1u << 11)) != 0;
      avx512bitalg = avx512_os_support && (ebx & (1u << 12)) != 0;
      avx512vpopcntdq = avx512_os_support && (ebx & (1u << 14)) != 0;
    }

    return {sse2,       ssse3,      sse41,       avx2,         fma3,
            avx512f,    avx512cd,   avx512bw,    avx512dq,     avx512vl,
            avx512vbmi, avx512vnni, avx512vbmi2, avx512bitalg, avx512vpopcntdq};
#else
    return {};
#endif
  }
};

}  // namespace avsut::test
