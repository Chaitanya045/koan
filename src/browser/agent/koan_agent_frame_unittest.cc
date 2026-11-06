// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_frame.h"

#include <stdint.h>

#include <algorithm>
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

// A small deterministic generator, so a failure of the randomised test can be
// reproduced from its seed.
class Lcg {
 public:
  explicit Lcg(uint64_t seed) : state_(seed) {}
  uint32_t Next() {
    state_ = state_ * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(state_ >> 33);
  }
  size_t Below(size_t bound) { return Next() % bound; }

 private:
  uint64_t state_;
};

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
  // A limit above the maximum is the maximum.
  EXPECT_FALSE(EncodeFrame(std::vector<uint8_t>(kMaxPayload + 1, 'a'), SIZE_MAX)
                   .has_value());
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
  ASSERT_TRUE(decoder.OnAuthenticated());
  ASSERT_TRUE(decoder.Append(stream));
  EXPECT_EQ(*decoder.TakeFrame(), Bytes("one"));
  EXPECT_EQ(*decoder.TakeFrame(), Bytes("two"));
  EXPECT_EQ(*decoder.TakeFrame(), Bytes("three"));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.failed());
  EXPECT_EQ(decoder.buffered_bytes(), kFrameHeaderSize);
  EXPECT_TRUE(decoder.has_partial_frame());
}

TEST(KoanAgentFrameTest, AFrameAtTheLimitPassesAndOneByteOverFails) {
  {
    FrameDecoder decoder;  // before authentication
    ASSERT_TRUE(decoder.Append(
        *EncodeFrame(std::vector<uint8_t>(kMaxPreAuthPayload, 'a'))));
    const auto payload = decoder.TakeFrame();
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(payload->size(), kMaxPreAuthPayload);
  }
  {
    FrameDecoder decoder;
    EXPECT_FALSE(decoder.Append(Header(kMaxPreAuthPayload + 1)));
    EXPECT_TRUE(decoder.failed());
    EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
  }
}

TEST(KoanAgentFrameTest, ARefusedHeaderKeepsNothingAndNeverWaitsForThePayload) {
  FrameDecoder decoder;
  // A 1 GiB announcement with no payload at all: refused on the header.
  EXPECT_FALSE(decoder.Append(Header(1u << 30)));
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
  EXPECT_EQ(decoder.buffered_bytes(), 0u);
  EXPECT_EQ(decoder.buffer_capacity(), 0u);
  EXPECT_FALSE(decoder.TakeFrame().has_value());
}

TEST(KoanAgentFrameTest, MaximumLengthPrefixIsRefused) {
  FrameDecoder decoder(kMaxPayload);
  EXPECT_FALSE(decoder.Append(Header(0xffffffffu)));
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
}

TEST(KoanAgentFrameTest, EmptyFrameIsRefused) {
  FrameDecoder decoder;
  EXPECT_FALSE(decoder.Append(Header(0)));
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kEmptyFrame);
}

TEST(KoanAgentFrameTest, ALaterBadHeaderIsRefusedWhenItBecomesTheNextOne) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.OnAuthenticated());
  ASSERT_TRUE(decoder.Append(Concat(*EncodeFrame(Bytes("ok")), Header(0))));
  EXPECT_EQ(*decoder.TakeFrame(), Bytes("ok"));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kEmptyFrame);
}

TEST(KoanAgentFrameTest, FailureIsStickyAndStopsAcceptingBytes) {
  FrameDecoder decoder;
  EXPECT_FALSE(decoder.Append(Header(0)));
  EXPECT_FALSE(decoder.Append(*EncodeFrame(Bytes("late"))));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.OnAuthenticated());
  EXPECT_TRUE(decoder.failed());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kEmptyFrame);
}

TEST(KoanAgentFrameTest, UnreadBytesAreBounded) {
  // A peer that sends and sends while nobody takes frames is cut off. The
  // bound is twice a header plus the limit, i.e. 8 KiB + 8 bytes before
  // authentication. The first frame is valid (4096 bytes announced), so only
  // the bound can stop the stream.
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.Append(Header(kMaxPreAuthPayload)));
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

TEST(KoanAgentFrameTest, AuthenticationRaisesTheLimit) {
  FrameDecoder decoder;
  EXPECT_EQ(decoder.max_payload(), kMaxPreAuthPayload);
  ASSERT_TRUE(decoder.OnAuthenticated());
  EXPECT_EQ(decoder.max_payload(), kMaxPayload);
  const std::vector<uint8_t> big(10 * 1024, 'b');
  ASSERT_TRUE(decoder.Append(*EncodeFrame(big)));
  const auto payload = decoder.TakeFrame();
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(*payload, big);
}

