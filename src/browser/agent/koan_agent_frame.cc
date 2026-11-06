// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_frame.h"

#include <array>
#include <utility>

#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/numerics/byte_conversions.h"
#include "base/numerics/safe_conversions.h"

namespace koan::agent {

std::optional<std::vector<uint8_t>> EncodeFrame(
    base::span<const uint8_t> payload,
    size_t max_payload) {
  if (payload.empty() || payload.size() > max_payload ||
      !base::IsValueInRangeForNumericType<uint32_t>(payload.size())) {
    return std::nullopt;
  }
  std::vector<uint8_t> frame;
  frame.reserve(kFrameHeaderSize + payload.size());
  const std::array<uint8_t, kFrameHeaderSize> header =
      base::U32ToBigEndian(static_cast<uint32_t>(payload.size()));
  frame.insert(frame.end(), header.begin(), header.end());
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

FrameDecoder::FrameDecoder(size_t max_payload) : max_payload_(max_payload) {}

FrameDecoder::~FrameDecoder() = default;

bool FrameDecoder::Append(base::span<const uint8_t> bytes) {
  if (failed()) {
    return false;
  }
  const size_t limit = 2 * (kFrameHeaderSize + max_payload_);
  if (bytes.size() > limit || buffered_bytes() > limit - bytes.size()) {
    Fail(Error::kTooLarge);
    return false;
  }
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  return true;
}

std::optional<std::vector<uint8_t>> FrameDecoder::TakeFrame() {
  if (failed() || buffered_bytes() < kFrameHeaderSize) {
    return std::nullopt;
  }
  const base::span<const uint8_t> unread =
      base::span(buffer_).subspan(read_offset_);
  const size_t length =
      base::U32FromBigEndian(unread.first<kFrameHeaderSize>());
  // The header alone is enough to refuse: nothing of an oversize payload is
  // ever waited for or kept.
  if (length == 0) {
    Fail(Error::kEmptyFrame);
    return std::nullopt;
  }
  if (length > max_payload_) {
    Fail(Error::kTooLarge);
    return std::nullopt;
  }
  if (unread.size() - kFrameHeaderSize < length) {
    return std::nullopt;
  }
  std::vector<uint8_t> payload =
      base::ToVector(unread.subspan(kFrameHeaderSize, length));
  read_offset_ += kFrameHeaderSize + length;
  // Reclaim the consumed prefix once it is at least half of the buffer, so the
  // cost of erasing stays amortised.
  if (read_offset_ == buffer_.size()) {
    buffer_.clear();
    read_offset_ = 0;
  } else if (read_offset_ >= buffer_.size() / 2) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + read_offset_);
    read_offset_ = 0;
  }
  return payload;
}

void FrameDecoder::Fail(Error error) {
  error_ = error;
  buffer_.clear();
  buffer_.shrink_to_fit();
  read_offset_ = 0;
}

}  // namespace koan::agent
