// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/time_formatting.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/icon_loader.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_source.h"
#include "components/download/public/common/download_url_parameters.h"
#include "components/download/public/common/simple_download_manager.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_library_sections.h"
#include "koan/browser/ui/views/koan_library_view.h"
#include "net/base/mime_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/decode_image.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/clipboard/file_info.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/text/bytes_formatting.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/compositor.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

constexpr int kPageSize = 100;
// Zen's OPENING_FEEDBACK_MS: how long "Opening in file manager…" shows.
constexpr base::TimeDelta kOpeningFeedback = base::Milliseconds(1500);
// Progress updates arrive many times a second; repaint rows at most this
// often.
constexpr base::TimeDelta kUpdateDelay = base::Milliseconds(250);
// Pictures larger than this get a file icon instead of a preview.
constexpr int64_t kMaxPreviewBytes = 20 * 1024 * 1024;
constexpr int kIconSize = 22;
constexpr int kPreviewSize = 34;
// A download coming in: an arrow inside a progress ring (Zen's 32px ring,
// stroke 3 on a 32-unit grid).
constexpr int kArrowSize = 14;
constexpr float kRingRadius = 13.f;
constexpr float kRingWidth = 2.5f;
// Zen's names for the file manager, and its icon for showing a file there
// (a magnifier on macOS, a folder elsewhere).
#if BUILDFLAG(IS_MAC)
constexpr char16_t kShowInFolder[] = u"Show in Finder";
constexpr char16_t kOpeningInFolder[] = u"Opening in Finder\u2026";
const gfx::VectorIcon& ShowInFolderIcon() {
  return vector_icons::kSearchIcon;
}
#else
constexpr char16_t kShowInFolder[] = u"Show in folder";
constexpr char16_t kOpeningInFolder[] = u"Opening in file manager\u2026";
const gfx::VectorIcon& ShowInFolderIcon() {
  return vector_icons::kFolderOpenIcon;
}
#endif

// Zen's statusSeparator and the dot between transfer figures.
constexpr char16_t kSeparator[] = u" — ";
constexpr char16_t kDot[] = u" · ";

// Zen's FILE_GROUPS: pictures, video and sound by content type, the rest by
// extension.
constexpr struct {
  const char* group;
  const char* extensions;
} kExtensionGroups[] = {
    {"documents",
     " pdf doc docx xls xlsx ppt pptx txt md rtf odt ods odp csv epub pages "
     "numbers key "},
    {"archives", " zip rar 7z tar gz bz2 xz tgz zst "},
    {"apps", " dmg pkg exe msi app deb rpm appimage apk jar "},
};

std::string ExtensionOf(const base::FilePath& path) {
  std::string extension = path.FinalExtension();
  if (!extension.empty() && extension[0] == '.') {
    extension.erase(0, 1);
  }
  return base::ToLowerASCII(extension);
}

// The filter group a file belongs to ("images", "video", ...), if any.
std::string FileGroupOf(const base::FilePath& path) {
  const std::string extension = ExtensionOf(path);
  if (extension.empty()) {
    return std::string();
  }
  // What the browser can't tell (Matroska) or types as a container (Ogg).
  if (extension == "mkv") {
    return "video";
  }
  if (extension == "ogg") {
    return "audio";
  }
  std::string mime;
  if (net::GetWellKnownMimeTypeFromExtension(extension, &mime)) {
    if (base::StartsWith(mime, "image/")) {
      return "images";
    }
    if (base::StartsWith(mime, "video/")) {
      return "video";
    }
    if (base::StartsWith(mime, "audio/")) {
      return "audio";
    }
  }
  const std::string needle = base::StrCat({" ", extension, " "});
  for (const auto& group : kExtensionGroups) {
    if (std::string_view(group.extensions).find(needle) !=
        std::string_view::npos) {
      return group.group;
    }
  }
  return std::string();
}

// Pictures the file itself can stand in for its icon (Zen's
// canDrawThumbnail).
bool CanPreview(const base::FilePath& path) {
  static constexpr const char* kPreviewable[] = {"png",  "jpg", "jpeg", "gif",
                                                 "webp", "bmp", "ico",  "avif"};
  return std::ranges::contains(kPreviewable, ExtensionOf(path));
}

