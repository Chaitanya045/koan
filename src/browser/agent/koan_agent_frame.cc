// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_frame.h"

#include <algorithm>
#include <array>

#include "base/containers/span.h"
#include "base/containers/to_vector.h"
#include "base/numerics/byte_conversions.h"
#include "base/numerics/safe_conversions.h"

namespace koan::agent {

namespace {

// A buffer larger than this is given back when it empties.
constexpr size_t kRetainedCapacity = 64 * 1024;

}  // namespace

std::optional<std::vector<uint8_t>> EncodeFrame(
    base::span<const uint8_t> payload,
    size_t max_payload) {
  max_payload = std::min(max_payload, kMaxPayload);
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

FrameDecoder::FrameDecoder() : max_payload_(kMaxPreAuthPayload) {}

FrameDecoder::FrameDecoder(size_t max_payload)
    : max_payload_(std::min(max_payload, kMaxPayload)), authenticated_(true) {}

FrameDecoder::~FrameDecoder() = default;

bool FrameDecoder::OnAuthenticated() {
  if (failed()) {
    return false;
  }
  if (authenticated_) {
    return true;
  }
  if (buffered_bytes() > 0) {
    Fail(Error::kPipelinedBeforeAuth);
    return false;
  }
  authenticated_ = true;
  max_payload_ = kMaxPayload;
  return true;
}

bool FrameDecoder::Append(base::span<const uint8_t> bytes) {
  if (failed()) {
    return false;
  }
  // max_payload_ <= kMaxPayload, so this cannot wrap.
  const size_t limit = 2 * (kFrameHeaderSize + max_payload_);
  if (bytes.size() > limit || buffered_bytes() > limit - bytes.size()) {
    Fail(Error::kTooLarge);
    return false;
  }
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
  return CheckNextHeader();
}

std::optional<std::vector<uint8_t>> FrameDecoder::TakeFrame() {
  if (failed() || buffered_bytes() < kFrameHeaderSize) {
    return std::nullopt;
  }
  if (!authenticated_ && pre_auth_frames_ >= kMaxPreAuthFrames) {
    Fail(Error::kTooManyPreAuthFrames);
    return std::nullopt;
  }
  if (!CheckNextHeader()) {
    return std::nullopt;
  }
  const size_t length = NextLength();
  const base::span<const uint8_t> unread =
      base::span(buffer_).subspan(read_offset_);
  if (unread.size() - kFrameHeaderSize < length) {
    return std::nullopt;
  }
  std::vector<uint8_t> payload =
      base::ToVector(unread.subspan(kFrameHeaderSize, length));
  read_offset_ += kFrameHeaderSize + length;
  if (!authenticated_) {
    ++pre_auth_frames_;
  }
  ReleaseConsumed();
  return payload;
}

bool FrameDecoder::has_partial_frame() const {
  if (failed() || buffered_bytes() == 0) {
    return false;
  }
  if (buffered_bytes() < kFrameHeaderSize) {
    return true;
  }
  return buffered_bytes() - kFrameHeaderSize < NextLength();
}

size_t FrameDecoder::NextLength() const {
  return base::U32FromBigEndian(
      base::span(buffer_).subspan(read_offset_).first<kFrameHeaderSize>());
}

bool FrameDecoder::CheckNextHeader() {
  if (buffered_bytes() < kFrameHeaderSize) {
    return true;
  }
  // The header alone is enough to refuse: nothing of an oversize payload is
  // ever waited for.
  const size_t length = NextLength();
  if (length == 0) {
    Fail(Error::kEmptyFrame);
    return false;
  }
  if (length > max_payload_) {
    Fail(Error::kTooLarge);
    return false;
  }
  return true;
}

void FrameDecoder::ReleaseConsumed() {
  if (read_offset_ == buffer_.size()) {
    buffer_.clear();
    read_offset_ = 0;
    if (buffer_.capacity() > kRetainedCapacity) {
      std::vector<uint8_t>().swap(buffer_);
    }
  } else if (read_offset_ >= buffer_.size() / 2) {
    // Reclaim the consumed prefix once it is at least half of the buffer, so
    // the cost of erasing stays amortised.
    buffer_.erase(buffer_.begin(), buffer_.begin() + read_offset_);
    read_offset_ = 0;
    if (buffer_.capacity() > kRetainedCapacity &&
        buffer_.size() < buffer_.capacity() / 4) {
      buffer_.shrink_to_fit();
    }
  }
}

void FrameDecoder::Fail(Error error) {
  error_ = error;
  std::vector<uint8_t>().swap(buffer_);
  read_offset_ = 0;
}

}  // namespace koan::agent
