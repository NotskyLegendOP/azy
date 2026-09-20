#include "azy/win32/detect/process_scanner.hpp"

#include <tlhelp32.h>
#include <wbemidl.h>

#include <mutex>
#include <vector>

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

bool name_matches(const std::wstring& candidate, const std::vector<std::wstring>& names) {
    for (const std::wstring& name : names) {
        if (iequals_wide(candidate, name)) return true;
    }
    return false;
}

std::wstring lower_wide(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return out;
}

// --- WMI asynchronous sink -------------------------------------------------

class WmiSink : public IWbemObjectSink {
public:
    explicit WmiSink(const std::vector<std::wstring>& names) : names_(names) {}
    virtual ~WmiSink() = default;

    // Called by WMI (on the thread whose STA owns the subscription) for every
    // matching process event. Kept deliberately tiny.
    HRESULT STDMETHODCALLTYPE Indicate(LONG count, IWbemClassObject** objects) override {
        for (LONG i = 0; i < count; ++i) {
            handle_object(objects[i]);
        }
        return WBEM_S_NO_ERROR;
    }

    HRESULT STDMETHODCALLTYPE SetStatus(LONG, HRESULT, BSTR, IWbemClassObject*) override {
        return WBEM_S_NO_ERROR;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = InterlockedDecrement(&ref_);
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IWbemObjectSink) {
            *out = static_cast<IWbemObjectSink*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    // `created` is supplied by the subscription that owns this sink.
    void set_kind(ProcessEvent::Kind kind) { kind_ = kind; }

    std::vector<ProcessEvent> drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ProcessEvent> out;
        out.swap(queue_);
        return out;
    }

    int dropped() const { return dropped_; }

private:
    void handle_object(IWbemClassObject* event_object) {
        if (event_object == nullptr) return;
        VARIANT target;
        VariantInit(&target);
        ProcessEvent event;
        event.kind = kind_;
        bool ok = false;
        if (SUCCEEDED(event_object->Get(L"TargetInstance", 0, &target, nullptr, nullptr)) &&
            target.vt == VT_UNKNOWN && target.punkVal != nullptr) {
            IWbemClassObject* instance = nullptr;
            if (SUCCEEDED(target.punkVal->QueryInterface(IID_IWbemClassObject,
                                                         reinterpret_cast<void**>(&instance))) &&
                instance != nullptr) {
                VARIANT name;
                VariantInit(&name);
                if (SUCCEEDED(instance->Get(L"Name", 0, &name, nullptr, nullptr)) && name.vt == VT_BSTR &&
                    name.bstrVal != nullptr) {
                    event.name = lower_wide(name.bstrVal);
                    ok = true;
                }
                VariantClear(&name);

                VARIANT pid;
                VariantInit(&pid);
                if (SUCCEEDED(instance->Get(L"ProcessId", 0, &pid, nullptr, nullptr))) {
                    if (pid.vt == VT_I4) event.pid = static_cast<unsigned long>(pid.lVal);
                    else if (pid.vt == VT_UI4) event.pid = static_cast<unsigned long>(pid.ulVal);
                }
                VariantClear(&pid);
                instance->Release();
            }
        }
        VariantClear(&target);
        if (!ok || !name_matches(event.name, names_)) return;

        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= kMaxQueue) {
            ++dropped_;
            return;
        }
        queue_.push_back(event);
    }

    static constexpr size_t kMaxQueue = 32;

    LONG ref_ = 1;
    ProcessEvent::Kind kind_ = ProcessEvent::Kind::Created;
    std::vector<std::wstring> names_;
    std::mutex mutex_;
    std::vector<ProcessEvent> queue_;
    int dropped_ = 0;
};

struct WmiSubscription {
    IWbemServices* services = nullptr;
    IWbemObjectSink* creation_sink = nullptr;
    IWbemObjectSink* deletion_sink = nullptr;
    WmiSink* creation_internal = nullptr;
    WmiSink* deletion_internal = nullptr;
    bool com_initialized = false;

    ~WmiSubscription() { release(); }

    void release() {
        if (services) {
            if (creation_sink) {
                services->CancelAsyncCall(creation_sink);
                creation_sink->Release();
                creation_sink = nullptr;
            }
            if (deletion_sink) {
                services->CancelAsyncCall(deletion_sink);
                deletion_sink->Release();
                deletion_sink = nullptr;
            }
            services->Release();
            services = nullptr;
        }
        if (creation_internal) {
            creation_internal->Release();
            creation_internal = nullptr;
        }
        if (deletion_internal) {
            deletion_internal->Release();
            deletion_internal = nullptr;
        }
    }
};

