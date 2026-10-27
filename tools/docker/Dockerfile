FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest

LABEL org.opencontainers.image.source=https://github.com/TheSlackbringer/dw5-empires-recomp
LABEL org.opencontainers.image.description="DW5 Empires Linux release builder: Steam Runtime 3 sniper SDK, clang 23, approved libc++ packages"
LABEL org.opencontainers.image.licenses="Steam Runtime: Debian packages under their own licenses; LLVM: Apache-2.0 WITH LLVM-exception"

COPY write_libcxx_manifest.py /usr/local/bin/write_libcxx_manifest.py

RUN curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /etc/apt/trusted.gpg.d/apt.llvm.org.asc \
 && echo "deb http://apt.llvm.org/bullseye/ llvm-toolchain-bullseye-23 main" > /etc/apt/sources.list.d/llvm.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends clang-23 lld-23 llvm-23 \
 && version=$(dpkg-query -W -f='${Version}' clang-23) \
 && mkdir -p /approved-debs && cd /approved-debs \
 && apt-get download libc++-23-dev="$version" libc++1="$version" libc++abi-23-dev="$version" libc++abi1="$version" libunwind-23-dev="$version" llvm-libunwind1="$version" \
 && python3 /usr/local/bin/write_libcxx_manifest.py /approved-debs \
 && rm -rf /var/lib/apt/lists/*

ENV CC=clang-23 CXX=clang++-23
