#include "src/dw5e_safe_read.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#ifndef _WIN32
#include <sys/mman.h>
#endif

int main()
{
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::size_t page = info.dwPageSize;
    auto *memory = static_cast<uint8_t *>(
        VirtualAlloc(nullptr, 2 * page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!memory) return 2;
#else
    const std::size_t page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto *memory = static_cast<uint8_t *>(
        mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (memory == MAP_FAILED) return 2;
#endif
    std::fill(memory, memory + 2 * page, uint8_t{0x5a});
    std::vector<uint8_t> buffer(page);
    bool exact = dw5e::debug::readMemory(buffer.data(), memory, page) &&
                 std::all_of(buffer.begin(), buffer.end(), [](auto b) { return b == 0x5a; });
#ifdef _WIN32
    DWORD previous = 0;
    if (!VirtualProtect(memory + page, page, PAGE_NOACCESS, &previous)) return 2;
#else
    if (mprotect(memory + page, page, PROT_NONE)) return 2;
#endif
    bool inaccessible = !dw5e::debug::readMemory(buffer.data(), memory + page, page);
    bool partial = !dw5e::debug::readMemory(buffer.data(), memory + page - 8, 16);
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, 2 * page);
#endif
    std::cout << (exact ? "PASS" : "FAIL") << ": complete readable page matches\n"
              << (inaccessible ? "PASS" : "FAIL") << ": inaccessible page fails without faulting\n"
              << (partial ? "PASS" : "FAIL") << ": partial page read is rejected\n";
    return exact && inaccessible && partial ? 0 : 1;
}
