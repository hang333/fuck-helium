#include "menu_patch.h"

#include <windows.h>
#include <winver.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <intrin.h>

namespace {

constexpr int kExitCommand = 34031;
constexpr int kOptionsCommand = 40015;
constexpr size_t kHookLength = 14;
constexpr std::chrono::milliseconds kChromeDllPollInterval(25);
constexpr int kChromeDllPollCount = 1200;

using AddItemWithVectorIcon =
    void (*)(void* model, int command_id, int string_id, const void* icon);

AddItemWithVectorIcon original_add_item = nullptr;
const void* exit_return_address = nullptr;
const void* options_return_address = nullptr;

struct PendingExit {
  int string_id = 0;
  const void* icon = nullptr;
  bool valid = false;
};

thread_local PendingExit pending_exit;

std::filesystem::path LogPath() {
  wchar_t local_app_data[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(
      L"LOCALAPPDATA", local_app_data, static_cast<DWORD>(std::size(local_app_data)));
  if (length == 0 || length >= std::size(local_app_data)) {
    return {};
  }
  std::filesystem::path directory =
      std::filesystem::path(local_app_data) / L"fuck-helium";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  return directory / L"fuck-helium.log";
}

void Log(std::string_view message) {
  static std::mutex mutex;
  std::lock_guard lock(mutex);
  const std::filesystem::path path = LogPath();
  if (path.empty()) {
    return;
  }
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  std::ofstream stream(path, std::ios::app);
  if (!stream) {
    return;
  }
  stream << std::setfill('0') << '[' << now.wYear << '-' << std::setw(2)
         << now.wMonth << '-' << std::setw(2) << now.wDay << ' '
         << std::setw(2) << now.wHour << ':' << std::setw(2) << now.wMinute
         << ':' << std::setw(2) << now.wSecond << "] " << message << '\n';
}

std::string HexAddress(const void* address) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::uppercase
         << reinterpret_cast<uintptr_t>(address);
  return stream.str();
}

bool IsHeliumHost() {
  wchar_t path[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path))) ==
      0) {
    return false;
  }

  wchar_t system_directory[MAX_PATH] = {};
  const UINT system_length =
      GetSystemDirectoryW(system_directory, static_cast<UINT>(std::size(system_directory)));
  if (system_length == 0 || system_length >= std::size(system_directory)) {
    return false;
  }
  std::wstring version_path(system_directory, system_length);
  version_path += L"\\version.dll";
  const HMODULE version_module = LoadLibraryW(version_path.c_str());
  if (!version_module) {
    return false;
  }
  using GetSize = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
  using GetInfo = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
  using Query = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
  const auto get_size = reinterpret_cast<GetSize>(
      GetProcAddress(version_module, "GetFileVersionInfoSizeW"));
  const auto get_info = reinterpret_cast<GetInfo>(
      GetProcAddress(version_module, "GetFileVersionInfoW"));
  const auto query_value = reinterpret_cast<Query>(
      GetProcAddress(version_module, "VerQueryValueW"));
  if (!get_size || !get_info || !query_value) {
    FreeLibrary(version_module);
    return false;
  }

  DWORD ignored = 0;
  const DWORD size = get_size(path, &ignored);
  if (size == 0) {
    FreeLibrary(version_module);
    return false;
  }
  std::vector<std::byte> data(size);
  if (!get_info(path, 0, size, data.data())) {
    FreeLibrary(version_module);
    return false;
  }

  struct Translation {
    WORD language;
    WORD code_page;
  };
  Translation* translations = nullptr;
  UINT translation_bytes = 0;
  if (!query_value(data.data(), L"\\VarFileInfo\\Translation",
                   reinterpret_cast<void**>(&translations),
                   &translation_bytes) ||
      translation_bytes < sizeof(Translation)) {
    FreeLibrary(version_module);
    return false;
  }

  wchar_t query_path[96] = {};
  swprintf_s(query_path, L"\\StringFileInfo\\%04x%04x\\ProductName",
             translations[0].language, translations[0].code_page);
  wchar_t* product_name = nullptr;
  UINT product_name_length = 0;
  const bool is_helium =
      query_value(data.data(), query_path,
                  reinterpret_cast<void**>(&product_name),
                  &product_name_length) &&
      product_name && _wcsicmp(product_name, L"Helium") == 0;
  FreeLibrary(version_module);
  return is_helium;
}

bool IsBrowserProcess() {
  const wchar_t* command_line = GetCommandLineW();
  return command_line && !wcsstr(command_line, L"--type=") &&
         !wcsstr(command_line, L"-type=");
}

