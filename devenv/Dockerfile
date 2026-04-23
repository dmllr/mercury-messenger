FROM ubuntu:24.04

RUN apt-get update
RUN apt-get install -y \
    curl wget ninja-build git pkg-config rsync \
    software-properties-common gnupg lsb-release

# Install CMake 4.0+ from Kitware repository
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null && \
    echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main' | tee /etc/apt/sources.list.d/kitware.list >/dev/null && \
    apt-get update && \
    apt-get install -y kitware-archive-keyring && \
    apt-get install -y cmake

# Install Clang 18 and related tools
# Added 'llvm-18' to get tools like llvm-strip
RUN wget -qO- https://apt.llvm.org/llvm.sh | bash -s -- 18 && \
    apt-get install -y \
    clang-format-18 clang-tidy-18 clang-tools-18 clang-18 clangd-18 \
    libc++-18-dev libc++abi-18-dev libclang-18-dev liblldb-18-dev \
    libomp-18-dev libomp5-18 lld-18 lldb-18 llvm-18-dev llvm-18-runtime python3-clang \
    llvm-18

# Setup alternatives for clang, clang++, and llvm-strip
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 && \
    update-alternatives --install /usr/bin/llvm-strip llvm-strip /usr/bin/llvm-strip-18 100

# Install Rust globally so any host UID/GID can access and use it
ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/cargo/bin:$PATH
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y --no-modify-path && \
    chmod -R a+rwX /usr/local/cargo /usr/local/rustup

# Install additional dependencies
RUN apt-get install -y \
    libpthread-stubs0-dev \
