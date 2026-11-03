// Small native bootstrap: keeps the wizard and its runtime DLLs inside game-files.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int report_error(DWORD code)
{
    const char text[] = "DW5E could not start. Keep DW5E.exe beside its complete game-files folder.\r\n";
    HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written;
    if (output && output != INVALID_HANDLE_VALUE)
        WriteFile(output, text, sizeof(text) - 1, &written, NULL);
    else
        MessageBoxW(NULL, L"DW5E could not start. Keep DW5E.exe beside its complete game-files folder.",
                    L"Dynasty Warriors 5: Empires", MB_OK | MB_ICONERROR);
    return code ? (int)code : 1;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR unused, int show)
{
    (void)instance;
    (void)previous;
    (void)unused;
    (void)show;
    const DWORD capacity = 32768;
    WCHAR *path = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(WCHAR));
    WCHAR *command = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(WCHAR));
    if (!path || !command)
        return report_error(ERROR_NOT_ENOUGH_MEMORY);
    DWORD length = GetModuleFileNameW(NULL, path, capacity);
    if (!length || length >= capacity)
        return report_error(ERROR_INSUFFICIENT_BUFFER);
    while (length && path[length - 1] != L'\\')
        --length;
    path[length] = 0;
    const WCHAR suffix[] = L"game-files\\launcher\\DW5E.exe";
    if (length + sizeof(suffix) / sizeof(WCHAR) >= capacity)
        return report_error(ERROR_INSUFFICIENT_BUFFER);
    lstrcatW(path, suffix);
    // argv[0] cannot contain a quote in a Windows filename. Preserve the remaining command line verbatim.
    const WCHAR *tail = GetCommandLineW();
    if (*tail == L'\"')
    {
        ++tail;
        while (*tail && *tail != L'\"')
            ++tail;
        if (*tail)
            ++tail;
    }
    else
        while (*tail && *tail != L' ' && *tail != L'\t')
            ++tail;
    while (*tail == L' ' || *tail == L'\t')
        ++tail;
    const BOOL manage = !*tail && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (lstrlenW(path) + lstrlenW(tail) + 32 >= capacity)
        return report_error(ERROR_INSUFFICIENT_BUFFER);
    lstrcpyW(command, L"\"");
    lstrcatW(command, path);
    lstrcatW(command, L"\" ");
    lstrcatW(command, tail);
    if (manage)
        lstrcatW(command, L" --manage");
    STARTUPINFOW start;
    PROCESS_INFORMATION process;
    ZeroMemory(&start, sizeof(start));
    ZeroMemory(&process, sizeof(process));
    start.cb = sizeof(start);
    // Keep console diagnostics when invoked by a terminal or test runner.
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(path, command, NULL, NULL, TRUE, 0, NULL, NULL, &start, &process))
        return report_error(GetLastError());
    CloseHandle(process.hThread);
    DWORD result = 1;
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hProcess);
    HeapFree(GetProcessHeap(), 0, command);
    HeapFree(GetProcessHeap(), 0, path);
    return (int)result;
}
