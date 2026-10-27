#pragma once
// Fault-free host reads for the F9 snapshot diagnostic. A partial read is a failure.
#include <cstddef>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace dw5e::debug {
inline bool readMemory(void *destination, const void *source, std::size_t size)
{
#ifdef _WIN32
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), source, destination, size, &copied) &&
           copied == size;
#else
    iovec local{destination, size};
    iovec remote{const_cast<void *>(source), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
#endif
}
} // namespace dw5e::debug
