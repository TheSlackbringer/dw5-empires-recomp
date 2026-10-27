#include <rex/ui/d3d12/d3d12_api.h>
#include <cstring>
#include <iostream>

int main()
{
    // Published interface IDs from the bundled Microsoft DXC declarations.
    const GUID converter = {0x5f956ed5, 0x78d1, 0x4b15,
                            {0x82,0x47,0xf7,0x18,0x76,0x14,0xa0,0x41}};
    const GUID utils = {0x4605c4cb, 0x2019, 0x492a,
                        {0xad,0xa4,0x65,0xf2,0x0b,0xb7,0xd6,0x7f}};
    const GUID compiler = {0x8c210bf3, 0x011f, 0x4422,
                           {0x8d,0x70,0x6f,0x9a,0xcb,0x8d,0xb6,0x17}};
    const bool ok = !std::memcmp(&__uuidof(IDxbcConverter), &converter, sizeof(GUID)) &&
                    !std::memcmp(&__uuidof(IDxcUtils), &utils, sizeof(GUID)) &&
                    !std::memcmp(&__uuidof(IDxcCompiler), &compiler, sizeof(GUID));
    std::cout << (ok ? "PASS" : "FAIL") << ": DXC interface IDs link and match their declarations\n";
    return ok ? 0 : 1;
}
