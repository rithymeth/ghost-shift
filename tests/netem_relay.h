#pragma once
// A UDP relay that sits between one client and the server and impairs the link in both directions:
// fixed latency, random jitter (so packets can reorder) and random loss. Driven by simulated time.
// Each client needs its own relay: the server tells peers apart by source address.
#include "aether/net/socket.h"

#include <algorithm>
#include <random>
#include <vector>

namespace gs_test {
using namespace aether;

class NetemRelay {
public:
    bool Open(const net::Address& server, const net::LinkConditions& conditions, u32 seed) {
        server_ = server, cond_ = conditions, rng_.seed(seed);
        return front_.Open(0) && back_.Open(0);
    }
    net::Address FrontAddress() const { return net::Address::Loopback(front_.LocalAddress().port); }
    u64 Forwarded() const { return forwarded_; }
    u64 Dropped() const { return dropped_; }

    void Pump(f64 now) {
        net::Address from;
        std::vector<u8> data;
        while (front_.Receive(from, data)) { client_ = from, have_client_ = true; Queue(now, true, data); }
        while (back_.Receive(from, data)) Queue(now, false, data);
        std::stable_sort(queue_.begin(), queue_.end(), [](const Packet& a, const Packet& b) { return a.due < b.due; });
        usize i = 0;
        for (; i < queue_.size() && queue_[i].due <= now; ++i) {
            if (queue_[i].to_server) back_.Send(server_, queue_[i].data);
            else if (have_client_) front_.Send(client_, queue_[i].data);
        }
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(i));
    }

private:
    struct Packet {
        f64 due;
        bool to_server;
        std::vector<u8> data;
    };
    void Queue(f64 now, bool to_server, const std::vector<u8>& data) {
        std::uniform_real_distribution<f64> u(0.0, 1.0);
        if (u(rng_) < cond_.loss) { ++dropped_; return; }
        ++forwarded_;
        queue_.push_back({now + cond_.latency + u(rng_) * cond_.jitter, to_server, data});
    }
    net::UdpSocket front_, back_;
    net::Address server_, client_;
    bool have_client_ = false;
    net::LinkConditions cond_;
    std::mt19937 rng_;
    std::vector<Packet> queue_;
    u64 forwarded_ = 0, dropped_ = 0;
};

} // namespace gs_test
