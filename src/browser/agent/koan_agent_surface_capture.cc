// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_surface_capture.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/agent/koan_agent_lease.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/codec/webp_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace koan::agent {

namespace {

// How often a copy looks for the tab's surface while it waits for one.
constexpr base::TimeDelta kSurfacePoll = base::Milliseconds(16);

// What a mask paints: opaque, so nothing of the field shows through. Not a
// UI colour (it is in an image for the agent, never on screen).
constexpr SkColor kMaskColor = SkColorSetRGB(0x80, 0x80, 0x80);

// One copy of one tab, from the lease's command to the encoded image. Owns
// itself; deleted in Finish().
class SurfaceCapture : public content::WebContentsObserver {
 public:
  SurfaceCapture(content::WebContents* contents,
                 SurfaceCaptureJob job,
                 SurfaceCaptureCallback done)
      : content::WebContentsObserver(contents),
        job_(std::move(job)),
        done_(std::move(done)) {}
  SurfaceCapture(const SurfaceCapture&) = delete;
  SurfaceCapture& operator=(const SurfaceCapture&) = delete;
  ~SurfaceCapture() override = default;

  void Start() {
    content::RenderWidgetHostView* view =
        web_contents()->GetRenderWidgetHostView();
    if (!view) {
      Finish(SurfaceStatus::kNoView);
      return;
    }
    // A tab that was not drawing (hidden or covered, and no command held its
    // capturer) draws once the lease holds it: wait for that frame, not the
    // one it drew before it was hidden.
    const std::optional<SurfaceGeometry> geometry =
        SurfaceGeometryOf(web_contents());
    needs_fresh_frame_ = job_.fresh_frame || !geometry || !geometry->drawing;
    // As a command does: the lease turns the tab's capturer on (or queues
    // this behind the cap), and EndCommand() lets it go.
    AgentLeaseManager::Get().RunCommand(
        web_contents(), base::BindOnce(&SurfaceCapture::OnLeaseGranted,
                                       weak_factory_.GetWeakPtr(),
                                       web_contents()->GetWeakPtr()));
  }

 private:
  // Static: a capture that finished while it waited for the lease still ends
  // the command the lease began for it.
  static void OnLeaseGranted(base::WeakPtr<SurfaceCapture> capture,
                             base::WeakPtr<content::WebContents> contents) {
    if (!capture) {
      if (contents) {
        AgentLeaseManager::Get().EndCommand(contents.get());
      }
      return;
    }
    capture->holding_ = true;
    capture->BeginWaiting();
  }

  void BeginWaiting() {
    deadline_.Start(FROM_HERE, kSurfaceWait,
                    base::BindOnce(&SurfaceCapture::OnSurfaceTimeout,
                                   base::Unretained(this)));
    if (needs_fresh_frame_) {
      web_contents()->GetPrimaryMainFrame()->InsertVisualStateCallback(
          base::BindOnce(&SurfaceCapture::OnFreshFrame,
                         weak_factory_.GetWeakPtr()));
    }
    PollSurface();
  }

  void OnFreshFrame(bool) {
    needs_fresh_frame_ = false;
    PollSurface();
  }

  void PollSurface() {
    if (copying_) {
      return;
    }
    content::RenderWidgetHostView* view =
        web_contents()->GetRenderWidgetHostView();
    if (!view) {
      Finish(SurfaceStatus::kNoView);
      return;
    }
    if (needs_fresh_frame_ || !view->IsSurfaceAvailableForCopy()) {
      if (!poll_.IsRunning()) {
        poll_.Start(FROM_HERE, kSurfacePoll,
                    base::BindRepeating(&SurfaceCapture::PollSurface,
                                        base::Unretained(this)));
      }
      return;
    }
    poll_.Stop();
    copying_ = true;
    // The copy's own wait: the viz timeout, and ours in case its answer
    // never comes.
    deadline_.Start(
        FROM_HERE, kCopyTimeout + base::Milliseconds(500),
        base::BindOnce(&SurfaceCapture::Finish, base::Unretained(this),
                       SurfaceStatus::kCopyFailed));
    const gfx::Rect source =
        job_.whole ? gfx::Rect()
                   : gfx::Rect(job_.source.x, job_.source.y, job_.source.width,
                               job_.source.height);
    view->CopyFromSurface(
        source, gfx::Size(job_.image_width, job_.image_height), kCopyTimeout,
        base::BindOnce(&SurfaceCapture::OnCopied, weak_factory_.GetWeakPtr()));
  }

  void OnSurfaceTimeout() {
    // No surface, or no fresh frame, in time: never the stale frame from
    // before the tab was hidden. The facade decides (ChooseFallback()).
    Finish(SurfaceStatus::kNoSurface);
  }

