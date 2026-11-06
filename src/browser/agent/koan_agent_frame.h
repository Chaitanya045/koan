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
// built for hostile input: the length of the next frame is checked as soon as
// its header has arrived, before the decoder waits for any payload, and what it
// holds is bounded.
namespace koan::agent {

inline constexpr size_t kFrameHeaderSize = 4;

// Before a connection has authenticated, only Koan.pair / Koan.authenticate
// are parsed and a frame may carry at most this many payload bytes.
inline constexpr size_t kMaxPreAuthPayload = 4 * 1024;

// After authentication, and the most any decoder allows. Larger results travel
// as IO stream reads in chunks.
inline constexpr size_t kMaxPayload = 32 * 1024 * 1024;

// A peer that has not authenticated gets this many frames, in total, before the
// decoder fails: pairing and authentication need two or three.
inline constexpr int kMaxPreAuthFrames = 4;

// The frame for `payload`: the header and the payload. nullopt if the payload
// is empty or longer than `max_payload` (itself at most kMaxPayload).
std::optional<std::vector<uint8_t>> EncodeFrame(
    base::span<const uint8_t> payload,
    size_t max_payload = kMaxPayload);

// Splits a byte stream into frames. Feed it what the socket delivers with
// Append() and take frames out with TakeFrame(). Once it fails, it stays failed
// and the caller closes the connection.
//
// Memory: the unread bytes are bounded by twice a header plus the payload
// limit, so a decoder holds at most about 64 MB after authentication (the
// vector may reach twice that while it grows) and about 8 KB before. The buffer
// is released when it empties, so a connection that once received a large frame
// does not keep the memory.
//
// Authentication contract (the gateway's, the decoder only helps):
//  - Before authentication the decoder takes at most kMaxPreAuthFrames frames
//  of
//    kMaxPreAuthPayload bytes. The caller handles only Koan.pair and
//    Koan.authenticate, and takes one frame at a time: while a signature check
//    or a user prompt is pending, it does not call TakeFrame().
//  - A peer must wait for the reply that completes authentication before it
//    sends anything else. OnAuthenticated() fails the decoder if bytes are
//    still unread, so nothing received before authentication is ever handled
//    as authenticated. (A client that pipelines a large command behind
//    authenticate is cut off here.)
class FrameDecoder {
 public:
  enum class Error {
    kNone,
    // A header announced more than the payload limit, or the unread bytes
    // exceeded what the limit allows.
    kTooLarge,
    // A header announced an empty payload.
    kEmptyFrame,
    // A peer that had not authenticated sent more than kMaxPreAuthFrames.
    kTooManyPreAuthFrames,
    // Bytes were received after authenticate and before it completed.
    kPipelinedBeforeAuth,
  };

  // A decoder for a peer that has not authenticated: 4 KB frames, at most
  // kMaxPreAuthFrames of them, until OnAuthenticated(). This is the one the
  // gateway uses for every accepted connection.
  FrameDecoder();

  // A decoder for a stream whose peer needs no authentication, with this
  // payload limit (clamped to kMaxPayload): the `koan` CLI reading the
  // browser's replies, and tests. It counts as authenticated from the start,
  // so it never runs out of frames and OnAuthenticated() has nothing to do.
  // A decoder that a CLI or test builds with the default constructor and then
  // never authenticates would be cut off after kMaxPreAuthFrames replies.
  explicit FrameDecoder(size_t max_payload);

  FrameDecoder(const FrameDecoder&) = delete;
  FrameDecoder& operator=(const FrameDecoder&) = delete;

  ~FrameDecoder();

  // Called once, when the peer has authenticated: raises the limit to
  // kMaxPayload and stops counting pre-auth frames. Returns false, and fails
  // the decoder, if bytes are still unread (kPipelinedBeforeAuth). The limit
  // can only go up. On a decoder that was authenticated from the start (the
  // explicit-limit constructor) it changes nothing and returns true.
  bool OnAuthenticated();

  // Adds bytes from the socket. Returns false, and fails the decoder, if the
  // unread bytes would exceed twice a header plus the limit, or if the first
  // unread frame's header, now complete, announces zero bytes or more than the
  // limit (the peer is not waited for).
  bool Append(base::span<const uint8_t> bytes);

  // The next complete frame's payload, or nullopt if more bytes are needed or
  // the decoder failed (check failed()).
  std::optional<std::vector<uint8_t>> TakeFrame();

  bool failed() const { return error_ != Error::kNone; }
  Error error() const { return error_; }
  size_t max_payload() const { return max_payload_; }

  // Bytes received and not yet returned as frames.
  size_t buffered_bytes() const { return buffer_.size() - read_offset_; }

  // Whether the unread bytes are the start of a frame whose end has not
  // arrived: the gateway's 10 second timer for an unfinished frame runs while
  // this is true.
  bool has_partial_frame() const;

  // For tests and the gateway's accounting: the memory the buffer holds.
  size_t buffer_capacity() const { return buffer_.capacity(); }

 private:
  // The announced payload length of the first unread frame; requires
  // kFrameHeaderSize unread bytes.
  size_t NextLength() const;
  // Fails the decoder if the first unread frame's header is complete and
  // announces something that is not allowed.
  bool CheckNextHeader();
  void ReleaseConsumed();
  void Fail(Error error);

  size_t max_payload_;
  bool authenticated_ = false;
  int pre_auth_frames_ = 0;
  std::vector<uint8_t> buffer_;
  size_t read_offset_ = 0;
  Error error_ = Error::kNone;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_FRAME_H_
