#include "process_mitigation.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>

namespace {

constexpr DWORD64 kBlockNonMicrosoftBinariesAlwaysOn = DWORD64{1} << 44;

using UpdateProcThreadAttributeFunction = decltype(&UpdateProcThreadAttribute);
using LoadLibraryWFunction = decltype(&LoadLibraryW);
using LoadLibraryExWFunction = decltype(&LoadLibraryExW);
using LoadLibraryExAFunction = decltype(&LoadLibraryExA);

UpdateProcThreadAttributeFunction original_update_proc_thread_attribute =
    &UpdateProcThreadAttribute;
LoadLibraryWFunction original_load_library_w = &LoadLibraryW;
LoadLibraryExWFunction original_load_library_ex_w = &LoadLibraryExW;
LoadLibraryExAFunction original_load_library_ex_a = &LoadLibraryExA;
std::atomic<bool> chrome_import_patched{false};

bool IsBrowserProcess() {
  const wchar_t* command_line = GetCommandLineW();
  return command_line && !wcsstr(command_line, L"--type=") &&
         !wcsstr(command_line, L"-type=");
}

bool IsChromeDll(HMODULE module) {
  if (!module) {
    return false;
  }
  wchar_t path[MAX_PATH] = {};
  const DWORD length =
      GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
  if (length == 0 || length >= std::size(path)) {
    return false;
  }
  const wchar_t* filename = wcsrchr(path, L'\\');
  filename = filename ? filename + 1 : path;
  return _wcsicmp(filename, L"chrome.dll") == 0;
}

bool PatchImport(HMODULE module, const char* name, const void* replacement) {
  if (!module || !name || !replacement) {
    return false;
  }
  auto* base = reinterpret_cast<std::byte*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    return false;
  }
  const auto* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    return false;
  }

  const IMAGE_DATA_DIRECTORY& imports =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (imports.VirtualAddress == 0 || imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    return false;
  }

  auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
      base + imports.VirtualAddress);
  const auto* descriptor_end = reinterpret_cast<const std::byte*>(descriptor) +
                               imports.Size;
  for (; reinterpret_cast<const std::byte*>(descriptor + 1) <= descriptor_end &&
         descriptor->Name != 0;
       ++descriptor) {
    if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0) {
      continue;
    }
    auto* lookup = reinterpret_cast<IMAGE_THUNK_DATA64*>(
        base + descriptor->OriginalFirstThunk);
    auto* address =
        reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
    for (; lookup->u1.AddressOfData != 0; ++lookup, ++address) {
      if (IMAGE_SNAP_BY_ORDINAL64(lookup->u1.Ordinal)) {
        continue;
      }
      const auto* import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
          base + lookup->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char*>(import->Name), name) != 0) {
        continue;
      }

      auto** slot = reinterpret_cast<void**>(&address->u1.Function);
      if (*slot == replacement) {
        return true;
      }
      DWORD old_protection = 0;
      if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE,
                          &old_protection)) {
        return false;
      }
      InterlockedExchangePointer(slot, const_cast<void*>(replacement));
      DWORD ignored = 0;
      VirtualProtect(slot, sizeof(*slot), old_protection, &ignored);
      FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
      return true;
    }
  }
  return false;
}

BOOL WINAPI CompatibleUpdateProcThreadAttribute(
    LPPROC_THREAD_ATTRIBUTE_LIST attribute_list,
    DWORD flags,
    DWORD_PTR attribute,
    PVOID value,
    SIZE_T value_size,
    PVOID previous_value,
    PSIZE_T return_size) {
  if (attribute == PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY && value &&
      value_size >= sizeof(DWORD64)) {
    auto* policy = static_cast<DWORD64*>(value);
    policy[0] &= ~kBlockNonMicrosoftBinariesAlwaysOn;
  }
  return original_update_proc_thread_attribute(
      attribute_list, flags, attribute, value, value_size, previous_value,
      return_size);
}

void PatchIfChromeDll(HMODULE module) {
  if (IsChromeDll(module)) {
    PatchChromeProcessMitigationImport(module);
  }
}

HMODULE WINAPI CompatibleLoadLibraryW(LPCWSTR filename) {
  HMODULE module = original_load_library_w(filename);
  PatchIfChromeDll(module);
  return module;
}

HMODULE WINAPI CompatibleLoadLibraryExW(LPCWSTR filename,
                                        HANDLE file,
                                        DWORD flags) {
  HMODULE module = original_load_library_ex_w(filename, file, flags);
  PatchIfChromeDll(module);
  return module;
}

HMODULE WINAPI CompatibleLoadLibraryExA(LPCSTR filename,
                                        HANDLE file,
                                        DWORD flags) {
  HMODULE module = original_load_library_ex_a(filename, file, flags);
  PatchIfChromeDll(module);
  return module;
}

}  // namespace

void PatchChromeProcessMitigationImport(HMODULE chrome_dll) {
  if (!IsBrowserProcess() || !IsChromeDll(chrome_dll)) {
    return;
  }
  if (PatchImport(chrome_dll, "UpdateProcThreadAttribute",
                  reinterpret_cast<const void*>(
                      &CompatibleUpdateProcThreadAttribute))) {
    chrome_import_patched.store(true, std::memory_order_release);
  }
}

void InstallProcessMitigationCompatibility() {
  if (!IsBrowserProcess()) {
    return;
  }
  HMODULE executable = GetModuleHandleW(nullptr);
  PatchImport(executable, "UpdateProcThreadAttribute",
              reinterpret_cast<const void*>(
                  &CompatibleUpdateProcThreadAttribute));
  PatchImport(executable, "LoadLibraryW",
              reinterpret_cast<const void*>(&CompatibleLoadLibraryW));
  PatchImport(executable, "LoadLibraryExW",
              reinterpret_cast<const void*>(&CompatibleLoadLibraryExW));
  PatchImport(executable, "LoadLibraryExA",
              reinterpret_cast<const void*>(&CompatibleLoadLibraryExA));

  PatchChromeProcessMitigationImport(GetModuleHandleW(L"chrome.dll"));
}