struct ExecutableSection {
  std::byte* begin = nullptr;
  size_t size = 0;

  bool Contains(const void* address, size_t length = 1) const {
    if (!address) {
      return false;
    }
    const auto* value = static_cast<const std::byte*>(address);
    return value >= begin && length <= size &&
           static_cast<size_t>(value - begin) <= size - length;
  }
};

std::optional<ExecutableSection> GetTextSection(HMODULE module) {
  auto* base = reinterpret_cast<std::byte*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    return std::nullopt;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    return std::nullopt;
  }
  const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections;
       ++index, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0 &&
        std::memcmp(section->Name, ".text", 5) == 0) {
      return ExecutableSection{
          base + section->VirtualAddress,
          static_cast<size_t>(section->Misc.VirtualSize)};
    }
  }
  return std::nullopt;
}

int32_t ReadInt32(const std::byte* address) {
  int32_t value = 0;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

void* DirectCallTarget(std::byte* call) {
  if (!call || call[0] != std::byte{0xE8}) {
    return nullptr;
  }
  return call + 5 + ReadInt32(call + 1);
}

bool HasHookablePrologue(const ExecutableSection& text, const void* function) {
  static constexpr std::array<int, kHookLength> signature = {
      0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83,
      0xEC, -1, 0x44, 0x89, 0xC6, 0x89, 0xD7};
  if (!text.Contains(function, signature.size())) {
    return false;
  }
  const auto* bytes = static_cast<const uint8_t*>(function);
  for (size_t index = 0; index < signature.size(); ++index) {
    if (signature[index] >= 0 && bytes[index] != signature[index]) {
      return false;
    }
  }
  return true;
}

struct CommandCall {
  std::byte* command = nullptr;
  std::byte* call = nullptr;
  void* target = nullptr;
};

std::vector<CommandCall> FindCommandCalls(const ExecutableSection& text,
                                          int command_id) {
  std::vector<CommandCall> matches;
  std::array<std::byte, 5> command = {std::byte{0xBA}};
  std::memcpy(command.data() + 1, &command_id, sizeof(command_id));

  for (size_t offset = 0; offset + 40 < text.size; ++offset) {
    std::byte* current = text.begin + offset;
    if (std::memcmp(current, command.data(), command.size()) != 0) {
      continue;
    }
    for (size_t distance = command.size(); distance < 36; ++distance) {
      std::byte* possible_call = current + distance;
      if (possible_call[0] != std::byte{0xE8}) {
        continue;
      }
      void* target = DirectCallTarget(possible_call);
      if (HasHookablePrologue(text, target)) {
        matches.push_back({current, possible_call, target});
      }
      break;
    }
  }
  return matches;
}

std::byte* FindSeparatorCall(const ExecutableSection& text,
                             const CommandCall& exit_call) {
  if (exit_call.command < text.begin + 128) {
    return nullptr;
  }
  std::byte* search_begin = exit_call.command - 128;
  std::byte* result = nullptr;
  for (std::byte* current = search_begin; current + 7 < exit_call.command;
       ++current) {
    if (current[0] == std::byte{0x48} && current[1] == std::byte{0x89} &&
        current[3] == std::byte{0x31} && current[4] == std::byte{0xD2} &&
        current[5] == std::byte{0xE8}) {
      void* target = DirectCallTarget(current + 5);
      if (text.Contains(target)) {
        result = current + 5;
      }
    }
  }
  return result;
}

bool WriteCode(void* destination, const void* source, size_t length) {
  DWORD old_protection = 0;
  if (!VirtualProtect(destination, length, PAGE_EXECUTE_READWRITE,
                      &old_protection)) {
    return false;
  }
  std::memcpy(destination, source, length);
  FlushInstructionCache(GetCurrentProcess(), destination, length);
  DWORD ignored = 0;
  return VirtualProtect(destination, length, old_protection, &ignored) != FALSE;
}

std::array<std::byte, kHookLength> AbsoluteJump(const void* destination) {
  std::array<std::byte, kHookLength> jump = {
      std::byte{0xFF}, std::byte{0x25}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}};
  std::memcpy(jump.data() + 6, &destination, sizeof(destination));
  return jump;
}

