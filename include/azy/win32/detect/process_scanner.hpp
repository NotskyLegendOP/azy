// Azy Skin — Win32 layer: process observation.
//
// Primary mechanism is event-driven (WMI __InstanceCreationEvent /
// __InstanceDeletionEvent on Win32_Process, consumed on Azy's own STA thread
// during message pumping). WMI is optional: if it is unavailable or disabled,
// `find_processes()` still works as a direct snapshot, and the application
// calls it only when its WinEvent observer reports that something changed.
//
// Nothing here opens a handle to Premiere with any write access, injects code,
// or waits on it: only the process list and (elsewhere) QueryFullProcessImageName.
#pragma once

#include <string>
#include <vector>

#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

struct ProcessEvent {
    enum class Kind { Created, Deleted };
    Kind kind = Kind::Created;
    unsigned long pid = 0;
    std::wstring name;  // executable file name, lowercase
};

class ProcessScanner {
public:
    ~ProcessScanner() { shutdown(); }

    // Subscribes to process creation/deletion events. Returns false (with a
    // reason in `error`) when WMI is not usable; Azy then relies on its WinEvent
    // observer + on-demand snapshots, which is still event-driven in practice.
    bool start_events(const std::vector<std::wstring>& lower_case_names, std::string* error);
    void shutdown();
    bool events_available() const { return events_available_; }

    // Non-blocking drain of queued process events.
    bool poll_event(ProcessEvent& out);

    // Direct snapshot: pids whose executable name matches one of the given
    // lower-case names. Cost is one toolhelp snapshot (~1 ms), so callers must
    // call it on state changes only, never on a tight timer.
    std::vector<unsigned long> find_processes(const std::vector<std::wstring>& lower_case_names);

    // Cheap existence check used to notice that Premiere exited.
    bool process_alive(unsigned long pid);

private:
    bool events_available_ = false;
    void* wmi_ = nullptr;  // WmiSubscription*, hidden to keep wbem out of headers
};

}  // namespace win
}  // namespace azy
