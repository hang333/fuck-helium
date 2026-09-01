#include <windows.h>

#include <string>

#include "menu_patch.h"
#include "process_mitigation.h"

namespace {

HMODULE SystemVersionModule() {
  static HMODULE module = [] {
    wchar_t system_dir[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
      return static_cast<HMODULE>(nullptr);
    }
    std::wstring path(system_dir, length);
    path += L"\\version.dll";
    return LoadLibraryW(path.c_str());
  }();
  return module;
}

template <typename Function>
Function Resolve(const char* name) {
  const HMODULE module = SystemVersionModule();
  return module ? reinterpret_cast<Function>(GetProcAddress(module, name))
                : nullptr;
}

}  // namespace

extern "C" DWORD WINAPI ProxyGetFileVersionInfoSizeW(LPCWSTR filename,
                                                       LPDWORD handle) {
  using Function = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
  static const Function function = Resolve<Function>("GetFileVersionInfoSizeW");
  if (!function) {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
  }
  return function(filename, handle);
}

extern "C" BOOL WINAPI ProxyGetFileVersionInfoW(
    LPCWSTR filename,
    DWORD handle,
    DWORD length,
    LPVOID data) {
  using Function = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
  static const Function function = Resolve<Function>("GetFileVersionInfoW");
  if (!function) {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return FALSE;
  }
  return function(filename, handle, length, data);
}

extern "C" BOOL WINAPI ProxyVerQueryValueW(
    LPCVOID block,
    LPCWSTR sub_block,
    LPVOID* buffer,
    PUINT length) {
  using Function = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
  static const Function function = Resolve<Function>("VerQueryValueW");
  if (!function) {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return FALSE;
  }
  return function(block, sub_block, buffer, length);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    InstallProcessMitigationCompatibility();
    StartMenuPatchWorker();
  }
  return TRUE;
}
