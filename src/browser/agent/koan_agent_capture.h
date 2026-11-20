// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_CAPTURE_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_CAPTURE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "base/values.h"

// Koan.captureScaled (.agent/notes/agent-control.md section 12, "One capture
// path"): a screenshot of any tab the agent may read, its own or the user's,
// copied from the frame the compositor already drew and scaled on the GPU to
// the size asked for. Nothing is emulated or drawn again, so the page cannot
// tell (no devicePixelRatio change, no resize) and a user's tab does not
// flicker; only the small image is read back and encoded.
//
// This file is the pure part: the parameters, the CSS px -> DIP -> device
// pixel mapping (page zoom, pinch zoom, the device scale factor), the image's
// size (never larger than the display draws, the long edge, the token cap),
// the CDP fallback for an agent tab with no surface, and where a mask lands
// in the image. The browser copies, paints the masks and encodes
// (koan_agent_surface_capture.h).
namespace koan::agent {

// What a model is charged for an image: about one token per 28x28 pixels,
// and no more than kMaxImageTokens (a larger image is scaled down by the
// model's API, so its extra pixels cost bytes only).
inline constexpr int kMaxImageTokens = 4784;
inline constexpr int kPixelsPerToken = 28 * 28;
int EstimateImageTokens(int width, int height);

// The longest side an image may have, and the default (CSS pixels of a
// laptop's window, at most).
inline constexpr int kMaxImageLongEdge = 2576;
inline constexpr int kDefaultImageLongEdge = 1568;
inline constexpr int kMinImageLongEdge = 16;
// The most image pixels per CSS pixel one can ask for: more than any display
// draws, so "as sharp as the display" (it is capped to what it draws).
inline constexpr double kMaxCaptureScale = 4;
// CDP's default JPEG and WebP quality (page_handler.cc).
inline constexpr int kDefaultCaptureQuality = 80;

enum class ImageFormat { kPng, kJpeg, kWebp };
std::string_view ImageFormatName(ImageFormat format);

// A rectangle in CSS pixels from the visual viewport's top left, as
// DOM.getContentQuads gives an element's box.
struct ViewportRect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};

// A rectangle of whole pixels (DIPs of the view, or pixels of the image).
struct CaptureRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool IsEmpty() const { return width <= 0 || height <= 0; }
  friend bool operator==(const CaptureRect&, const CaptureRect&) = default;
};

// An error for the agent: a Koan.* code and what to do about it.
struct CaptureError {
  std::string code;
  std::string message;
};

// Koan.captureScaled {targetId, longEdge?, scale?, clip?: {x, y, width,
// height}, format?, quality?}, the targetId aside:
//   longEdge  the image's long side at most, 16 to 2576 (default 1568);
//   scale     image pixels per CSS pixel at most, (0, 4] (default 1: CSS
//             pixels); capped to what the display draws, so 4 means "device
//             pixels";
//   clip      viewport CSS pixels; cut to the viewport;
//   format    png (default), jpeg or webp; quality 0 to 100 for jpeg and webp.
// The image is never larger than the display draws, and never costs more
// than kMaxImageTokens.
struct CaptureRequest {
  int long_edge = kDefaultImageLongEdge;
  double scale = 1;
  std::optional<ViewportRect> clip;
  ImageFormat format = ImageFormat::kPng;
  int quality = kDefaultCaptureQuality;
};
base::expected<CaptureRequest, CaptureError> ParseCaptureRequest(
    const base::DictValue& params);

// The visual viewport, from Page.getLayoutMetrics (cssVisualViewport).
struct PageViewport {
  // Where it is in the document, and its size, in CSS pixels.
  double page_x = 0;
  double page_y = 0;
  double width = 0;
  double height = 0;
  // DIPs per CSS pixel from the browser's zoom (Ctrl/Cmd +/-), and the
  // pinch zoom on top of it.
  double zoom = 1;
  double pinch = 1;
};
std::optional<PageViewport> ReadPageViewport(
    const base::DictValue& layout_metrics);

// The view the page draws into, as the browser has it: its size in DIPs and
// device pixels per DIP (2 on a Retina display).
struct SurfaceGeometry {
  int width = 0;
  int height = 0;
  double device_scale = 1;
  // The tab draws frames now: the user can see it, or its lease holds the
  // capturer. A tab that does not has a frame from before it was hidden.
  bool drawing = true;
};