TEST(KoanAgentFrameTest,
     BytesReceivedBeforeAuthenticationAreNeverHandledAsAuthenticated) {
  // The peer pipelined a command behind authenticate: a protocol violation.
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.Append(
      Concat(*EncodeFrame(Bytes("authenticate")), *EncodeFrame(Bytes("cmd")))));
  ASSERT_TRUE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.OnAuthenticated());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kPipelinedBeforeAuth);
  EXPECT_FALSE(decoder.TakeFrame().has_value());
}

TEST(KoanAgentFrameTest, APeerThatDoesNotAuthenticateGetsAFewFramesOnly) {
  FrameDecoder decoder;
  for (int i = 0; i < kMaxPreAuthFrames; ++i) {
    ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("{\"id\":1}"))));
    ASSERT_TRUE(decoder.TakeFrame().has_value()) << i;
  }
  ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("{\"id\":2}"))));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooManyPreAuthFrames);
}

TEST(KoanAgentFrameTest, AnAuthenticatedPeerIsNotCountedOut) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.OnAuthenticated());
  for (int i = 0; i < 10 * kMaxPreAuthFrames; ++i) {
    ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("{}"))));
    ASSERT_TRUE(decoder.TakeFrame().has_value()) << i;
  }
}

TEST(KoanAgentFrameTest,
     ADecoderWithAnExplicitLimitIsAuthenticatedFromTheStart) {
  // The CLI reading replies: more than kMaxPreAuthFrames frames, no
  // OnAuthenticated() call.
  FrameDecoder decoder(kMaxPayload);
  for (int i = 0; i < 10 * kMaxPreAuthFrames; ++i) {
    ASSERT_TRUE(
        decoder.Append(*EncodeFrame(Bytes("{\"id\":1,\"result\":{}}"))));
    ASSERT_TRUE(decoder.TakeFrame().has_value()) << i;
  }
  // Calling it anyway changes nothing, even with bytes unread.
  ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("x"))));
  EXPECT_TRUE(decoder.OnAuthenticated());
  EXPECT_EQ(decoder.max_payload(), kMaxPayload);
  EXPECT_FALSE(decoder.failed());
  EXPECT_TRUE(decoder.TakeFrame().has_value());
}

TEST(KoanAgentFrameTest, ADefaultDecoderIsForAPeerThatMustAuthenticate) {
  FrameDecoder decoder;
  EXPECT_EQ(decoder.max_payload(), kMaxPreAuthPayload);
  // It is cut off after kMaxPreAuthFrames, which is why a reader that never
  // authenticates must use the explicit-limit constructor.
  for (int i = 0; i < kMaxPreAuthFrames; ++i) {
    ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("{}"))));
    ASSERT_TRUE(decoder.TakeFrame().has_value());
  }
  ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("{}"))));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooManyPreAuthFrames);
}

TEST(KoanAgentFrameTest, TheLimitCannotBeSetAboveTheMaximum) {
  FrameDecoder decoder(SIZE_MAX);
  EXPECT_EQ(decoder.max_payload(), kMaxPayload);
  FrameDecoder just_over(kMaxPayload + 1);
  EXPECT_EQ(just_over.max_payload(), kMaxPayload);
  // A header above the maximum fails whatever limit was asked for.
  EXPECT_FALSE(decoder.Append(Header(kMaxPayload + 1)));
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
}

TEST(KoanAgentFrameTest, AHeaderOfExactlyTheMaximumIsAcceptedAndWaits) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.OnAuthenticated());
  ASSERT_TRUE(decoder.Append(Header(kMaxPayload)));
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.failed());
  EXPECT_TRUE(decoder.has_partial_frame());
}

TEST(KoanAgentFrameTest, AHeaderOneOverTheMaximumFailsInAppend) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.OnAuthenticated());
  EXPECT_FALSE(decoder.Append(Header(kMaxPayload + 1)));
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kTooLarge);
}

TEST(KoanAgentFrameTest, HalfAHeaderRefusesNothing) {
  FrameDecoder decoder;
  const std::vector<uint8_t> header = Header(0);
  EXPECT_TRUE(decoder.Append(base::span(header).first(2u)));
  EXPECT_FALSE(decoder.failed());
  EXPECT_TRUE(decoder.has_partial_frame());
}

