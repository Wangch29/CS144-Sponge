#include "wrapping_integers.hh"
#include <cstdint>

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

#define WRAP_32_BIT (1ULL << 32)

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) { return WrappingInt32{static_cast<uint32_t>(n) + isn.raw_value()}; }

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint32_t low32bit = static_cast<uint32_t>(n - isn);
    uint64_t high32bit1 = (checkpoint + (1 << 31)) & 0xFFFFFFFF00000000;
    uint64_t high32bit2 = (checkpoint - (1 << 31)) & 0xFFFFFFFF00000000;
    uint64_t res1 = low32bit | high32bit1;
    uint64_t res2 = low32bit | high32bit2;
    auto distance = [](uint64_t a, uint64_t b) {
        return a > b ? a - b : b - a;
    };
    return distance(res1, checkpoint) < distance(res2, checkpoint) ? res1 : res2;
}