std::u16string FileNameOf(const download::DownloadItem* item) {
  const base::FilePath name = item->GetFileNameToReportUser();
  return name.empty() ? base::UTF8ToUTF16(item->GetURL().spec())
                      : name.LossyDisplayName();
}

// "https://www.example.com/a/" -> "example.com/a".
std::u16string FormatUrl(const GURL& url) {
  std::string text = url.spec();
  for (const char* prefix : {"https://", "http://"}) {
    if (base::StartsWith(text, prefix)) {
      text.erase(0, strlen(prefix));
      if (base::StartsWith(text, "www.")) {
        text.erase(0, 4);
      }
      break;
    }
  }
  if (base::EndsWith(text, "/")) {
    text.pop_back();
  }
  return base::UTF8ToUTF16(text);
}

std::u16string Bytes(int64_t bytes) {
  return ui::FormatBytes(
      base::ByteSize(static_cast<uint64_t>(std::max<int64_t>(bytes, 0))));
}

// When a download finished, as Firefox's DownloadUtils.getReadableDates
// puts it: the time today, "Yesterday", then the date.
std::u16string ReadableDate(base::Time time) {
  const base::Time today = base::Time::Now().LocalMidnight();
  if (time >= today) {
    return base::TimeFormatTimeOfDay(time);
  }
  const std::u16string relative = ui::TimeFormat::RelativeDate(time, today);
  return relative.empty() ? base::TimeFormatShortDate(time) : relative;
}

bool IsPending(const download::DownloadItem* item) {
  return item->GetState() == download::DownloadItem::IN_PROGRESS;
}

bool HasFile(const download::DownloadItem* item) {
  return item->GetState() == download::DownloadItem::COMPLETE &&
         !item->GetFileExternallyRemoved();
}

bool IsBlocked(const download::DownloadItem* item) {
  return item->IsDangerous() && !item->IsDone();
}

// Zen's canRetry: stopped before it was done, and not for safety reasons.
bool CanRetry(const download::DownloadItem* item) {
  const download::DownloadItem::DownloadState state = item->GetState();
  return (state == download::DownloadItem::CANCELLED ||
          state == download::DownloadItem::INTERRUPTED) &&
         !item->IsDangerous();
}

// "156 MB/1.07 GB · 3.2 MB/s · 1m 14s" (Zen's transferText).
std::u16string TransferText(const download::DownloadItem* item, bool speed) {
  std::vector<std::u16string> parts;
  const int64_t total = item->GetTotalBytes();
  if (total > 0) {
    parts.push_back(
        base::StrCat({Bytes(item->GetReceivedBytes()), u"/", Bytes(total)}));
  } else {
    parts.push_back(Bytes(item->GetReceivedBytes()));
  }
  if (speed && !item->IsPaused() && item->CurrentSpeed() > 0) {
    parts.push_back(base::StrCat({Bytes(item->CurrentSpeed()), u"/s"}));
    base::TimeDelta remaining;
    if (total > 0 && item->TimeRemaining(&remaining)) {
      parts.push_back(ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_REMAINING,
                                             ui::TimeFormat::LENGTH_SHORT,
                                             remaining));
    }
  }
  return base::JoinString(parts, kDot);
}

