#pragma once

#include "EventLoop.h"

#include <sdbus-c++/sdbus-c++.h>

namespace vw {

// Drives an sdbus-c++ connection from our poll loop instead of its own thread.
class DBusSource : public EventLoop::Source {
public:
    explicit DBusSource(sdbus::IConnection& connection) : connection_(connection) {}

    int fd() override { return connection_.getEventLoopPollData().fd; }
    short events() override { return connection_.getEventLoopPollData().events; }
    int timeoutMs() override { return connection_.getEventLoopPollData().getPollTimeout(); }
    void dispatch(short) override
    {
        // sd-bus may have buffered more than one message; drain them all.
        while (connection_.processPendingRequest()) {
        }
    }

private:
    sdbus::IConnection& connection_;
};

} // namespace vw
