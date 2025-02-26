#include "tcp_receiver.hh"

#include "wrapping_integers.hh"

#include <cstdint>
#include <optional>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

// Receiver has three status:
// 1. Not received syn.
// 2. received syn, and not received fin.
// 3. received fin.
//
// So focus on these three status.
// _received_syn can different between 1 and 2.
// _reassembler.stream_out().input_ended() can different between 2 and 3.

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // not received syned.
    if (!_received_syn) {
        if (!seg.header().syn) {
            // Not syn, return;
            return;
        }
        _received_syn = true;
        _isn = seg.header().seqno;
    }
    // Receiver's abs ackno.
    uint64_t abs_ackno = _reassembler.stream_out().bytes_written() + 1;
    // Seg's abs seqno.
    uint64_t curr_abs_seqno = unwrap(seg.header().seqno, _isn, abs_ackno);
    // Begin Idx in stream.
    uint64_t stream_index = curr_abs_seqno - 1 + (seg.header().syn);
    _reassembler.push_substring(seg.payload().copy(), stream_index, seg.header().fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (!_received_syn) {
        return std::nullopt;
    }
    uint64_t abs_ackno = _reassembler.stream_out().bytes_written() + 1;
    if (_reassembler.stream_out().input_ended()) {
        abs_ackno += 1;
    }
    return WrappingInt32(_isn) + abs_ackno;
}

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