// The line under a download's name (Zen's #statusText).
std::u16string StatusText(const download::DownloadItem* item) {
  switch (item->GetState()) {
    case download::DownloadItem::IN_PROGRESS:
      if (IsBlocked(item)) {
        return u"This file may be dangerous";
      }
      if (item->IsPaused()) {
        return base::StrCat({u"Paused", kDot, TransferText(item, false)});
      }
      return TransferText(item, /*speed=*/true);
    case download::DownloadItem::COMPLETE: {
      if (item->GetFileExternallyRemoved()) {
        return u"File moved or missing";
      }
      std::vector<std::u16string> parts = {Bytes(item->GetTotalBytes())};
      const std::string site =
          net::registry_controlled_domains::GetDomainAndRegistry(
              item->GetURL(),
              net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
      if (!site.empty()) {
        parts.push_back(base::UTF8ToUTF16(site));
      }
      parts.push_back(ReadableDate(item->GetEndTime()));
      return base::JoinString(parts, kSeparator);
    }
    case download::DownloadItem::CANCELLED:
      return u"Canceled";
    case download::DownloadItem::INTERRUPTED:
      return u"Failed";
    case download::DownloadItem::MAX_DOWNLOAD_STATE:
      break;
  }
  return std::u16string();
}

std::optional<std::string> ReadPreviewFile(const base::FilePath& path) {
  std::string data;
  if (!base::ReadFileToStringWithMaxSize(path, &data, kMaxPreviewBytes)) {
    return std::nullopt;
  }
  return data;
}

// A download's icon: its file's icon or a preview of the picture, or, while
// it comes in, an arrow inside a progress ring (Zen's .zen-library-row-icon
// and .zen-library-row-progress).
class DownloadIconView : public views::View {
  METADATA_HEADER(DownloadIconView, views::View)

 public:
  DownloadIconView() { SetCanProcessEventsWithinSubtree(false); }

  void SetPending(bool pending, float progress) {
    pending_ = pending;
    progress_ = progress;
    SchedulePaint();
  }
  void SetImage(const gfx::ImageSkia& image, bool preview) {
    image_ = image;
    preview_ = preview;
    SchedulePaint();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kPreviewSize - 8, kPreviewSize - 8);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    const gfx::RectF bounds(GetLocalBounds());
    const gfx::PointF center = bounds.CenterPoint();
    if (pending_) {
      // A downward arrow in the accent colour, ringed by the progress.
      const SkColor accent = provider->GetColor(kColorKoanLibraryIcon);
      const gfx::ImageSkia arrow = gfx::CreateVectorIcon(
          vector_icons::kDownloadIcon, kArrowSize, accent);
      canvas->DrawImageInt(arrow, std::lround(center.x() - kArrowSize / 2.f),
                           std::lround(center.y() - kArrowSize / 2.f));
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(kRingWidth);
      flags.setColor(provider->GetColor(kColorKoanLibraryProgressTrack));
      const float radius = kRingRadius;
      canvas->DrawCircle(center, radius, flags);
      flags.setColor(accent);
      flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
      // Without a size to go by, a quarter of the ring (Zen spins it).
      const float sweep = 360.f * (progress_ >= 0.f ? progress_ : 0.25f);
      canvas->sk_canvas()->drawArc(
          SkRect::MakeLTRB(center.x() - radius, center.y() - radius,
                           center.x() + radius, center.y() + radius),
          -90.f, sweep, flags);
      return;
    }
    const int size = preview_ ? kPreviewSize : kIconSize;
    gfx::ImageSkia image = image_;
    if (image.isNull()) {
      image = gfx::CreateVectorIcon(
          vector_icons::kInsertDriveFileOutlineOldIcon, kIconSize,
          provider->GetColor(kColorKoanLibraryForeground));
    }
    const gfx::Rect target(std::lround(center.x() - size / 2.f),
                           std::lround(center.y() - size / 2.f), size, size);
    canvas->Save();
    if (preview_) {
      SkPath clip = SkPath::RRect(gfx::RectToSkRect(target),
                                  library::kSmallRadius, library::kSmallRadius);
      canvas->ClipPath(clip, /*do_anti_alias=*/true);
    }
    canvas->DrawImageInt(image, 0, 0, image.width(), image.height(), target.x(),
                         target.y(), target.width(), target.height(),
                         /*filter=*/true);
    canvas->Restore();
  }

 private:
  bool pending_ = false;
  float progress_ = -1.f;
  bool preview_ = false;
  gfx::ImageSkia image_;
};

BEGIN_METADATA(DownloadIconView)
END_METADATA

}  // namespace

