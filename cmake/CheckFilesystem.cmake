# Require a linkable C++17 standard filesystem implementation. Older libstdc++
# versions provide it in a separate library, including when used with Clang.
include(CheckCXXSourceCompiles)

function(avs_check_filesystem)
  # A static-library try_compile would not detect missing filesystem symbols.
  set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
  set(source [=[
    #include <filesystem>
    #include <system_error>
    int main(int argc, char** argv) {
      std::error_code ec;
      const auto path = std::filesystem::absolute(argc > 1 ? argv[1] : ".", ec);
      std::filesystem::directory_iterator it(path, ec);
      return std::filesystem::exists(path, ec) ? 0 : 1;
    }
  ]=])
  check_cxx_source_compiles("${source}" AVS_HAS_STD_FILESYSTEM)
  if(NOT AVS_HAS_STD_FILESYSTEM)
    list(APPEND CMAKE_REQUIRED_LIBRARIES stdc++fs)
    check_cxx_source_compiles("${source}" AVS_HAS_STD_FILESYSTEM_WITH_STDCXXFS)
    if(NOT AVS_HAS_STD_FILESYSTEM_WITH_STDCXXFS)
      message(FATAL_ERROR "AviSynthMinus requires a C++17 standard library with linkable std::filesystem support. Upgrade the compiler and standard library; the ghc filesystem fallback is no longer provided.")
    endif()
    target_link_libraries(AvsFilesystem INTERFACE stdc++fs)
  endif()
endfunction()

add_library(AvsFilesystem INTERFACE)
avs_check_filesystem()
