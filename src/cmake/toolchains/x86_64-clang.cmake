set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Specify Clang as the compiler
set(CMAKE_C_COMPILER clang-18)
set(CMAKE_CXX_COMPILER clang++-18)

# Use LLVM's LLD linker for faster linking
set(CMAKE_LINKER lld-18)

# Set target architecture explicitly
set(CMAKE_C_FLAGS "-target x86_64-linux-gnu --sysroot=/usr/x86_64-linux-gnu")
set(CMAKE_CXX_FLAGS "-target x86_64-linux-gnu --sysroot=/usr/x86_64-linux-gnu -stdlib=libc++")
