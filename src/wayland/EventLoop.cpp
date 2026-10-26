#include "EventLoop.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>

namespace muralop {

EventLoop::EventLoop()
{
    wakeFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

EventLoop::~EventLoop()
{
    if (wakeFd_ >= 0)
        close(wakeFd_);
}

void EventLoop::add(Source* source)
{
    sources_.push_back(source);
}

void EventLoop::remove(Source* source)
{
    sources_.erase(std::remove(sources_.begin(), sources_.end(), source), sources_.end());
}

void EventLoop::post(std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lock(postMutex_);
        posted_.push_back(std::move(fn));
    }
    wake();
}

void EventLoop::wake()
{
    const uint64_t one = 1;
    [[maybe_unused]] ssize_t n = write(wakeFd_, &one, sizeof one);
}

void EventLoop::run()
{
    running_ = true;
    std::vector<pollfd> fds;
    std::vector<Source*> round;   // sources as they were when polled; add/remove may happen mid-dispatch

    while (running_) {
        // Drain posted callbacks first so they see a consistent world before we block.
        std::vector<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(postMutex_);
            pending.swap(posted_);
        }
        for (auto& fn : pending)
            fn();
        if (!running_)
            break;

        round = sources_;
        fds.clear();
        fds.push_back({wakeFd_, POLLIN, 0});
        int timeout = -1;
        for (Source* s : round) {
            s->prepare();
            fds.push_back({s->fd(), s->events(), 0});
            const int t = s->timeoutMs();
            if (t >= 0 && (timeout < 0 || t < timeout))
                timeout = t;
        }

        const int ret = poll(fds.data(), fds.size(), timeout);
        if (fds[0].revents & POLLIN) {
            uint64_t v;
            [[maybe_unused]] ssize_t n = read(wakeFd_, &v, sizeof v);
        }
        for (size_t i = 0; i < round.size(); ++i) {
            // Skip sources removed by an earlier dispatch in this round.
            if (std::find(sources_.begin(), sources_.end(), round[i]) == sources_.end())
                continue;
            round[i]->dispatch(ret > 0 ? fds[i + 1].revents : 0);
        }
    }
}

} // namespace muralop
