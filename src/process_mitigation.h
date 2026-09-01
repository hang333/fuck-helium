#ifndef FUCK_HELIUM_SRC_PROCESS_MITIGATION_H_
#define FUCK_HELIUM_SRC_PROCESS_MITIGATION_H_

#include <windows.h>

void InstallProcessMitigationCompatibility();
void PatchChromeProcessMitigationImport(HMODULE chrome_dll);

#endif  // FUCK_HELIUM_SRC_PROCESS_MITIGATION_H_
