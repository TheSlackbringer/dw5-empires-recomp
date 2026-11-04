#include "setup/install_state.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
namespace fs = std::filesystem;
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const fs::path expected = (fs::absolute(fs::u8path(argv[1])) / "dw5e-data").lexically_normal();
    const auto actual = dw5e::setup::playerDataDir().lexically_normal();
    const bool ok = actual == expected;
    std::cout << (ok ? "PASS" : "FAIL")
              << ": default installation is in dw5e-data beside the launcher, independent of working/user-data folders\n";
    return ok ? 0 : 1;
}
