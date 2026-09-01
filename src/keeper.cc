#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr DWORD kRepairIntervalMs = 500;
constexpr int kStableIntervalsBeforeRepair = 10;

uint64_t HashPath(std::wstring_view value) {
  uint64_t hash = 1469598103934665603ULL;
  for (wchar_t character : value) {
    hash ^= static_cast<uint16_t>(towlower(character));
    hash *= 1099511628211ULL;
  }
  return hash;
}

void Log(const std::filesystem::path& source, std::wstring_view message) {
  const std::filesystem::path path = source.parent_path() / L"keeper.log";
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  std::wofstream stream(path, std::ios::app);
  if (stream) {
    stream << L'[' << now.wYear << L'-' << now.wMonth << L'-' << now.wDay
           << L' ' << now.wHour << L':' << now.wMinute << L':' << now.wSecond
           << L"] " << message << L'\n';
  }
}

bool Repair(const std::filesystem::path& source,
            const std::filesystem::path& target) {
  if (std::filesystem::exists(target)) {
    return true;
  }
  const std::filesystem::path temporary = target.wstring() + L".tmp";
  DeleteFileW(temporary.c_str());
  if (!CopyFileW(source.c_str(), temporary.c_str(), FALSE)) {
    return false;
  }
  if (!MoveFileExW(temporary.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argument_count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (!arguments || argument_count != 3) {
    if (arguments) {
      LocalFree(arguments);
    }
    return 2;
  }

  const std::filesystem::path source(arguments[1]);
  const std::filesystem::path application(arguments[2]);
  LocalFree(arguments);
  if (!std::filesystem::is_regular_file(source) ||
      !std::filesystem::is_regular_file(application / L"chrome.exe")) {
    return 3;
  }

  const std::wstring canonical_target =
      std::filesystem::weakly_canonical(application).wstring();
  const std::wstring mutex_name =
      L"Local\\fuck-helium-keeper-" + std::to_wstring(HashPath(canonical_target));
  HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
  if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
    if (mutex) {
      CloseHandle(mutex);
    }
    return 0;
  }

  const std::filesystem::path target = application / L"version.dll";
  const std::filesystem::path chrome = application / L"chrome.exe";
  std::filesystem::file_time_type observed_chrome_time = {};
  int stable_intervals = 0;
  bool failure_logged = false;
  for (;;) {
    const bool missing = !std::filesystem::exists(target);
    std::error_code error;
    const auto chrome_time = std::filesystem::last_write_time(chrome, error);
    if (!missing || error) {
      stable_intervals = 0;
      failure_logged = false;
    } else {
      if (chrome_time == observed_chrome_time) {
        ++stable_intervals;
      } else {
        observed_chrome_time = chrome_time;
        stable_intervals = 0;
      }

      if (stable_intervals >= kStableIntervalsBeforeRepair &&
          !Repair(source, target)) {
        if (!failure_logged) {
          Log(source, L"version.dll is missing and could not be restored");
          failure_logged = true;
        }
      } else if (stable_intervals >= kStableIntervalsBeforeRepair) {
        Log(source, L"restored version.dll after an installer/update removed it");
        stable_intervals = 0;
        failure_logged = false;
      }
    }
    Sleep(kRepairIntervalMs);
  }
}
