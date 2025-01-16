#include "tcp_receiver.hh"
#include "wrapping_integers.hh"
#include <cstdint>
#include <optional>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // Process the received segment.
    const auto &header = seg.header();

    if (!_ackno.has_value() && header.syn) {
        _isn = header.seqno;
        _ackno = std::make_optional(header.seqno + 1);
    }
    if (!_ackno.has_value()) {
        return;
    }
    if (header.fin) {
        _fin = true;
    }

    auto before_reassem = _reassembler.stream_out().buffer_size();

    const Buffer &payload = seg.payload();
    WrappingInt32 seqno = header.syn ? header.seqno + 1 : header.seqno;
    _checkpoint = unwrap(seqno, _isn, _checkpoint);
    _reassembler.push_substring(payload.copy(),  _checkpoint - 1, _fin);  // Absolute seqno to stream index

    auto after_reassem = _reassembler.stream_out().buffer_size();

    _ackno = std::make_optional(_ackno.value() + (after_reassem - before_reassem));
    if (_fin && _reassembler.empty()) {
        _ackno = std::make_optional(_ackno.value() + 1);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const { return _ackno; }

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
