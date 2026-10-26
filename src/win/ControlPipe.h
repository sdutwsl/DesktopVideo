#pragma once

#include "core/AppControl.h"
#include <windows.h>
#include <memory>
#include <mutex>
#include <string>

namespace muralop {

class ControlPipe {
public:
    static constexpr UINT Message = WM_APP + 2;
    ControlPipe(HWND window, AppControl& control);
    ~ControlPipe();
    void dispatch();

private:
    struct Request {
        std::string input, output;
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ~Request() { CloseHandle(ready); }
    };
    static DWORD WINAPI runThread(void* self);
    void run();
    bool finish(HANDLE pipe, OVERLAPPED& operation, BOOL immediate, DWORD& count);

    HWND window_;
    AppControl& control_;
    HANDLE stop_ = nullptr;
    HANDLE thread_ = nullptr;
    std::wstring name_;
    PSECURITY_DESCRIPTOR security_ = nullptr;
    std::mutex mutex_;
    std::shared_ptr<Request> pending_;
};
} // namespace muralop
