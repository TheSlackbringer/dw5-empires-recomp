// Authored, disc-free native process fixture. Never linked into the game or launcher.
#include "../../rexglue-sdk/thirdparty/inja/third_party/include/nlohmann/json.hpp"
#include "setup/install_location.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace dw5e::setup;
namespace fs = std::filesystem;
static void write(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
static fs::path environmentPath(const char *name)
{
#ifdef _WIN32
    const auto key = pathFromText(name).wstring();
    const wchar_t *value = _wgetenv(key.c_str());
    return value ? fs::path(value) : fs::path{};
#else
    const char *value = std::getenv(name);
    return value ? pathFromText(value) : fs::path{};
#endif
}
int main(int argc, char **argv)
{
    const auto args = nativeArguments(argc, argv);
    const auto reports = environmentPath("DW5E_FIXTURE_REPORT");
    const auto mode = args.size() > 1 ? args[1] : "game";
    std::string error;
    const auto self =
        executableDirectory(error) / pathFromText(pathText(pathFromText(args.at(0)).filename()));
    if(mode=="hold") {
        std::cout<<"ready"<<std::endl;
        return std::cin.get()==EOF?5:0;
    }
    if (mode == "bootstrap-probe") {
        write(reports / "bootstrap.json",nlohmann::json{{"args",args},{"cwd",pathText(fs::current_path())}}.dump());
        const char *exit=std::getenv("DW5E_FIXTURE_EXIT");return exit?std::atoi(exit):0;
    }
    if (mode == "codegen")
    {
        std::ifstream in(pathFromText(args.at(2)));
        std::string line;
        while (std::getline(in, line))
            if (line.starts_with("out_directory_path = "))
            {
                const auto path = pathFromText(nlohmann::json::parse(line.substr(21)).get<std::string>());
                write(path / "sources.cmake", "# authored synthetic output\n");
                write(path / "dw5e_pch.h", "// authored synthetic output\n");
            }
    }
    else if (mode == "-S")
    {
        fs::path build;
        for (size_t i = 1; i + 1 < args.size(); ++i)
            if (args[i] == "-B")
                build = pathFromText(args[i + 1]);
        write(build / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY:INTERNAL=" + args.at(2) + "\n");
        if (!reports.empty())
            write(reports / "configure.json",
                  nlohmann::json{{"args", args}, {"cwd", pathText(fs::current_path())}}.dump());
    }
    else if (mode == "--build")
    {
#ifdef _WIN32
        const char *name = "dw5e.exe";
#else
        const char *name = "dw5e";
#endif
        fs::copy_file(self, pathFromText(args.at(2)) / name, fs::copy_options::overwrite_existing);
    }
    else
    {
        if (!reports.empty())
            write(reports / "game.json", nlohmann::json{{"args", args},
                                                        {"cwd", pathText(fs::current_path())}
#ifdef _WIN32
                                                        ,
                                                        {"pid", GetCurrentProcessId()}
#endif
                                         }.dump());
#ifdef _WIN32
        const auto eventName = environmentPath("DW5E_FIXTURE_EVENT").wstring();
        if (!eventName.empty())
        {
            const auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
            if (!event)
                return 3;
            SetEvent(event);
            CloseHandle(event);
            const auto acknowledgement = environmentPath("DW5E_FIXTURE_ACK_EVENT").wstring();
            const auto ack = OpenEventW(SYNCHRONIZE, FALSE, acknowledgement.c_str());
            if (!ack)
                return 4;
            const auto completed = WaitForSingleObject(ack, 10000);
            CloseHandle(ack);
            if (completed != WAIT_OBJECT_0)
                return 5;
        }
#endif
    }
    return 0;
}