  void OnCopied(const content::CopyFromSurfaceResult& result) {
    deadline_.Stop();
    if (!result.has_value() || result->bitmap.drawsNothing()) {
      Finish(SurfaceStatus::kCopyFailed);
      return;
    }
    // The copy is done: the lease may let the tab go while it encodes.
    EndCommand();
    width_ = result->bitmap.width();
    height_ = result->bitmap.height();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::TaskPriority::USER_BLOCKING,
         base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
        base::BindOnce(&EncodeCapture, result->bitmap, job_.format,
                       job_.quality, std::move(job_.masks)),
        base::BindOnce(&SurfaceCapture::OnEncoded, weak_factory_.GetWeakPtr()));
  }

  void OnEncoded(std::optional<std::vector<uint8_t>> encoded) {
    if (!encoded) {
      Finish(SurfaceStatus::kEncodeFailed);
      return;
    }
    encoded_ = std::move(*encoded);
    Finish(SurfaceStatus::kOk);
  }

  // content::WebContentsObserver:
  void WebContentsDestroyed() override {
    // The lease went with the tab.
    holding_ = false;
    Finish(SurfaceStatus::kNoView);
  }

  void EndCommand() {
    if (holding_) {
      holding_ = false;
      if (web_contents()) {
        AgentLeaseManager::Get().EndCommand(web_contents());
      }
    }
  }

  void Finish(SurfaceStatus status) {
    EndCommand();
    SurfaceCaptureResult result;
    result.status = status;
    if (status == SurfaceStatus::kOk) {
      result.encoded = std::move(encoded_);
      result.width = width_;
      result.height = height_;
    }
    SurfaceCaptureCallback done = std::move(done_);
    delete this;
    std::move(done).Run(std::move(result));
  }

  SurfaceCaptureJob job_;
  SurfaceCaptureCallback done_;
  // The lease began a command for this copy (RunCommand()), not ended yet.
  bool holding_ = false;
  bool needs_fresh_frame_ = false;
  bool copying_ = false;
  int width_ = 0;
  int height_ = 0;
  std::vector<uint8_t> encoded_;
  base::OneShotTimer deadline_;
  base::RepeatingTimer poll_;
  base::WeakPtrFactory<SurfaceCapture> weak_factory_{this};
};

}  // namespace

std::optional<SurfaceGeometry> SurfaceGeometryOf(
    content::WebContents* contents) {
  content::RenderWidgetHostView* view =
      contents ? contents->GetRenderWidgetHostView() : nullptr;
  if (!view) {
    return std::nullopt;
  }
  const gfx::Size size = view->GetViewBounds().size();
  const AgentTabLease* lease = AgentTabLease::FromWebContents(contents);
  return SurfaceGeometry{
      size.width(), size.height(), view->GetDeviceScaleFactor(),
      contents->GetVisibility() == content::Visibility::VISIBLE ||
          (lease && lease->capturing())};
}

void CaptureSurface(content::WebContents* contents,
                    SurfaceCaptureJob job,
                    SurfaceCaptureCallback done) {
  if (!contents) {
    SurfaceCaptureResult result;
    result.status = SurfaceStatus::kNoView;
    std::move(done).Run(std::move(result));
    return;
  }
  (new SurfaceCapture(contents, std::move(job), std::move(done)))->Start();
}

std::optional<std::vector<uint8_t>> EncodeCapture(
    const SkBitmap& bitmap,
    ImageFormat format,
    int quality,
    const std::vector<CaptureRect>& masks) {
  SkBitmap image = bitmap;
  if (!masks.empty()) {
    // Painted on a copy: the copy's pixels may be shared.
    SkBitmap painted;
    if (!painted.tryAllocPixels(bitmap.info()) ||
        !bitmap.readPixels(painted.pixmap())) {
      return std::nullopt;
    }
    SkCanvas canvas(painted);
    SkPaint paint;
    paint.setColor(kMaskColor);
    for (const CaptureRect& mask : masks) {
      canvas.drawRect(SkRect::MakeXYWH(mask.x, mask.y, mask.width, mask.height),
                      paint);
    }
    image = painted;
  }
  switch (format) {
    case ImageFormat::kPng:
      // The page is opaque: no alpha channel, a smaller file.
      return gfx::PNGCodec::EncodeBGRASkBitmap(image,
                                               /*discard_transparency=*/true);
    case ImageFormat::kJpeg:
      return gfx::JPEGCodec::Encode(image, quality);
    case ImageFormat::kWebp:
      return gfx::WebpCodec::Encode(image, quality);
  }
  return std::nullopt;
}

}  // namespace koan::agent
