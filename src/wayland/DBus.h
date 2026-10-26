#pragma once

#include "EventLoop.h"

#include <sdbus-c++/sdbus-c++.h>

namespace muralop {

// Drives an sdbus-c++ connection from our poll loop instead of its own thread.
// sdbus-c++ 2 wants two fds watched: the bus socket and an eventfd it uses to
// wake the loop, which a small companion source contributes.
class DBusSource : public EventLoop::Source {
public:
    DBusSource(EventLoop& loop, sdbus::IConnection& connection) : loop_(loop), connection_(connection), wake_(*this)
    {
        loop_.add(this);
        loop_.add(&wake_);   // after us, so prepare() has refreshed poll_ by the time it is asked
    }
    ~DBusSource() override
    {
        loop_.remove(&wake_);
        loop_.remove(this);
    }

    void prepare() override { poll_ = connection_.getEventLoopPollData(); }
    int fd() override { return poll_.fd; }
    short events() override { return poll_.events; }
    int timeoutMs() override { return poll_.getPollTimeout(); }
    void dispatch(short) override
    {
        // sd-bus may have buffered more than one message; drain them all. Running dry
        // also resets the eventfd.
        while (connection_.processPendingEvent()) {
        }
    }

private:
    struct WakeSource : EventLoop::Source {
        explicit WakeSource(DBusSource& owner) : owner(owner) {}
        int fd() override { return owner.poll_.eventFd; }
        void dispatch(short) override {}   // the bus source drains everything
        DBusSource& owner;
    };

    EventLoop& loop_;
    sdbus::IConnection& connection_;
    sdbus::IConnection::PollData poll_{};
    WakeSource wake_;
};

} // namespace muralop