void HookedAddItem(void* model,
                   int command_id,
                   int string_id,
                   const void* icon) {
  const void* return_address = _ReturnAddress();
  if (return_address == exit_return_address && command_id == kExitCommand) {
    pending_exit = {string_id, icon, true};
    return;
  }

  original_add_item(model, command_id, string_id, icon);

  if (return_address == options_return_address &&
      command_id == kOptionsCommand && pending_exit.valid) {
    const PendingExit exit = pending_exit;
    pending_exit = {};
    original_add_item(model, kExitCommand, exit.string_id, exit.icon);
  }
}

bool InstallHook(const ExecutableSection& /*text*/,
                 const CommandCall& exit_call,
                 const CommandCall& options_call,
                 std::byte* separator_call) {
  void* helper = exit_call.target;
  void* trampoline = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
  if (!trampoline) {
    Log("VirtualAlloc for the hook trampoline failed");
    return false;
  }

  std::memcpy(trampoline, helper, kHookLength);
  const auto jump_back = AbsoluteJump(static_cast<std::byte*>(helper) + kHookLength);
  std::memcpy(static_cast<std::byte*>(trampoline) + kHookLength,
              jump_back.data(), jump_back.size());

  const std::array<std::byte, 5> nops = {
      std::byte{0x90}, std::byte{0x90}, std::byte{0x90},
      std::byte{0x90}, std::byte{0x90}};
  std::array<std::byte, 5> original_separator = {};
  std::memcpy(original_separator.data(), separator_call,
              original_separator.size());
  if (!WriteCode(separator_call, nops.data(), nops.size())) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    Log("failed to suppress the More tools separator");
    return false;
  }

  original_add_item = reinterpret_cast<AddItemWithVectorIcon>(trampoline);
  exit_return_address = exit_call.call + 5;
  options_return_address = options_call.call + 5;
  const auto hook_jump = AbsoluteJump(reinterpret_cast<const void*>(&HookedAddItem));
  if (!WriteCode(helper, hook_jump.data(), hook_jump.size())) {
    WriteCode(separator_call, original_separator.data(),
              original_separator.size());
    original_add_item = nullptr;
    exit_return_address = nullptr;
    options_return_address = nullptr;
    VirtualFree(trampoline, 0, MEM_RELEASE);
    Log("failed to install the menu helper hook");
    return false;
  }

  Log("hook installed: helper=" + HexAddress(helper) +
      " exit_call=" + HexAddress(exit_call.call) +
      " options_call=" + HexAddress(options_call.call));
  return true;
}

bool LocateAndPatch(HMODULE chrome_dll) {
  const auto text = GetTextSection(chrome_dll);
  if (!text) {
    Log("chrome.dll has no valid executable .text section");
    return false;
  }

  const std::vector<CommandCall> exits = FindCommandCalls(*text, kExitCommand);
  const std::vector<CommandCall> options =
      FindCommandCalls(*text, kOptionsCommand);

  struct Candidate {
    CommandCall exit;
    CommandCall options;
    std::byte* separator = nullptr;
  };
  std::vector<Candidate> candidates;
  for (const CommandCall& exit : exits) {
    std::byte* separator = FindSeparatorCall(*text, exit);
    if (!separator) {
      continue;
    }
    for (const CommandCall& option : options) {
      const ptrdiff_t distance = option.command - exit.command;
      if (option.target == exit.target && distance > 0 && distance < 0x10000) {
        candidates.push_back({exit, option, separator});
      }
    }
  }

  if (candidates.size() != 1) {
    Log("patch disabled: expected one validated Exit/Settings sequence, found " +
        std::to_string(candidates.size()) + " (exit helpers=" +
        std::to_string(exits.size()) + ", settings helpers=" +
        std::to_string(options.size()) + ")");
    return false;
  }
  return InstallHook(*text, candidates[0].exit, candidates[0].options,
                     candidates[0].separator);
}

DWORD WINAPI PatchWorker(void*) {
  if (!IsBrowserProcess()) {
    return 0;
  }
  if (!IsHeliumHost()) {
    Log("host ProductName is not Helium; patch skipped");
    return 0;
  }

  for (int attempt = 0; attempt < kChromeDllPollCount; ++attempt) {
    if (HMODULE chrome_dll = GetModuleHandleW(L"chrome.dll")) {
      LocateAndPatch(chrome_dll);
      return 0;
    }
    Sleep(static_cast<DWORD>(kChromeDllPollInterval.count()));
  }
  Log("chrome.dll did not load within 30 seconds; patch skipped");
  return 0;
}

}  // namespace

void StartMenuPatchWorker() {
  HANDLE thread = CreateThread(nullptr, 0, &PatchWorker, nullptr, 0, nullptr);
  if (thread) {
    CloseHandle(thread);
  }
}