const wchar_t* kWqlCreation =
    L"SELECT * FROM __InstanceCreationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'";
const wchar_t* kWqlDeletion =
    L"SELECT * FROM __InstanceDeletionEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'";

bool subscribe(IWbemServices* services, const wchar_t* query, WmiSink* sink, IWbemObjectSink** out) {
    BSTR language = SysAllocString(L"WQL");
    BSTR text = SysAllocString(query);
    const HRESULT hr = services->ExecNotificationQueryAsync(
        language, text, WBEM_FLAG_SEND_STATUS, nullptr, static_cast<IWbemObjectSink*>(sink));
    SysFreeString(language);
    SysFreeString(text);
    if (FAILED(hr)) return false;
    sink->AddRef();
    sink->AddRef();  // one for us, one for WMI
    *out = sink;
    return true;
}

}  // namespace

bool ProcessScanner::start_events(const std::vector<std::wstring>& lower_case_names, std::string* error) {
    shutdown();

    auto* subscription = new WmiSubscription();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        if (error) *error = "COM initialization failed (" + to_utf8(hresult_text(com)) + ")";
        delete subscription;
        return false;
    }
    subscription->com_initialized = SUCCEEDED(com);

    IWbemLocator* locator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                  reinterpret_cast<void**>(&locator));
    if (FAILED(hr) || locator == nullptr) {
        if (error) *error = "WMI unavailable (" + to_utf8(hresult_text(hr)) + ")";
        delete subscription;
        return false;
    }

    BSTR namespace_path = SysAllocString(L"ROOT\\CIMV2");
    hr = locator->ConnectServer(namespace_path, nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                &subscription->services);
    SysFreeString(namespace_path);
    locator->Release();
    if (FAILED(hr) || subscription->services == nullptr) {
        if (error) *error = "WMI connect failed (" + to_utf8(hresult_text(hr)) + ")";
        delete subscription;
        return false;
    }

    hr = CoSetProxyBlanket(subscription->services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        if (error) *error = "WMI proxy setup failed (" + to_utf8(hresult_text(hr)) + ")";
        delete subscription;
        return false;
    }

    subscription->creation_internal = new WmiSink(lower_case_names);
    subscription->creation_internal->set_kind(ProcessEvent::Kind::Created);
    subscription->deletion_internal = new WmiSink(lower_case_names);
    subscription->deletion_internal->set_kind(ProcessEvent::Kind::Deleted);

    if (!subscribe(subscription->services, kWqlCreation, subscription->creation_internal,
                   &subscription->creation_sink) ||
        !subscribe(subscription->services, kWqlDeletion, subscription->deletion_internal,
                   &subscription->deletion_sink)) {
        if (error) *error = "WMI event subscription failed";
        delete subscription;
        return false;
    }

    wmi_ = subscription;
    events_available_ = true;
    log_info("process observer: WMI events active (instant Premiere start/stop notification)");
    return true;
}

void ProcessScanner::shutdown() {
    if (wmi_ != nullptr) {
        delete static_cast<WmiSubscription*>(wmi_);
        wmi_ = nullptr;
    }
    events_available_ = false;
}

bool ProcessScanner::poll_event(ProcessEvent& out) {
    if (wmi_ == nullptr) return false;
    auto* subscription = static_cast<WmiSubscription*>(wmi_);
    for (WmiSink* sink : {subscription->creation_internal, subscription->deletion_internal}) {
        if (sink == nullptr) continue;
        std::vector<ProcessEvent> events = sink->drain();
        if (events.empty()) continue;
        out = events.front();
        // Anything beyond the first event of a batch just means "look again",
        // which the caller's normal rescan already covers.
        return true;
    }
    return false;
}

std::vector<unsigned long> ProcessScanner::find_processes(const std::vector<std::wstring>& lower_case_names) {
    std::vector<unsigned long> result;
    ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) return result;
    do {
        if (name_matches(lower_wide(entry.szExeFile), lower_case_names)) {
            result.push_back(entry.th32ProcessID);
        }
    } while (Process32NextW(snapshot.get(), &entry));
    return result;
}

bool ProcessScanner::process_alive(unsigned long pid) {
    ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) return false;
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.get(), &exit_code)) return false;
    return exit_code == STILL_ACTIVE;
}

}  // namespace win
}  // namespace azy
