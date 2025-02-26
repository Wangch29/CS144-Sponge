#include "tcp_connection.hh"

#include <cassert>
#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_received = 0;

    auto &header = seg.header();

    // Receiver.
    _receiver.segment_received(seg);

    if (header.rst) {
        // set both to error state, and end connection.
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        _linger_after_streams_finish = false;
        return;
    }

    // Sender.
    // Has sent syn and is ack seg: normal ack.
    if (_sent_syn && header.ack) {
        _sender.ack_received(header.ackno, header.win);
    }

    // Try to connect, send syn ack.
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        connect();
        return;
    }

    // Try to set _linger_after_streams_finish = false.
    // CLOSE_WAIT.
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED) {
        _linger_after_streams_finish = false;
    }

    // If _reciever has closed, directly close cleanly without lingering.
    // CLOSED.
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && _linger_after_streams_finish == false) {
        _active = false;
        return;
    }

    // Receiver reply.
    // If seg is a single ack segment, don't need to re-ack.
    if (seg.length_in_sequence_space() != 0) {
        _sender.send_empty_segment();
    }
    send_segments();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t write_size = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segments();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_received += ms_since_last_tick;

    if (!_sent_syn) {
        return;
    }

    // Transformsmission too many times, abort.
    if (_sender.consecutive_retransmissions() >= _cfg.MAX_RETX_ATTEMPTS) {
        send_rst_segment();
        return;
    }

    // Retransmission.
    _sender.tick(ms_since_last_tick);
    send_segments();

    // Lingering, and close connection cleanly.
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && _linger_after_streams_finish &&
        _time_since_last_received >= 10 * _cfg.rt_timeout) {
        _active = false;
        _linger_after_streams_finish = false;
    }
}

void TCPConnection::end_input_stream() {
    if (!_sent_syn) {
        return;
    }
    _sender.stream_in().end_input();
    // Send FIN segment.
    _sender.fill_window();
    send_segments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    _sent_syn = true;
    _active = true;
    send_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            send_rst_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_segments() {
    auto re_ack = _receiver.ackno();
    auto &sender_out = _sender.segments_out();
    while (!sender_out.empty()) {
        if (re_ack.has_value()) {
            sender_out.front().header().ack = true;
            sender_out.front().header().ackno = _receiver.ackno().value();
            sender_out.front().header().win = _receiver.window_size();
        }
        _segments_out.push(std::move(sender_out.front()));
        sender_out.pop();
    }
}

void TCPConnection::send_rst_segment() {
    // Clear out _segments_out.
    while (!_segments_out.empty()) {
        _segments_out.pop();
    }

    // Generate rst segment.
    TCPSegment seg;
    seg.header().rst = true;

    // Send segments.
    _segments_out.push(std::move(seg));

    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _active = false;
    _linger_after_streams_finish = false;
}
