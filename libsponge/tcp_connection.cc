#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _cfg.send_capacity - _sender.bytes_in_flight(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_received = 0;

    auto &header = seg.header();
    if (header.rst) {
        // set both to error state, and end connection.
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        return;
    }

    // Receiver.
    _receiver.segment_received(seg);
    // Sender.
    if ((_sent_syn && header.ack) || (header.syn)) {
        _sent_syn = true;
        _sender.ack_received(header.ackno, header.win);
        _sender.fill_window();
    }

    // Receiver reply, piggybacking.
    piggybacking_ack(header.ack && !header.syn && !header.fin && seg.payload().size() == 0);

    // Send segments.
    send_segments();

    auto &stream_inbound = _receiver.stream_out();
    auto &stream_outbound = _sender.stream_in();

    if (_linger_after_streams_finish == false && header.ackno == _sender.next_seqno()) {
        _active = false;
    }

    // Try to set _linger_after_streams_finish = false.
    if (stream_inbound.input_ended() && !stream_outbound.input_ended() && !_lingering) {
        _linger_after_streams_finish = false;
    }

    // Enter lingering state(TIME WAIT).
    // Only when fin has been acked.
    if (stream_inbound.input_ended() && stream_outbound.input_ended() && header.ackno == _sender.next_seqno() &&
        !_lingering) {
        if (_linger_after_streams_finish) {
            _lingering_time = _cfg.rt_timeout * 10;
            _lingering = true;
        }
    }
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) { return _sender.stream_in().write(data); }

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_received += ms_since_last_tick;

    if (!_sent_syn) {
        return;
    }

    // Transformsmission too many times, abort.
    if (!_lingering && _sender.consecutive_retransmissions() >= _cfg.MAX_RETX_ATTEMPTS) {
        // Generate rst segment.
         _sender.send_empty_segment();
        auto &seg = _sender.segments_out().back();
        seg.header().rst = true;

        // Send segments.
        _segments_out.push(seg);

        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        return;
    }

    // Retransmission.
    _sender.tick(ms_since_last_tick);
    _sender.fill_window();
    send_segments();

    // Lingering, and close connection cleanly.
    if (_lingering) {
        if (_lingering_time <= ms_since_last_tick) {
            _lingering_time = 0;
            _active = false;
        } else {
            _lingering_time -= ms_since_last_tick;
        }
    }
}

void TCPConnection::end_input_stream() {
    if (!_sent_syn) {
        return;
    }
    _sender.stream_in().end_input();
    // Send FIN segment.
    _sender.fill_window();
    piggybacking_ack(false);
    send_segments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send_segments();
    _sent_syn = true;
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            // Generate rst segment.
            _sender.send_empty_segment();
            auto &seg = _sender.segments_out().back();
            seg.header().rst = true;

            // Send segments.
            send_segments();

            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _active = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::piggybacking_ack(bool is_single_ack) {
    auto &sender_out = _sender.segments_out();
    auto re_ackno = _receiver.ackno();
    if (re_ackno.has_value()) {
        if (!sender_out.empty()) {
            // piggybacking_ack
            sender_out.front().header().ack = true;
            sender_out.front().header().ackno = _receiver.ackno().value();
            sender_out.front().header().win =
                std::min(_receiver.window_size(), static_cast<size_t>(std::numeric_limits<uint16_t>::max()));
        } else if (!is_single_ack) {
            // Create a new segment with ack.
            TCPSegment ack_seg;
            ack_seg.header().ack = true;
            ack_seg.header().ackno = re_ackno.value();
            ack_seg.header().win =
                std::min(_receiver.window_size(), static_cast<size_t>(std::numeric_limits<uint16_t>::max()));
            _segments_out.push(ack_seg);
        }
    }
}

void TCPConnection::send_segments() {
    auto re_ack = _receiver.ackno();

    auto &sender_out = _sender.segments_out();
    while (!sender_out.empty()) {
        if (re_ack.has_value()) {
            sender_out.front().header().ack = true;
            sender_out.front().header().ackno = _receiver.ackno().value();
            sender_out.front().header().win =
                std::min(_receiver.window_size(), static_cast<size_t>(std::numeric_limits<uint16_t>::max()));
        }
        _segments_out.push(sender_out.front());
        sender_out.pop();
    }
}
