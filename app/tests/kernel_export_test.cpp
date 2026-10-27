#include <rex/ppc/func.h>
#include <iostream>
REX_EXTERN(__imp__XamEnumerate);
REX_EXTERN(__imp__XNotifyGetNext);
REX_EXTERN(__imp__NtAllocateVirtualMemory);
REX_EXTERN(__imp__sprintf);
REX_EXTERN(__imp__DbgPrint);

int main()
{
    // Link existing ABI wrappers without executing guest kernel operations.
    PPCFunc *volatile enumerator = &__imp__XamEnumerate;
    PPCFunc *volatile notification = &__imp__XNotifyGetNext;
    PPCFunc *volatile allocation = &__imp__NtAllocateVirtualMemory;
    PPCFunc *volatile formatting = &__imp__sprintf;
    PPCFunc *volatile debug = &__imp__DbgPrint;
    auto *volatile dispatcher = &rex::runtime::ResolveIndirectFunction;
    const bool ok = enumerator && notification && allocation && dispatcher && formatting && debug;
    std::cout << (ok ? "PASS" : "FAIL") << ": kernel wrappers and existing C++ runtime API link\n";
    return ok ? 0 : 1;
}
