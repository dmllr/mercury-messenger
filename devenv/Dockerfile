FROM ubuntu:24.04

RUN apt-get update
RUN apt-get install -y \
    curl wget ninja-build git pkg-config rsync \
    software-properties-common gnupg lsb-release

# Install CMake 4.0+ from Kitware repository
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null && \
    echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ noble main' | tee /etc/apt/sources.list.d/kitware.list >/dev/null && \
    apt-get update && \
    apt-get install -y kitware-archive-keyring && \
    apt-get install -y cmake

# Install Clang and related tools
ARG LLVM_VERSION=20
RUN wget -qO- https://apt.llvm.org/llvm.sh | bash -s -- ${LLVM_VERSION} && \
    apt-get install -y \
    clang-format-${LLVM_VERSION} clang-tidy-${LLVM_VERSION} clang-tools-${LLVM_VERSION} \
    clang-${LLVM_VERSION} clangd-${LLVM_VERSION} libc++-${LLVM_VERSION}-dev \
    libc++abi-${LLVM_VERSION}-dev libclang-${LLVM_VERSION}-dev liblldb-${LLVM_VERSION}-dev \
    libomp-${LLVM_VERSION}-dev libomp5-${LLVM_VERSION} lld-${LLVM_VERSION} \
    lldb-${LLVM_VERSION} llvm-${LLVM_VERSION}-dev llvm-${LLVM_VERSION}-runtime \
    python3-clang llvm-${LLVM_VERSION}

# Setup alternatives for clang, clang++, and llvm-strip
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${LLVM_VERSION} 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${LLVM_VERSION} 100 && \
    update-alternatives --install /usr/bin/llvm-strip llvm-strip /usr/bin/llvm-strip-${LLVM_VERSION} 100

# Install Rust globally so any host UID/GID can access and use it
ENV RUSTUP_HOME=/usr/local/rustup \
    CARGO_HOME=/usr/local/cargo \
    PATH=/usr/local/cargo/bin:$PATH
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y --no-modify-path && \
    rustup target add x86_64-unknown-linux-gnu && \
    chmod -R a+rwX /usr/local/cargo /usr/local/rustup

# Install additional dependencies
RUN apt-get install -y \
    libpthread-stubs0-dev \
    openssh-client \
    g++-x86-64-linux-gnu
