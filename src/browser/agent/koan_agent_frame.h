// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_FRAME_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_FRAME_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <vector>

#include "base/containers/span.h"

// The wire format of agent control (.agent/notes/agent-control.md section
// 4.1): a frame is a 4-byte big-endian payload length followed by the payload
// (UTF-8 JSON, CDP-shaped). The browser process speaks only this on the unix
// socket; it parses no HTTP or WebSocket from a local process. The decoder is
// built for hostile input: the length is checked as soon as the header is
// known, before any payload is buffered, and what it holds is bounded.
namespace koan::agent {

inline constexpr size_t kFrameHeaderSize = 4;

// Before a connection has authenticated, only Koan.pair / Koan.authenticate
// are parsed and a frame may carry at most this many payload bytes.
inline constexpr size_t kMaxPreAuthPayload = 4 * 1024;

// After authentication. Larger results travel as IO stream reads in chunks.
inline constexpr size_t kMaxPayload = 32 * 1024 * 1024;

// The frame for `payload`: the header and the payload. nullopt if the payload
// is empty or longer than `max_payload`.
std::optional<std::vector<uint8_t>> EncodeFrame(
    base::span<const uint8_t> payload,
    size_t max_payload = kMaxPayload);

// Splits a byte stream into frames. Feed it what the socket delivers with
// Append() and take frames out with TakeFrame(). Once it fails, it stays failed
// and the caller closes the connection.
class FrameDecoder {
 public:
  enum class Error {
    kNone,
    // A header announced more than the payload limit, or the unread bytes
    // exceeded what the limit allows.
    kTooLarge,
    // A header announced an empty payload.
    kEmptyFrame,
  };

  explicit FrameDecoder(size_t max_payload = kMaxPreAuthPayload);

  FrameDecoder(const FrameDecoder&) = delete;
  FrameDecoder& operator=(const FrameDecoder&) = delete;

  ~FrameDecoder();

  // Raises or lowers the payload limit, e.g. to kMaxPayload once the peer has
  // authenticated. A frame whose header was already read is checked against
  // the new limit when it is taken.
  void SetMaxPayload(size_t max_payload) { max_payload_ = max_payload; }

  // Adds bytes from the socket. Returns false, and fails the decoder, if the
  // unread bytes would exceed twice a header plus the limit: a peer that
  // keeps sending without the caller taking frames is cut off.
  bool Append(base::span<const uint8_t> bytes);

  // The next complete frame's payload, or nullopt if more bytes are needed or
  // the decoder failed (check failed()).
  std::optional<std::vector<uint8_t>> TakeFrame();

  bool failed() const { return error_ != Error::kNone; }
  Error error() const { return error_; }

  // Bytes received and not yet returned as frames.
  size_t buffered_bytes() const { return buffer_.size() - read_offset_; }

 private:
  void Fail(Error error);

  size_t max_payload_;
  std::vector<uint8_t> buffer_;
  size_t read_offset_ = 0;
  Error error_ = Error::kNone;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_FRAME_H_