// Zen's Downloads section (ZenLibraryDownloadsSection.mjs): every download,
// newest first, searchable and filtered by file type and date, with each
// row's actions (cancel, retry, show in folder, more) on hover.
class KoanLibraryDownloadsSection
    : public KoanLibrarySearchSection,
      public download::AllDownloadItemNotifier::Observer {
  METADATA_HEADER(KoanLibraryDownloadsSection, KoanLibrarySearchSection)

 public:
  explicit KoanLibraryDownloadsSection(KoanLibraryController* library)
      : KoanLibrarySearchSection(library,
                                 LibrarySection::kDownloads,
                                 u"Search Downloads",
                                 u"Filter Downloads…"),
        profile_(library->browser_view()->GetProfile()) {
    if (content::DownloadManager* manager = profile_->GetDownloadManager()) {
      notifier_ =
          std::make_unique<download::AllDownloadItemNotifier>(manager, this);
    }
    InitFilters();
  }
  ~KoanLibraryDownloadsSection() override = default;

  int GetRowCount() const { return static_cast<int>(rows_.size()); }

  // views::View:
  void AddedToWidget() override { UpdateProgress(); }

  // KoanLibrarySection:
  void OnShown() override {
    KoanLibrarySearchSection::OnShown();
    shown_ = true;
    Rebuild();
  }

  void FillTabMenu(KoanLibraryMenu* menu) override {
    menu->AddItem(u"Clear all downloads",
                  base::BindRepeating(&KoanLibraryDownloadsSection::ClearAll,
                                      weak_factory_.GetWeakPtr()));
    menu->AddSeparator();
    menu->AddItem(
        u"Open the Downloads page",
        base::BindRepeating(
            [](BrowserWindowInterface* browser) {
              browser->OpenURL(
                  content::OpenURLParams(
                      GURL("chrome://downloads/"), content::Referrer(),
                      WindowOpenDisposition::NEW_FOREGROUND_TAB,
                      ui::PAGE_TRANSITION_AUTO_BOOKMARK, false),
                  {});
            },
            library()->browser_view()->browser()));
  }

  // KoanLibrarySearchSection:
  std::vector<FilterGroup> GetFilterGroups() const override {
    return {
        FilterGroup("type", u"File type", /*exclusive=*/false,
                    {{"images", u"Images"},
                     {"video", u"Video"},
                     {"audio", u"Audio"},
                     {"documents", u"Documents"},
                     {"archives", u"Archives"},
                     {"apps", u"Apps"}}),
        WhenFilterGroup(u"When was it downloaded?"),
    };
  }

  void OnSearchChanged() override {
    limit_ = kPageSize;
    Rebuild();
  }

  void OnFiltersChanged() override {
    limit_ = kPageSize;
    Rebuild();
  }

  void OnScrolledToEnd() override {
    if (static_cast<int>(rows_.size()) < limit_) {
      return;
    }
    limit_ += kPageSize;
    ScheduleRebuild();
  }

  // download::AllDownloadItemNotifier::Observer:
  void OnManagerInitialized(content::DownloadManager* manager) override {
    ScheduleRebuild();
  }
  void OnManagerGoingDown(content::DownloadManager* manager) override {
    rows_.clear();
    notifier_.reset();
  }
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override {
    ScheduleRebuild();
    UpdateProgress();
  }
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override {
    // Nothing to show while the library is closed; it catches up when it
    // opens. A finished download changes its row's actions: rebuild then.
    // Progress only repaints.
    if (!library()->is_open()) {
      return;
    }
    auto it = rows_.find(item->GetGuid());
    if (it != rows_.end() && it->second.pending != IsPending(item)) {
      ScheduleRebuild();
    } else if (!update_timer_.IsRunning()) {
      update_timer_.Start(
          FROM_HERE, kUpdateDelay,
          base::BindOnce(&KoanLibraryDownloadsSection::UpdateRows,
                         base::Unretained(this)));
    }
  }
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override {
    // The item is about to go; nothing may keep it, the rows only its GUID.
    rows_.erase(item->GetGuid());
    if (opening_ == item->GetGuid()) {
      opening_.clear();
    }
    ScheduleRebuild();
    UpdateProgress();
  }

 private:
  struct Row {
    raw_ptr<KoanLibraryRow> row;
    raw_ptr<DownloadIconView> icon;
    bool pending = false;
  };

  using ItemAction =
      void (KoanLibraryDownloadsSection::*)(download::DownloadItem*);

  // A download by its GUID. Callbacks hold that, never the item, which can
  // be gone (removed, or cleared from the list) by the time they run.
  download::DownloadItem* Find(const std::string& guid) const {
    return notifier_ && notifier_->GetManager()
               ? notifier_->GetManager()->GetDownloadByGuid(guid)
               : nullptr;
  }

  // Runs `action` on the download with `guid`, if it is still there.
  void RunOn(const std::string& guid, ItemAction action) {
    if (download::DownloadItem* item = Find(guid)) {
      (this->*action)(item);
    }
  }

  base::RepeatingClosure BindAction(const std::string& guid,
                                    ItemAction action) {
    return base::BindRepeating(&KoanLibraryDownloadsSection::RunOn,
                               weak_factory_.GetWeakPtr(), guid, action);
  }

  std::vector<download::DownloadItem*> Downloads() const {
    std::vector<download::DownloadItem*> items;
    if (!notifier_ || !notifier_->GetManager()) {
      return items;
    }
    download::SimpleDownloadManager::DownloadVector all;
    notifier_->GetManager()->GetAllDownloads(&all);
    for (download::DownloadItem* item : all) {
      if (!item->IsTemporary() && !item->IsTransient()) {
        items.push_back(item);
      }
    }
    // Newest first.
    std::ranges::stable_sort(items, std::greater<>(),
                             &download::DownloadItem::GetStartTime);
    return items;
  }

  bool Matches(const download::DownloadItem* item) const {
    std::vector<std::string> types;
    const std::vector<FilterGroup> groups = GetFilterGroups();
    for (const FilterOption& option : groups[0].options) {
      if (IsFilterActive("type", option.id)) {
        types.push_back(option.id);
      }
    }
    if (!types.empty() &&
        !std::ranges::contains(types,
                               FileGroupOf(item->GetFileNameToReportUser()))) {
      return false;
    }
    if (std::optional<int> days = GetWhenDays()) {
      const base::Time end =
          item->IsDone() ? item->GetEndTime() : base::Time::Now();
      if (end < base::Time::Now() - base::Days(*days)) {
        return false;
      }
    }
    if (!query().empty()) {
      const std::u16string needle = base::i18n::ToLower(query());
      const std::u16string haystack = base::i18n::ToLower(base::StrCat(
          {FileNameOf(item), u" ", base::UTF8ToUTF16(item->GetURL().spec())}));
      if (haystack.find(needle) == std::u16string::npos) {
        return false;
      }
    }
    return true;
  }

  void ScheduleRebuild() {
    if (!shown_ || !library()->is_open() || rebuild_pending_) {
      return;
    }
    // Not synchronously: a rebuild destroys the rows, which may be in the
    // middle of handling the click that changed the download.
    rebuild_pending_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanLibraryDownloadsSection::Rebuild,
                                  weak_factory_.GetWeakPtr()));
  }

  void Rebuild() {
    rebuild_pending_ = false;
    icon_tracker_.TryCancelAll();
    preview_weak_factory_.InvalidateWeakPtrs();
    // Forget the rows before they are destroyed.
    rows_.clear();
    ClearList();
    std::vector<download::DownloadItem*> visible;
    for (download::DownloadItem* item : Downloads()) {
      if (Matches(item)) {
        visible.push_back(item);
        if (static_cast<int>(visible.size()) >= limit_) {
          break;
        }
      }
    }
    if (visible.empty()) {
      ShowEmpty(u"No downloads found");
    } else {
      views::View* group = AddGroup(std::u16string());
      for (download::DownloadItem* item : visible) {
        AddRow(group, item);
      }
    }
    UpdateProgress();
    InvalidateLayout();
  }

  void AddRow(views::View* group, download::DownloadItem* item) {
    const std::string& guid = item->GetGuid();
    auto row = std::make_unique<KoanLibraryRow>(
        BindAction(guid, &KoanLibraryDownloadsSection::OnRowPressed));
    row->SetTitle(FileNameOf(item));
    auto icon = std::make_unique<DownloadIconView>();
    DownloadIconView* icon_view = icon.get();
    row->SetIconView(std::move(icon));

    const auto add_button = [&](ItemAction action,
                                const gfx::VectorIcon& button_icon,
                                std::u16string tooltip) {
      row->AddAction(library::CreateIconButton(
          BindAction(guid, action), button_icon, std::move(tooltip),
          library::kRowButtonIconSize, library::kRowButtonPadding,
          library::kPillRadius));
    };
    if (IsPending(item)) {
      add_button(&KoanLibraryDownloadsSection::Cancel, vector_icons::kCloseIcon,
                 u"Cancel download");
    }
    if (CanRetry(item)) {
      add_button(&KoanLibraryDownloadsSection::Retry,
                 vector_icons::kReloadChromeRefreshOldIcon, u"Retry download");
    }
    if (HasFile(item)) {
      add_button(&KoanLibraryDownloadsSection::ShowInFolder, ShowInFolderIcon(),
                 kShowInFolder);
    }
    auto more = library::CreateIconButton(
        views::Button::PressedCallback(), views::kMoreHorizIcon,
        u"More options", library::kRowButtonIconSize,
        library::kRowButtonPadding, library::kPillRadius);
    views::Button* more_button = more.get();
    more->SetCallback(base::BindRepeating(
        &KoanLibraryDownloadsSection::ShowMenuBelow, base::Unretained(this),
        guid, base::Unretained(more_button)));
    row->AddAction(std::move(more));
    row->SetMenuCallback(
        base::BindRepeating(&KoanLibraryDownloadsSection::ShowMenuAt,
                            base::Unretained(this), guid));

    KoanLibraryRow* added = group->AddChildView(std::move(row));
    rows_[guid] = {added, icon_view, IsPending(item)};
    UpdateRow(item);
    LoadIcon(item);
  }

  // Status lines, rings and dimming of the rows, as the downloads progress.
  void UpdateRows() {
    for (const auto& [guid, row] : rows_) {
      if (download::DownloadItem* item = Find(guid)) {
        UpdateRow(item);
      }
    }
    UpdateProgress();
  }

  void UpdateRow(download::DownloadItem* item) {
    auto it = rows_.find(item->GetGuid());
    if (it == rows_.end()) {
      return;
    }
    KoanLibraryRow* row = it->second.row;
    const bool pending = IsPending(item);
    const bool opening = opening_ == item->GetGuid();
    std::u16string status;
    if (opening) {
      status = kOpeningInFolder;
    } else if (pending && item->GetOpenWhenComplete()) {
      status = u"Opens when finished";
    } else {
      status = StatusText(item);
    }
    // Hovered, the row shows where the file came from (not while it comes
    // in or opens).
    row->SetSubtitle(status, pending || opening ? std::u16string()
                                                : FormatUrl(item->GetURL()));
    row->SetDimmed(item->GetState() == download::DownloadItem::COMPLETE &&
                   item->GetFileExternallyRemoved());
    const int percent = item->PercentComplete();
    it->second.icon->SetPending(pending, percent >= 0 ? percent / 100.f : -1.f);
  }

  void LoadIcon(download::DownloadItem* item) {
    const base::FilePath path = item->GetTargetFilePath();
    if (path.empty() || IsPending(item)) {
      return;
    }
    if (HasFile(item) && CanPreview(path)) {
      auto preview = previews_.find(path);
      if (preview != previews_.end()) {
        SetIcon(item->GetGuid(), preview->second, /*preview=*/true);
        return;
      }
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
          base::BindOnce(&ReadPreviewFile, path),
          base::BindOnce(&KoanLibraryDownloadsSection::OnPreviewRead,
                         preview_weak_factory_.GetWeakPtr(), item->GetGuid(),
                         path));
    }
    IconManager* icons = g_browser_process->icon_manager();
    const ui::Compositor* compositor =
        GetWidget() ? GetWidget()->GetCompositor() : nullptr;
    const float scale = compositor ? compositor->device_scale_factor() : 1.f;
    if (gfx::Image* cached =
            icons->LookupIconFromFilepath(path, IconLoader::NORMAL, scale)) {
      SetIcon(item->GetGuid(), *cached->ToImageSkia(), /*preview=*/false);
      return;
    }
    icons->LoadIcon(path, IconLoader::NORMAL, scale,
                    base::BindOnce(
                        [](base::WeakPtr<KoanLibraryDownloadsSection> section,
                           const std::string& guid, gfx::Image image) {
                          if (section && !image.IsEmpty()) {
                            section->SetIcon(guid, *image.ToImageSkia(),
                                             /*preview=*/false);
                          }
                        },
                        preview_weak_factory_.GetWeakPtr(), item->GetGuid()),
                    &icon_tracker_);
  }

  void OnPreviewRead(const std::string& guid,
                     const base::FilePath& path,
                     std::optional<std::string> data) {
    if (!data || data->empty()) {
      return;
    }
    // Decoded out of process, like every picture from the web.
    data_decoder::DecodeImageIsolated(
        base::as_byte_span(*data), data_decoder::mojom::ImageCodec::kDefault,
        /*shrink_to_fit=*/true, kMaxPreviewBytes,
        gfx::Size(2 * kPreviewSize, 2 * kPreviewSize),
        base::BindOnce(&KoanLibraryDownloadsSection::OnPreviewDecoded,
                       preview_weak_factory_.GetWeakPtr(), guid, path));
  }

  void OnPreviewDecoded(const std::string& guid,
                        const base::FilePath& path,
                        const SkBitmap& bitmap) {
    if (bitmap.isNull()) {
      return;
    }
    // Cover the square, like object-fit: cover.
    const int side = std::min(bitmap.width(), bitmap.height());
    const gfx::ImageSkia image = gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
    const gfx::ImageSkia square = gfx::ImageSkiaOperations::ExtractSubset(
        image, gfx::Rect((bitmap.width() - side) / 2,
                         (bitmap.height() - side) / 2, side, side));
    previews_[path] = square;
    SetIcon(guid, square, /*preview=*/true);
  }

  void SetIcon(const std::string& guid,
               const gfx::ImageSkia& image,
               bool preview) {
    auto it = rows_.find(guid);
    if (it != rows_.end()) {
      it->second.icon->SetImage(image, preview);
    }
  }

  // The Downloads tab's ring: how far the running downloads have come.
  void UpdateProgress() {
    int64_t received = 0;
    int64_t total = 0;
    bool any = false;
    for (download::DownloadItem* item : Downloads()) {
      if (!IsPending(item) || item->IsPaused()) {
        continue;
      }
      any = true;
      if (item->GetTotalBytes() > 0) {
        received += item->GetReceivedBytes();
        total += item->GetTotalBytes();
      }
    }
    if (auto* panel = views::AsViewClass<KoanLibraryView>(parent())) {
      panel->SetDownloadProgress(
          !any ? -1.f
               : (total > 0 ? static_cast<float>(received) / total : 0.f));
    }
  }

  // A click on a download still coming in opens it once it's here; on a
  // finished one, shows it in the file manager (Zen's #onRowClick).
  void OnRowPressed(download::DownloadItem* item) {
    if (IsPending(item)) {
      item->SetOpenWhenComplete(!item->GetOpenWhenComplete());
      UpdateRow(item);
      return;
    }
    if (!HasFile(item)) {
      return;
    }
    item->ShowDownloadInShell();
    opening_ = item->GetGuid();
    UpdateRow(item);
    opening_timer_.Start(
        FROM_HERE, kOpeningFeedback,
        base::BindOnce(
            [](KoanLibraryDownloadsSection* section) {
              const std::string guid = std::exchange(section->opening_, {});
              if (download::DownloadItem* item = section->Find(guid)) {
                section->UpdateRow(item);
              }
            },
            base::Unretained(this)));
  }

  void Cancel(download::DownloadItem* item) { item->Cancel(true); }
  void ShowInFolder(download::DownloadItem* item) {
    item->ShowDownloadInShell();
  }
  void Open(download::DownloadItem* item) { item->OpenDownload(); }
  void RemoveItem(download::DownloadItem* item) { item->Remove(); }

  void Retry(download::DownloadItem* item) {
    if (item->CanResume()) {
      item->Resume(true);
      return;
    }
    // Downloaded afresh, like the downloads page's Retry.
    content::WebContents* contents = library()
                                         ->browser_view()
                                         ->browser()
                                         ->GetTabStripModel()
                                         ->GetActiveWebContents();
    if (!contents || !notifier_ || !notifier_->GetManager()) {
      return;
    }
    const net::NetworkTrafficAnnotationTag traffic_annotation =
        net::DefineNetworkTrafficAnnotation("koan_library_download_retry", R"(
        semantics {
          sender: "Koan Library"
          description: "Retrying a download."
          trigger:
            "The user selects Retry on a cancelled or failed download in "
            "the Library's Downloads."
          data: "None."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled by settings, but it's only "
            "triggered by user request."
          policy_exception_justification: "Not implemented."
        })");
    auto params = contents->GetPrimaryMainFrame()->CreateDownloadUrlParameters(
        item->GetURL(), traffic_annotation);
    params->set_content_initiated(true);
    params->set_initiator(item->GetRequestInitiator());
    params->set_referrer(item->GetReferrerUrl());
    params->set_download_source(download::DownloadSource::RETRY);
    notifier_->GetManager()->DownloadUrl(std::move(params));
  }

  void CopyFile(download::DownloadItem* item) {
    if (!HasFile(item)) {
      return;
    }
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteFilenames(ui::FileInfosToURIList(
        {ui::FileInfo(item->GetTargetFilePath(), base::FilePath())}));
  }

  void CopyLink(download::DownloadItem* item) {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(base::UTF8ToUTF16(item->GetURL().spec()));
  }

  void Delete(download::DownloadItem* item) {
    if (HasFile(item)) {
      item->DeleteFile(base::DoNothing());
    } else if (IsPending(item)) {
      item->Cancel(true);
    }
  }

  void ClearAll() {
    for (download::DownloadItem* item : Downloads()) {
      if (item->IsDone()) {
        item->Remove();
      }
    }
  }

  void ShowMenuBelow(const std::string& guid, views::Button* button) {
    ShowMenu(guid, button, button->GetBoundsInScreen(),
             views::MenuAnchorPosition::kTopRight,
             ui::mojom::MenuSourceType::kMouse);
  }

  void ShowMenuAt(const std::string& guid,
                  KoanLibraryRow* row,
                  const gfx::Point& point,
                  ui::mojom::MenuSourceType source_type) {
    ShowMenu(guid, row, gfx::Rect(point, gfx::Size()),
             views::MenuAnchorPosition::kTopLeft, source_type);
  }

  // Zen's downloads menu.
  void ShowMenu(const std::string& guid,
                views::View* source,
                const gfx::Rect& bounds,
                views::MenuAnchorPosition anchor,
                ui::mojom::MenuSourceType source_type) {
    download::DownloadItem* item = Find(guid);
    if (!item) {
      return;
    }
    const std::u16string name = FileNameOf(item);
    const bool has_file = HasFile(item);
    auto menu = std::make_unique<KoanLibraryMenu>();
    menu->AddItem(base::StrCat({u"Open ", name}),
                  BindAction(guid, &KoanLibraryDownloadsSection::Open),
                  has_file);
    menu->AddItem(kShowInFolder,
                  BindAction(guid, &KoanLibraryDownloadsSection::ShowInFolder),
                  has_file);
    menu->AddSeparator();
    menu->AddItem(base::StrCat({u"Copy ", name}),
                  BindAction(guid, &KoanLibraryDownloadsSection::CopyFile),
                  has_file);
    menu->AddItem(u"Copy download link",
                  BindAction(guid, &KoanLibraryDownloadsSection::CopyLink));
    menu->AddSeparator();
    menu->AddItem(u"Remove from history",
                  BindAction(guid, &KoanLibraryDownloadsSection::RemoveItem));
    menu->AddItem(u"Delete File",
                  BindAction(guid, &KoanLibraryDownloadsSection::Delete),
                  has_file || IsPending(item));
    if (auto row = rows_.find(guid); row != rows_.end()) {
      row->second.row->SetMenuOpen(true);
    }
    RunMenu(std::move(menu), source, bounds, anchor, source_type,
            base::BindRepeating(
                [](base::WeakPtr<KoanLibraryDownloadsSection> section,
                   const std::string& guid) {
                  if (!section) {
                    return;
                  }
                  if (auto it = section->rows_.find(guid);
                      it != section->rows_.end()) {
                    it->second.row->SetMenuOpen(false);
                  }
                },
                weak_factory_.GetWeakPtr(), guid));
  }

  const raw_ptr<Profile> profile_;
  std::unique_ptr<download::AllDownloadItemNotifier> notifier_;
  // The rows shown, by their download's GUID.
  std::map<std::string, Row> rows_;
  std::map<base::FilePath, gfx::ImageSkia> previews_;
  int limit_ = kPageSize;
  bool shown_ = false;
  bool rebuild_pending_ = false;
  // The download just shown in the file manager, for a moment.
  std::string opening_;
  base::OneShotTimer opening_timer_;
  base::OneShotTimer update_timer_;
  base::CancelableTaskTracker icon_tracker_;
  base::WeakPtrFactory<KoanLibraryDownloadsSection> preview_weak_factory_{this};
  base::WeakPtrFactory<KoanLibraryDownloadsSection> weak_factory_{this};
};

BEGIN_METADATA(KoanLibraryDownloadsSection)
ADD_READONLY_PROPERTY_METADATA(int, RowCount)
END_METADATA

std::unique_ptr<KoanLibrarySection> CreateLibraryDownloadsSection(
    KoanLibraryController* library) {
  return std::make_unique<KoanLibraryDownloadsSection>(library);
}

}  // namespace koan