// What to ask RenderWidgetHostView::CopyFromSurface() for, and the image it
// gives.
struct CapturePlan {
  // The part of the view to copy, in its DIPs; `whole` if it is all of it
  // (copied as an empty rectangle, the surface's own size).
  CaptureRect source;
  bool whole = false;
  // The image (CopyFromSurface's output size).
  int image_width = 0;
  int image_height = 0;
  // CSS pixels per image pixel.
  double css_per_pixel = 1;
  // Image pixels per CSS pixel, and the viewport CSS pixels the image shows.
  double pixels_per_css = 1;
  ViewportRect shows;
};
// Koan.noSurface for a view of no size; Koan.emptyElement for a clip of no
// size; Koan.offScreen for one outside the viewport.
base::expected<CapturePlan, CaptureError> PlanCapture(
    const PageViewport& viewport,
    const SurfaceGeometry& surface,
    const CaptureRequest& request);

// How a copy of the surface went.
enum class SurfaceStatus {
  kOk,
  // The tab has no view or widget (it is closing, or crashed).
  kNoView,
  // No frame came within the wait (a hidden tab that did not draw).
  kNoSurface,
  // The copy did not come back, or came back empty.
  kCopyFailed,
  // The image could not be encoded.
  kEncodeFailed,
};
std::string_view SurfaceStatusName(SurfaceStatus status);

// What to do when the copy did not work: on an agent's own tab, ask CDP
// (Page.captureScreenshot with clip and scale, which draws the page again
// under device emulation; nobody looks at the tab). Never on a user's tab:
// emulation would resize their view and flicker.
enum class CaptureFallback {
  kNone,
  kCdp,
  kFail,
};
CaptureFallback ChooseFallback(SurfaceStatus status, bool agent_owned);
// The error for a copy that failed with no fallback.
CaptureError SurfaceError(SurfaceStatus status);

// Page.captureScreenshot's params for the same image as `plan` (its clip in
// CDP's units: DIPs from the document's top left).
base::DictValue CdpCaptureParams(const PageViewport& viewport,
                                 const SurfaceGeometry& surface,
                                 const CapturePlan& plan,
                                 const CaptureRequest& request);

// Masks: boxes (viewport CSS pixels) that are painted over the bitmap before
// it is encoded, in the image's pixels: grown by `padding` CSS pixels,
// rounded outwards, cut to the image; boxes outside it are left out. Koan's
// secret-field masking (the card fields it filled) gives its boxes here.
std::vector<CaptureRect> MaskPixels(const CapturePlan& plan,
                                    base::span<const ViewportRect> boxes,
                                    double padding);

// Koan.captureScaled's answer: {data (base64), width, height, cssPerPixel,
// format, via: "surface" | "cdp", fallbackReason (with "cdp"), masked (with
// masks)}.
struct CaptureAnswer {
  std::string data;
  int width = 0;
  int height = 0;
  double css_per_pixel = 1;
  ImageFormat format = ImageFormat::kPng;
  // nullopt: copied from the surface; else why CDP drew it.
  std::optional<SurfaceStatus> fallback;
  int masked = 0;
};
base::DictValue CaptureAnswerDict(CaptureAnswer answer);

// A copy of a tab's surface, as the facade asks the browser for it
// (FacadeBackend::CopySurface(), koan_agent_surface_capture.h).
struct SurfaceCaptureJob {
  // What to copy (CapturePlan's: the part of the view in DIPs, the image's
  // size) and how to encode it.
  CaptureRect source;
  bool whole = false;
  int image_width = 0;
  int image_height = 0;
  ImageFormat format = ImageFormat::kPng;
  int quality = kDefaultCaptureQuality;
  // The tab was not drawing when the capture began: wait for a frame drawn
  // since, not the one from before it was hidden.
  bool fresh_frame = false;
  // Painted over the bitmap before it is encoded, in the image's pixels
  // (MaskPixels()).
  std::vector<CaptureRect> masks;
};

struct SurfaceCaptureResult {
  SurfaceStatus status = SurfaceStatus::kCopyFailed;
  std::vector<uint8_t> encoded;
  // The image as encoded.
  int width = 0;
  int height = 0;
};
using SurfaceCaptureCallback = base::OnceCallback<void(SurfaceCaptureResult)>;

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_CAPTURE_H_
