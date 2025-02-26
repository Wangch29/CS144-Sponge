#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _next(0), _unassembled_bytes(), _unassembled_size(0), _capacity(capacity), _eof(false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    /* if (index == static_cast<size_t>(-1)) { */
    /*     return; */
    /* } */
    // Set new eof.
    _eof = _eof || eof;
    
    // The index data begin to push.
    int data_begin_idx = index >= _next ? 0 : std::min(data.size(), _next - index);
    // The valid data length, ingore stuff before _next.
    size_t valid_length = data.size() - data_begin_idx;
    // Index begins in _unassembly_bytes.
    int unassem_idx = (index > _next && valid_length != 0) ? index - _next : 0;
    if (unassem_idx >= static_cast<int>(_capacity) || unassem_idx < 0) {
        return;
    }
    
    // Insert to _unassembled_bytes.
    if (unassem_idx + valid_length > _unassembled_bytes.size()) {
        _unassembled_bytes.resize(std::min(unassem_idx + valid_length, _capacity), std::nullopt);
    }
    for (size_t i = data_begin_idx; i < data_begin_idx + valid_length; ++i) {
        if (unassem_idx >= static_cast<int>(_capacity - _output.buffer_size())) {
            break;
        }
        if (_unassembled_bytes[unassem_idx] == std::nullopt) {
            _unassembled_size += 1;
        }
        _unassembled_bytes[unassem_idx++] = data[i];
    }

    assemble_bytes();
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_size; }

bool StreamReassembler::empty() const { return _unassembled_size == 0; }

void StreamReassembler::assemble_bytes() {
    std::string re;
    while (!_unassembled_bytes.empty()) {
        std::optional<char> c = _unassembled_bytes.front();
        if (!c.has_value()) {
            break;
        }
        re.push_back(c.value());
        _unassembled_bytes.pop_front();
        c = _unassembled_bytes.front();
    }
    _next += re.size();
    _unassembled_size -= re.size();
    _output.write(std::move(re));

    if (_eof && _unassembled_bytes.empty()) {
        _output.end_input();
    }
}
