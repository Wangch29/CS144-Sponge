#include "tcp_sender.hh"

#include "tcp_helpers/tcp_config.hh"
#include "tcp_helpers/tcp_segment.hh"
#include "util/buffer.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {
    _timer.set_retrans_timeout(_initial_retransmission_timeout);
}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    uint16_t left_window_size = std::max(std::uint16_t(1), _window_size);
    left_window_size = left_window_size > _bytes_in_flight ? left_window_size - _bytes_in_flight : 0;

    while (left_window_size > 0) {
        uint16_t remain_seg_length = left_window_size;
        TCPSegment tcp;
        // Set TCP header.
        tcp.header().seqno = next_seqno();
        if (_next_seqno == 0 && remain_seg_length > 0) {
            tcp.header().syn = 1;
            remain_seg_length -= 1;
        }
        // Fill TCP Payload.
        if (remain_seg_length > 0) {
            size_t payload_limit = std::min(static_cast<std::uint16_t>(TCPConfig::MAX_PAYLOAD_SIZE), remain_seg_length);
            tcp.payload() = Buffer(_stream.peek_output(payload_limit));
            _stream.pop_output(payload_limit);
            remain_seg_length -= tcp.payload().size();
        }
        // set fin.
        if (remain_seg_length > 0 && _stream.eof() && !_sent_fin) {
            tcp.header().fin = 1;
            _sent_fin = true;
            remain_seg_length -= 1;
        }
        // Update seg_length.
        size_t seg_length = tcp.length_in_sequence_space();
        left_window_size =
            std::max(static_cast<std::uint16_t>(0), static_cast<std::uint16_t>(left_window_size - seg_length));

        if (seg_length == 0) {
            // No data to send, break.
            break;
        }

        // Reset timer.
        if (_outstanding_segments.empty() && seg_length > 0) {
            _timer.reset(_initial_retransmission_timeout);
            _timer.open();
        }
        // Insert tcp.
        _segments_out.push(tcp);
        _outstanding_segments.emplace(_next_seqno, tcp);
        _bytes_in_flight += seg_length;
        // Update _next_seqno
        _next_seqno += seg_length;

        // If set fin, break;
        if (tcp.header().fin) {
            break;
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);

    if (abs_ackno > _next_seqno) {
        // Impossible ack.
        return;
    }

    _window_size = window_size;
    bool ack = false;

    // Try to remove acked outstanding segments.
    while (!_outstanding_segments.empty()) {
        auto &p = _outstanding_segments.front();
        if (p.first + p.second.length_in_sequence_space() - 1 < abs_ackno) {
            _bytes_in_flight -= _outstanding_segments.front().second.length_in_sequence_space();
            _outstanding_segments.pop();
            ack = true;
        } else {
            break;
        }
    }

    if (ack) {
        _timer.reset(_initial_retransmission_timeout);
    }
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (_timer.check_timeout(ms_since_last_tick, _window_size != 0) && !_outstanding_segments.empty()) {
        _segments_out.push(_outstanding_segments.front().second);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _timer.get_retransmission_count(); }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = next_seqno();
    _segments_out.emplace(std::move(seg));
}
