FROM ubuntu:22.04

# Update package lists
RUN apt-get update

# Install essential build tools
RUN apt-get install -y \
    wget cmake ninja-build git pkg-config rsync chmod \
    software-properties-common gnupg lsb-release

# Install Clang 18 and related tools
RUN wget -qO- https://apt.llvm.org/llvm.sh | bash -s -- 18 && \
    apt-get install -y \
    clang-format-18 clang-tidy-18 clang-tools-18 clang-18 clangd-18 \
    libc++-18-dev libc++abi-18-dev libclang-18-dev liblldb-18-dev \
    libomp-18-dev libomp5-18 lld-18 lldb-18 llvm-18-dev llvm-18-runtime python3-clang

# Install x86_64 cross-compilation toolchain
RUN apt-get install -y \
    gcc-11-x86-64-linux-gnu g++-11-x86-64-linux-gnu \
    gcc-multilib-x86-64-linux-gnu  # Provides necessary x86_64 libc headers

# Install additional x86_64 libraries
RUN apt-get install -y \
    libc++-18-dev-x86-64-cross \
    libc6-x86-64-cross \
    libmvec1-x86-64-cross \
    libm-dev-x86-64-cross

# Install additional dependencies
RUN apt-get install -y \
    libpthread-stubs0-dev libmbedtls-dev libmbedtls14 \