TEST(KoanAgentFrameTest, ReportsAnUnfinishedFrame) {
  FrameDecoder decoder;
  EXPECT_FALSE(decoder.has_partial_frame());
  const std::vector<uint8_t> frame = *EncodeFrame(Bytes("0123456789"));
  ASSERT_TRUE(decoder.Append(base::span(frame).first(2u)));
  EXPECT_TRUE(decoder.has_partial_frame());
  ASSERT_TRUE(decoder.Append(base::span(frame).subspan(2u, 6u)));
  EXPECT_TRUE(decoder.has_partial_frame());
  ASSERT_TRUE(decoder.Append(base::span(frame).subspan(8u)));
  EXPECT_FALSE(decoder.has_partial_frame());  // complete, waiting to be taken
  ASSERT_TRUE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.has_partial_frame());
}

TEST(KoanAgentFrameTest, TwoMaximumSizeFramesBackToBack) {
  const size_t limit = 1024 * 1024;
  FrameDecoder decoder(limit);
  const std::vector<uint8_t> big(limit, 'm');
  ASSERT_TRUE(decoder.Append(
      Concat(*EncodeFrame(big, limit), *EncodeFrame(big, limit))));
  EXPECT_EQ(decoder.TakeFrame()->size(), limit);
  EXPECT_EQ(decoder.TakeFrame()->size(), limit);
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.failed());
}

TEST(KoanAgentFrameTest, MemoryIsGivenBackAfterALargeFrame) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.OnAuthenticated());
  const std::vector<uint8_t> big(kMaxPayload, 'x');
  ASSERT_TRUE(decoder.Append(*EncodeFrame(big)));
  EXPECT_GT(decoder.buffer_capacity(), kMaxPayload);
  ASSERT_TRUE(decoder.TakeFrame().has_value());
  EXPECT_LE(decoder.buffer_capacity(), 64u * 1024u);
}

TEST(KoanAgentFrameTest, MemoryIsGivenBackWhenAFailureDropsTheBuffer) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.OnAuthenticated());
  // A 1 MiB frame, then a header announcing nothing behind it.
  ASSERT_TRUE(decoder.Append(
      Concat(*EncodeFrame(std::vector<uint8_t>(1024 * 1024, 'x')), Header(0))));
  ASSERT_TRUE(decoder.TakeFrame().has_value());
  EXPECT_FALSE(decoder.TakeFrame().has_value());
  EXPECT_EQ(decoder.error(), FrameDecoder::Error::kEmptyFrame);
  EXPECT_EQ(decoder.buffer_capacity(), 0u);
}

TEST(KoanAgentFrameTest, LongStreamsOfFramesDoNotGrowTheBuffer) {
  FrameDecoder decoder;
  ASSERT_TRUE(decoder.OnAuthenticated());
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(decoder.Append(*EncodeFrame(Bytes("{\"method\":\"x\"}"))));
    ASSERT_TRUE(decoder.TakeFrame().has_value());
    EXPECT_EQ(decoder.buffered_bytes(), 0u);
  }
  EXPECT_LE(decoder.buffer_capacity(), 64u * 1024u);
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

TEST(KoanAgentFrameTest, AStreamCutAtRandomPointsYieldsTheSameFrames) {
  for (uint64_t seed = 1; seed <= 200; ++seed) {
    Lcg random(seed);
    std::vector<std::vector<uint8_t>> payloads;
    std::vector<uint8_t> stream;
    const size_t count = 1 + random.Below(12);
    for (size_t i = 0; i < count; ++i) {
      std::vector<uint8_t> payload(1 + random.Below(3000));
      for (uint8_t& byte : payload) {
        byte = static_cast<uint8_t>(random.Next());
      }
      stream = Concat(stream, *EncodeFrame(payload));
      payloads.push_back(std::move(payload));
    }
    FrameDecoder decoder;
    ASSERT_TRUE(decoder.OnAuthenticated());
    std::vector<std::vector<uint8_t>> out;
    size_t offset = 0;
    while (offset < stream.size()) {
      const size_t piece =
          std::min(1 + random.Below(2500), stream.size() - offset);
      ASSERT_TRUE(decoder.Append(base::span(stream).subspan(offset, piece)))
          << "seed " << seed;
      offset += piece;
      while (auto frame = decoder.TakeFrame()) {
        out.push_back(std::move(*frame));
      }
    }
    EXPECT_EQ(out, payloads) << "seed " << seed;
    EXPECT_FALSE(decoder.failed()) << "seed " << seed;
    EXPECT_EQ(decoder.buffered_bytes(), 0u) << "seed " << seed;
  }
}

}  // namespace koan::agent
