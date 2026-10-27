#include "ControlPipe.h"
#include <sddl.h>
#include <vector>
#include <stdexcept>

namespace muralop {
std::string ControlPipe::request(const std::string& payload)
{
    wchar_t username[256];
    DWORD length = 256, session = 0;
    if (!GetUserNameW(username, &length) || !ProcessIdToSessionId(GetCurrentProcessId(), &session))
        throw std::runtime_error("Cannot identify the current Windows session");
    const std::wstring name = L"\\\\.\\pipe\\Muralop." + std::to_wstring(session) + L"." + username;
    char output[65536];
    DWORD count = 0;
    if (!CallNamedPipeW(name.c_str(), const_cast<char*>(payload.data()), DWORD(payload.size()),
                        output, sizeof output, &count, 2000))
        throw std::runtime_error("Wallpaper backend is unavailable or did not respond");
    return std::string(output, count);
}

ControlPipe::ControlPipe(HWND window, AppControl& control) : window_(window), control_(control)
{
    wchar_t username[256];
    DWORD length = 256, session = 0;
    if (!GetUserNameW(username, &length) || !ProcessIdToSessionId(GetCurrentProcessId(), &session))
        throw std::runtime_error("Cannot identify the current Windows session");
    name_ = L"\\\\.\\pipe\\Muralop." + std::to_wstring(session) + L"." + username;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error("Cannot read process identity");
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> info(bytes);
    const BOOL identified = GetTokenInformation(token, TokenUser, info.data(), bytes, &bytes);
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!identified || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid, &sid))
        throw std::runtime_error("Cannot identify the current user");
    const std::wstring acl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &security_, nullptr))
        throw std::runtime_error("Cannot secure the GUI control pipe");
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, runThread, this, 0, nullptr);
    if (!thread_) {
        CloseHandle(stop_);
        LocalFree(security_);
        throw std::runtime_error("Cannot start GUI control pipe");
    }
}

ControlPipe::~ControlPipe()
{
    SetEvent(stop_);
    WaitForSingleObject(thread_, INFINITE);
    CloseHandle(thread_);
    CloseHandle(stop_);
    LocalFree(security_);
}

DWORD WINAPI ControlPipe::runThread(void* self)
{
    static_cast<ControlPipe*>(self)->run();
    return 0;
}

bool ControlPipe::finish(HANDLE pipe, OVERLAPPED& op, BOOL immediate, DWORD& count)
{
    if (immediate) return GetOverlappedResult(pipe, &op, &count, FALSE);
    if (GetLastError() != ERROR_IO_PENDING) return false;
    HANDLE waits[] = {stop_, op.hEvent};
    if (WaitForMultipleObjects(2, waits, FALSE, 10000) != WAIT_OBJECT_0 + 1) {
        CancelIoEx(pipe, &op);
        GetOverlappedResult(pipe, &op, &count, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, &op, &count, FALSE);
}

void ControlPipe::run()
{
    SECURITY_ATTRIBUTES attrs{sizeof(attrs), security_, FALSE};
    while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 65536, 65536, 0, &attrs);
        if (pipe == INVALID_HANDLE_VALUE) return;
        OVERLAPPED op{};
        op.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD count = 0;
        BOOL connected = ConnectNamedPipe(pipe, &op);
        const DWORD error = GetLastError();
        bool success = connected || error == ERROR_PIPE_CONNECTED;
        if (!success && error == ERROR_IO_PENDING) {
            // Idle connections have no deadline; shutdown still interrupts the wait.
            HANDLE waits[] = {stop_, op.hEvent};
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
                success = GetOverlappedResult(pipe, &op, &count, FALSE);
            else { CancelIoEx(pipe, &op); GetOverlappedResult(pipe, &op, &count, TRUE); }
        }
        if (success) {
            auto request = std::make_shared<Request>();
            char buffer[32768];
            ResetEvent(op.hEvent);
            success = finish(pipe, op, ReadFile(pipe, buffer, sizeof buffer, nullptr, &op), count);
            if (success) {
                request->input.assign(buffer, count);
                { std::lock_guard<std::mutex> lock(mutex_); pending_ = request; }
                PostMessageW(window_, Message, 0, 0);
                HANDLE waits[] = {stop_, request->ready};
                if (WaitForMultipleObjects(2, waits, FALSE, 10000) == WAIT_OBJECT_0 + 1) {
                    ResetEvent(op.hEvent);
                    if (finish(pipe, op, WriteFile(pipe, request->output.data(), DWORD(request->output.size()), nullptr, &op), count)) {
                        // CallNamedPipe closes after reading the reply. Wait for that
                        // EOF before disconnecting, which otherwise discards unread data.
                        char ack;
                        ResetEvent(op.hEvent);
                        finish(pipe, op, ReadFile(pipe, &ack, 1, nullptr, &op), count);
                    }
                }
                { std::lock_guard<std::mutex> lock(mutex_); if (pending_ == request) pending_.reset(); }
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(op.hEvent);
        CloseHandle(pipe);
    }
}

void ControlPipe::dispatch()
{
    std::shared_ptr<Request> request;
    { std::lock_guard<std::mutex> lock(mutex_); request = std::move(pending_); }
    if (!request) return;
    request->output = control_.execute(request->input);
    SetEvent(request->ready);
}
} // namespace muralop
