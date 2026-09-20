#pragma once
#include <windows.h>
#include <string>
namespace sporemp::worker {
// Read process/token identities. Reject an unguarded process sharing this profile.
void reject_process_in_profile(const std::wstring& executable_name, DWORD ignored_pid = 0);
}
