#include "overlay_publication.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

int main() {
    enhance_runtime::OverlayPublication channel;
    std::uint32_t generation = 99;
    assert(channel.can_write() && !channel.acquire(generation));
    channel.publish();
    assert(channel.acquire(generation) && !channel.can_write());
    const auto leased = generation;
    const auto leased_slot = channel.slot.load();
    // The worker is still copying this frame while the game closes/reopens UI.
    // Previously hide() reset published and allowed two publications to wrap
    // around onto the still-leased buffer.
    for (int i = 0; i < 1000; ++i) {
        channel.hide();
        assert(!channel.can_write());
        assert(channel.slot.load() == leased_slot);
    }
    channel.acknowledge(leased);
    assert(channel.can_write());
    channel.publish();
    assert(channel.slot.load() != leased_slot);
    assert(channel.acquire(generation));
    channel.acknowledge(generation);
    // An image hidden before the worker sees it is retired by that worker,
    // including when no GPU texture could be created for it.
    channel.publish();channel.hide();
    assert(!channel.can_write() && !channel.acquire(generation));
    assert(channel.can_write());
    for (int i = 0; i < 10000; ++i) {
        channel.publish();
        assert(channel.acquire(generation));
        channel.acknowledge(generation);
        assert(channel.can_write());
    }
    channel.next = std::numeric_limits<std::uint32_t>::max();
    channel.publish();assert(channel.acquire(generation) && generation == 1);
    channel.acknowledge(generation);channel.reset();
    assert(channel.can_write() && !channel.acquire(generation));
    std::puts("Overlay publication: close/reopen leases, hidden retirement, 10000 updates and wrap passed");
}
