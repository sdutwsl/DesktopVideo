#pragma once

#include <functional>
#include <mutex>
#include <vector>

namespace vw {

// Minimal poll(2) loop. Sources contribute an fd and get told whether it became
// readable after each round, so a source can also use the round for bookkeeping
// (the Wayland source needs to cancel its read when nothing arrived).
class EventLoop {
public:
    class Source {
    public:
        virtual ~Source() = default;
        virtual int fd() = 0;                       // -1 to sit this round out
        virtual short events() { return 1 /*POLLIN*/; }
        virtual int timeoutMs() { return -1; }      // wake up after this even without I/O
        virtual void prepare() {}                   // right before poll
        virtual void dispatch(short revents) = 0;   // after poll, revents == 0 when not ready
    };

    EventLoop();
    ~EventLoop();

    void add(Source* source);
    void remove(Source* source);

    // Run a callback on the loop thread; safe to call from any thread.
    void post(std::function<void()> fn);

    void run();
    void quit() { running_ = false; wake(); }

private:
    void wake();

    std::vector<Source*> sources_;
    std::mutex postMutex_;
    std::vector<std::function<void()>> posted_;
    int wakeFd_ = -1;
    bool running_ = false;
};

} // namespace vw
