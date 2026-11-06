// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_frame.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

namespace {

std::vector<uint8_t> Bytes(std::string_view text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

std::vector<uint8_t> Header(uint32_t length) {
  return {static_cast<uint8_t>(length >> 24),
          static_cast<uint8_t>(length >> 16), static_cast<uint8_t>(length >> 8),
          static_cast<uint8_t>(length)};
}

std::vector<uint8_t> Concat(std::vector<uint8_t> a,
                            const std::vector<uint8_t>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

}  // namespace

TEST(KoanAgentFrameTest, EncodesLengthBigEndianThenPayload) {
  const auto frame = EncodeFrame(Bytes("hello"));
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(*frame, Concat(Header(5), Bytes("hello")));
}

TEST(KoanAgentFrameTest, EncodeRefusesEmptyAndOversizePayloads) {
  EXPECT_FALSE(EncodeFrame({}).has_value());
  EXPECT_FALSE(EncodeFrame(Bytes("abcd"), /*max_payload=*/3).has_value());
  EXPECT_TRUE(EncodeFrame(Bytes("abc"), /*max_payload=*/3).has_value());
}

TEST(KoanAgentFrameTest, RoundTrip) {
  FrameDecoder decoder(kMaxPayload);
  ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("{\"id\":1}"))));
  const auto payload = decoder.TakeFrame();
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(*payload, Bytes("{\"id\":1}"));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.failed());
  EXPECT_EQ(decoder.buffered_bytes(), 0u);
}

TEST(KoanAgentFrameTest, ReassemblesAFrameDeliveredOneByteAtATime) {
  const std::vector<uint8_t> frame = *EncodeFrame(Bytes("split across reads"));
  FrameDecoder decoder;
  for (size_t i = 0; i < frame.size(); ++i) {
    EXPECT_FALSE(decoder.TakeFrame().has_value());
    ASSERT_TRUE(decoder.Append(base::span(frame).subspan(i, 1u)));
  }
  const auto payload = decoder.TakeFrame();
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(*payload, Bytes("split across reads"));
}

TEST(KoanAgentFrameTest, SeveralFramesInOneRead) {
  std::vector<uint8_t> stream = *EncodeFrame(Bytes("one"));
  stream = Concat(stream, *EncodeFrame(Bytes("two")));
  stream = Concat(stream, *EncodeFrame(Bytes("three")));
  stream = Concat(stream, Header(9));  // the start of a fourth, incomplete
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.Append(stream));
  EXPECT_EQ(*decoder.TakeFrame(), Bytes("one"));
  EXPECT_EQ(*decoder.TakeFrame(), Bytes("two"));
  EXPECT_EQ(*decoder.TakeFrame(), Bytes("three"));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.failed());
  EXPECT_EQ(decoder.buffered_bytes(), kFrameHeaderSize);
}

TEST(KoanAgentFrameTest, AFrameAtTheLimitPassesAndOneByteOverFails) {
  {
    FrameDecoder decoder;  // pre-auth
    ASSERT_TRUE(decoder.Append(
        *EncodeFrame(std::vector<uint8_t>(kMaxPreAuthPayload, 'a'))));
    const auto payload = decoder.TakeFrame();
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->size(), kMaxPreAuthPayload);
  }
  {
    FrameDecoder decoder;
    ASSERT_TRUE(decoder.Append(Header(kMaxPreAuthPayload + 1)));
    EXPECT_FALSE(decoder.TakeFrame().has_value());
    EXPECT_TRUE(decoder.failed());
    EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
  }
}

TEST(KoanAgentFrameTest, ARefusedHeaderKeepsNothingAndNeverWaitsForThePayload) {
  FrameDecoder decoder;
  // A 1 GiB announcement with no payload at all: refused on the header.
  ASSERT_TRUE(decoder.Append(Header(1u << 30)));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
  EXPECT_EQ(decoder.buffered_bytes(), 0u);
}

TEST(KoanAgentFrameTest, MaximumLengthPrefixIsRefused) {
  FrameDecoder decoder(kMaxPayload);
  ASSERT_TRUE(decoder.Append(Header(0xffffffffu)));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
}

TEST(KoanAgentFrameTest, EmptyFrameIsRefused) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.Append(Header(0)));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kEmptyFrame);
}

TEST(KoanAgentFrameTest, FailureIsStickyAndStopsAcceptingBytes) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.Append(Header(0)));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.Append(*EncodeFrame(Bytes("late"))));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_TRUE(decoder.failed());
}

TEST(KoanAgentFrameTest, UnreadBytesAreBounded) {
  // A peer that sends and sends while nobody takes frames is cut off. The
  // bound is twice a header plus the limit, i.e. 8 KiB + 8 bytes pre-auth.
  FrameDecoder decoder;
  const std::vector<uint8_t> chunk(1024, 'x');
  bool failed = false;
  for (int i = 0; i < 64 && !failed; ++i) {
    failed = !decoder.Append(chunk);
  }
  EXPECT_TRUE(failed);
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
  EXPECT_EQ(decoder.buffered_bytes(), 0u);
}

TEST(KoanAgentFrameTest, OneReadLargerThanTheBoundFailsAtOnce) {
  FrameDecoder decoder;
  EXPECT_FALSE(decoder.Append(std::vector<uint8_t>(64 * 1024, 'x')));
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
}

TEST(KoanAgentFrameTest, RaisingTheLimitAfterAuthenticationAdmitsBiggerFrames) {
  FrameDecoder decoder;
  const std::vector<uint8_t> big(10 * 1024, 'b');
  decoder.SetMaxPayload(kMaxPayload);
  ASSERT_TRUE(decoder.Append(*EncodeFrame(big)));
  const auto payload = decoder.TakeFrame();
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(*payload, big);
}

TEST(KoanAgentFrameTest, ALimitLoweredAfterTheHeaderWasReadAppliesWhenTaken) {
  FrameDecoder decoder(kMaxPayload);
  ASSERT_TRUE(decoder.Append(Concat(Header(8 * 1024), Bytes("partial"))));
  decoder.SetMaxPayload(kMaxPreAuthPayload);
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
}

TEST(KoanAgentFrameTest, LongStreamsOfFramesDoNotGrowTheBuffer) {
  FrameDecoder decoder;
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("{\"method\":\"x\"}"))));
    ASSERT_TRUE(decoder.TakeFrame().has_value());
    EXPECT_EQ(decoder.buffered_bytes(), 0u);
  }
}

TEST(KoanAgentFrameTest, BinaryPayloadBytesSurvive) {
  std::vector<uint8_t> payload;
  for (int i = 0; i < 256; ++i) {
    payload.push_back(static_cast<uint8_t>(i));
  }
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.Append(*EncodeFrame(payload)));
  EXPECT_EQ(*decoder.TakeFrame(), payload);
}

}  // namespace koan::agent
