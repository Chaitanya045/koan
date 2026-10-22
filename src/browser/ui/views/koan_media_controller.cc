// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_media_controller.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "base/containers/adapters.h"
#include "base/containers/flat_set.h"
#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/cpp/media_metadata.h"
#include "services/media_session/public/cpp/media_position.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace koan {

namespace {

using Action = media_session::mojom::MediaSessionAction;

// ZenMediaController.mjs / zen-media-controls.css (sizes in
// koan_sidebar_metrics.h). At most three cards stack; two peek out above the
// front one, each a little narrower and fainter. Expanded (on hover) they
// sit in a column; each card's shadow margin makes Zen's 6px gap.
constexpr size_t kMaxStackedCards = 3;
constexpr int kMaxPeekLevels = 2;
constexpr float kPeekScaleStep = 0.04f;
constexpr float kPeekOpacityStep = 0.3f;
constexpr auto kCardPadding =
    gfx::Insets::VH(kMediaCardPaddingVertical, kMediaCardPaddingHorizontal);

constexpr base::TimeDelta kExpandDuration = base::Milliseconds(300);
constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(300);
constexpr base::TimeDelta kStillPlayingDelay = base::Seconds(1);
constexpr base::TimeDelta kTabSwitchDelay = base::Milliseconds(500);
constexpr base::TimeDelta kPositionInterval = base::Seconds(1);
// Media this long is a live stream: no progress (Zen's 900000 s).
constexpr base::TimeDelta kLiveDuration = base::Seconds(900000);

// Zen's formatSecondsToTime: m:ss, or h:mm:ss.
std::u16string FormatTime(base::TimeDelta time) {
  const int64_t total =
      std::max<int64_t>(0, static_cast<int64_t>(std::ceil(time.InSecondsF())));
  const int64_t hours = total / 3600;
  const int64_t minutes = total % 3600 / 60;
  const int64_t seconds = total % 60;
  const auto two_digits = [](int64_t value) {
    return (value < 10 ? u"0" : u"") + base::NumberToString16(value);
  };
  if (hours > 0) {
    return base::StrCat({base::NumberToString16(hours), u":",
                         two_digits(minutes), u":", two_digits(seconds)});
  }
  return base::StrCat(
      {base::NumberToString16(minutes), u":", two_digits(seconds)});
}

// Sets the button's icon (as colour-ID image models, so it follows the
// theme without being reset to another icon) and its name.
void SetButtonIcon(views::ImageButton* button,
                   const gfx::VectorIcon& icon,
                   const std::u16string& name) {
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, ui::kColorIcon, kMediaIconSize));
  button->SetImageModel(views::Button::STATE_DISABLED,
                        ui::ImageModel::FromVectorIcon(
                            icon, ui::kColorIconDisabled, kMediaIconSize));
  button->SetTooltipText(name);
  button->GetViewAccessibility().SetName(name);
}

std::unique_ptr<views::ImageButton> MakeButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    const std::u16string& name) {
  auto button = views::CreateVectorImageButton(std::move(callback));
  SetButtonIcon(button.get(), icon, name);
  button->SetPreferredSize(gfx::Size(kMediaButtonSize, kMediaButtonSize));
  views::InstallRoundRectHighlightPathGenerator(button.get(), gfx::Insets(),
                                                kMediaButtonCornerRadius);
  return button;
}

}  // namespace

// One card's media: the tab's media session, observed.
class KoanMediaCard : public media_session::mojom::MediaSessionObserver {
 public:
  KoanMediaCard(KoanMediaController* controller, content::WebContents* contents)
      : controller_(controller), contents_(contents) {
    content::MediaSession::Get(contents)->AddObserver(
        receiver_.BindNewPipeAndPassRemote());
  }
  KoanMediaCard(const KoanMediaCard&) = delete;
  KoanMediaCard& operator=(const KoanMediaCard&) = delete;
  ~KoanMediaCard() override = default;

  content::WebContents* contents() const { return contents_; }
  const std::u16string& title() const { return title_; }
  const std::u16string& artist() const { return artist_; }
  bool playing() const { return playing_; }
  bool in_picture_in_picture() const { return in_picture_in_picture_; }
  const std::optional<media_session::MediaPosition>& position() const {
    return position_;
  }
  bool Supports(Action action) const { return actions_.contains(action); }
  bool HasProgress() const {
    return position_ && position_->duration().is_positive() &&
           position_->duration() < kLiveDuration;
  }

  // In the stack.
  bool shown() const { return shown_; }
  bool ever_shown() const { return ever_shown_; }
  void set_shown(bool shown) {
    shown_ = shown;
    ever_shown_ |= shown;
  }

  void Focus() { controller_->FocusTab(contents_); }
  void TogglePlayPause() {
    content::MediaSession* session = content::MediaSession::Get(contents_);
    if (playing_) {
      session->Suspend(content::MediaSession::SuspendType::kUI);
    } else {
      session->Resume(content::MediaSession::SuspendType::kUI);
    }
  }
  void PreviousTrack() {
    if (Supports(Action::kPreviousTrack)) {
      content::MediaSession::Get(contents_)->PreviousTrack();
    }
  }
  void NextTrack() {
    if (Supports(Action::kNextTrack)) {
      content::MediaSession::Get(contents_)->NextTrack();
    }
  }
  void SeekTo(double fraction) {
    if (Supports(Action::kSeekTo) && HasProgress()) {
      content::MediaSession::Get(contents_)->SeekTo(position_->duration() *
                                                    fraction);
      content::MediaSession::Get(contents_)->Resume(
          content::MediaSession::SuspendType::kUI);
    }
  }
  void EnterPictureInPicture() {
    content::MediaSession::Get(contents_)->EnterPictureInPicture();
  }
  void ToggleMute() {
    SetTabAudioMuted(contents_, !contents_->IsAudioMuted(),
                     TabMutedReason::kAudioIndicator, std::string());
    controller_->OnCardChanged(this);
  }
  // Zen's close: pause, and drop the card.
  void Dismiss() {
    if (playing_) {
      content::MediaSession::Get(contents_)->Suspend(
          content::MediaSession::SuspendType::kUI);
    }
    controller_->RemoveCardSoon(this);
  }

  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr info) override {
    if (!info) {
      return;
    }
    if (info->state ==
        media_session::mojom::MediaSessionInfo::SessionState::kInactive) {
      // Chromium takes a muted tab's media out of its session; Zen keeps the
      // card, muted, and so does Koan (unmuting brings the session back).
      if (!contents_->IsAudioMuted()) {
        controller_->RemoveCardSoon(this);
      }
      return;
    }
    playing_ = info->playback_state ==
               media_session::mojom::MediaPlaybackState::kPlaying;
    in_picture_in_picture_ =
        info->picture_in_picture_state ==
        media_session::mojom::MediaPictureInPictureState::kInPictureInPicture;
    controller_->OnCardChanged(this);
  }
  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>& metadata) override {
    title_ = metadata ? metadata->title : std::u16string();
    artist_ = metadata ? metadata->artist : std::u16string();
    controller_->OnCardChanged(this);
  }
  void MediaSessionActionsChanged(const std::vector<Action>& actions) override {
    actions_ = base::flat_set<Action>(actions.begin(), actions.end());
    controller_->OnCardChanged(this);
  }
  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override {}
  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>& position) override {
    position_ = position;
    controller_->OnCardChanged(this);
  }

 private:
  const raw_ptr<KoanMediaController> controller_;
  const raw_ptr<content::WebContents> contents_;
  std::u16string title_;
  std::u16string artist_;
  bool playing_ = false;
  bool in_picture_in_picture_ = false;
  base::flat_set<Action> actions_;
  std::optional<media_session::MediaPosition> position_;
  bool shown_ = false;
  bool ever_shown_ = false;
  mojo::Receiver<media_session::mojom::MediaSessionObserver> receiver_{this};
};

namespace {

// The thin seek bar: a 4 DIP track with the played part in the accent
// colour and, on hover, a thumb.
class KoanMediaProgressBar : public views::View {
  METADATA_HEADER(KoanMediaProgressBar, views::View)

 public:
  // `on_seek` gets the fraction while dragging (done == false) and once
  // the button is released (done == true).
  using SeekCallback = base::RepeatingCallback<void(double, bool)>;

  explicit KoanMediaProgressBar(SeekCallback on_seek)
      : on_seek_(std::move(on_seek)) {}

  void SetValue(double value) {
    if (!dragging_ && value != value_) {
      value_ = std::clamp(value, 0.0, 1.0);
      SchedulePaint();
    }
  }
  void set_seekable(bool seekable) { seekable_ = seekable; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kMediaProgressBarHeight * 3);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const float y = (height() - kMediaProgressBarHeight) / 2.0f;
    const gfx::RectF track(0, y, width(), kMediaProgressBarHeight);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorKoanMediaProgressTrack));
    canvas->DrawRoundRect(track, kMediaProgressBarHeight / 2.0f, flags);
    gfx::RectF played = track;
    played.set_width(track.width() * value_);
    flags.setColor(colors->GetColor(kColorKoanMediaProgress));
    canvas->DrawRoundRect(played, kMediaProgressBarHeight / 2.0f, flags);
    if (seekable_ && (IsMouseHovered() || dragging_)) {
      canvas->DrawCircle(gfx::PointF(played.right(), track.CenterPoint().y()),
                         kMediaProgressBarHeight, flags);
    }
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!seekable_ || !event.IsOnlyLeftMouseButton()) {
      return false;
    }
    dragging_ = true;
    Seek(event, /*done=*/false);
    return true;
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    Seek(event, /*done=*/false);
    return true;
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (dragging_) {
      Seek(event, /*done=*/true);
      dragging_ = false;
    }
  }
  void OnMouseCaptureLost() override { dragging_ = false; }
  void OnMouseEntered(const ui::MouseEvent& event) override { SchedulePaint(); }
  void OnMouseExited(const ui::MouseEvent& event) override { SchedulePaint(); }

 private:
  void Seek(const ui::MouseEvent& event, bool done) {
    value_ = width() > 0 ? std::clamp(event.x() / static_cast<double>(width()),
                                      0.0, 1.0)
                         : 0;
    SchedulePaint();
    on_seek_.Run(value_, done);
  }

  const SeekCallback on_seek_;
  double value_ = 0;
  bool seekable_ = false;
  bool dragging_ = false;
};

BEGIN_METADATA(KoanMediaProgressBar)
END_METADATA

}  // namespace

// One card. Its bottom row (site icon, previous / play-pause / next, mute)
// always shows; the rows above it (title and artist with picture-in-picture
// and close; the progress) are revealed as the stack expands, since the
// card's contents are laid out from its bottom edge and clipped to it.
class KoanMediaCardView : public views::View {
  METADATA_HEADER(KoanMediaCardView, views::View)

 public:
  explicit KoanMediaCardView(KoanMediaCard* card) : card_(card) {
    favicon_ = AddChildView(std::make_unique<views::ImageButton>(
        base::BindRepeating(&KoanMediaCard::Focus, base::Unretained(card))));
    favicon_->SetPreferredSize(gfx::Size(kMediaButtonSize, kMediaButtonSize));
    favicon_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    favicon_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    favicon_->SetTooltipText(u"Go to tab");
    favicon_->GetViewAccessibility().SetName(u"Go to tab");
    previous_ = AddChildView(
        MakeButton(base::BindRepeating(&KoanMediaCard::PreviousTrack,
                                       base::Unretained(card)),
                   vector_icons::kSkipPreviousIcon, u"Previous track"));
    play_pause_ = AddChildView(
        MakeButton(base::BindRepeating(&KoanMediaCard::TogglePlayPause,
                                       base::Unretained(card)),
                   vector_icons::kPlayArrowIcon, u"Play"));
    next_ = AddChildView(MakeButton(
        base::BindRepeating(&KoanMediaCard::NextTrack, base::Unretained(card)),
        vector_icons::kSkipNextIcon, u"Next track"));
    mute_ = AddChildView(MakeButton(
        base::BindRepeating(&KoanMediaCard::ToggleMute, base::Unretained(card)),
        vector_icons::kVolumeUpIcon, u"Mute"));

    title_ = AddChildView(std::make_unique<views::Label>());
    title_->SetFontList(
        title_->font_list().DeriveWithWeight(gfx::Font::Weight::MEDIUM));
    artist_ = AddChildView(std::make_unique<views::Label>());
    artist_->SetFontList(artist_->font_list().DeriveWithSizeDelta(-1));
    artist_->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    for (views::Label* label : {title_.get(), artist_.get()}) {
      label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      label->SetElideBehavior(gfx::FADE_TAIL);
      label->SetAutoColorReadabilityEnabled(false);
      // The card is a layer that isn't opaque.
      label->SetSubpixelRenderingEnabled(false);
    }
    pip_ = AddChildView(MakeButton(
        base::BindRepeating(&KoanMediaCard::EnterPictureInPicture,
                            base::Unretained(card)),
        vector_icons::kPictureInPictureAltIcon, u"Picture-in-picture"));
    close_ = AddChildView(MakeButton(
        base::BindRepeating(&KoanMediaCard::Dismiss, base::Unretained(card)),
        vector_icons::kCloseIcon, u"Close"));

    current_time_ = AddChildView(std::make_unique<views::Label>());
    duration_ = AddChildView(std::make_unique<views::Label>());
    for (views::Label* label : {current_time_.get(), duration_.get()}) {
      label->SetFontList(label->font_list().DeriveWithSizeDelta(-3));
      label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
      label->SetAutoColorReadabilityEnabled(false);
      label->SetSubpixelRenderingEnabled(false);
    }
    progress_ =
        AddChildView(std::make_unique<KoanMediaProgressBar>(base::BindRepeating(
            &KoanMediaCardView::OnSeek, base::Unretained(this))));
    Refresh();
  }

  KoanMediaCard* card() const { return card_; }

  // Whether the stack shows this card's title and progress (on hover).
  void SetExpanded(bool expanded) {
    if (expanded_ != expanded) {
      expanded_ = expanded;
      Refresh();
    }
  }

  int CollapsedHeight() const {
    return 2 * kMediaCardShadowMargin + kCardPadding.height() +
           kMediaButtonSize;
  }
  int ExpandedHeight() const {
    return CollapsedHeight() + InfoHeight() +
           (card_->HasProgress() ? kMediaProgressRowHeight : 0);
  }

  void Refresh() {
    content::WebContents* contents = card_->contents();
    ui::ImageModel icon;
    if (tabs::TabInterface* tab =
            tabs::TabInterface::MaybeGetFromContents(contents)) {
      if (TabUIHelper* helper = TabUIHelper::From(tab)) {
        icon = helper->GetFavicon();
      }
    }
    if (icon.IsEmpty()) {
      icon = ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                            ui::kColorIcon, gfx::kFaviconSize);
    }
    favicon_->SetImageModel(views::Button::STATE_NORMAL, icon);

    title_->SetText(card_->title().empty() ? contents->GetTitle()
                                           : card_->title());
    artist_->SetText(card_->artist());
    artist_->SetVisible(!card_->artist().empty());

    SetButtonIcon(play_pause_,
                  card_->playing() ? vector_icons::kPauseIcon
                                   : vector_icons::kPlayArrowIcon,
                  card_->playing() ? u"Pause" : u"Play");
    previous_->SetEnabled(card_->Supports(Action::kPreviousTrack));
    next_->SetEnabled(card_->Supports(Action::kNextTrack));
    const bool muted = contents->IsAudioMuted();
    SetButtonIcon(
        mute_,
        muted ? vector_icons::kVolumeOffIcon : vector_icons::kVolumeUpIcon,
        muted ? u"Unmute" : u"Mute");
    pip_->SetVisible(card_->Supports(Action::kEnterPictureInPicture));

    const bool progress = card_->HasProgress();
    for (views::View* view : {static_cast<views::View*>(current_time_.get()),
                              static_cast<views::View*>(duration_.get()),
                              static_cast<views::View*>(progress_.get())}) {
      view->SetVisible(progress);
    }
    progress_->set_seekable(card_->Supports(Action::kSeekTo));
    UpdatePosition();
    // The time only shows while the stack is expanded: no timer otherwise.
    if (expanded_ && progress && card_->playing()) {
      if (!position_timer_.IsRunning()) {
        position_timer_.Start(FROM_HERE, kPositionInterval, this,
                              &KoanMediaCardView::UpdatePosition);
      }
    } else {
      position_timer_.Stop();
    }
    InvalidateLayout();
  }

  // views::View:
  void Layout(PassKey) override {
    gfx::Rect inner = GetLocalBounds();
    inner.Inset(kMediaCardShadowMargin);
    inner.Inset(kCardPadding);

    // The bottom row: the site's icon, the playback buttons (spread over at
    // most 10em, centred), mute.
    int y = inner.bottom() - kMediaButtonSize;
    favicon_->SetBounds(inner.x(), y, kMediaButtonSize, kMediaButtonSize);
    mute_->SetBounds(inner.right() - kMediaButtonSize, y, kMediaButtonSize,
                     kMediaButtonSize);
    const int room =
        std::max(3 * kMediaButtonSize, mute_->x() - favicon_->bounds().right() -
                                           2 * kMediaControlsGap);
    const int group = std::min(room, kMediaPlaybackMaxWidth);
    const int group_x =
        favicon_->bounds().right() + kMediaControlsGap + (room - group) / 2;
    previous_->SetBounds(group_x, y, kMediaButtonSize, kMediaButtonSize);
    play_pause_->SetBounds(group_x + (group - kMediaButtonSize) / 2, y,
                           kMediaButtonSize, kMediaButtonSize);
    next_->SetBounds(group_x + group - kMediaButtonSize, y, kMediaButtonSize,
                     kMediaButtonSize);

    // Above it, the progress: current time, the bar, the duration.
    if (progress_->GetVisible()) {
      y -= kMediaProgressRowHeight;
      const gfx::Size current = current_time_->GetPreferredSize();
      const gfx::Size duration = duration_->GetPreferredSize();
      const int center = y + kMediaProgressRowHeight / 2;
      current_time_->SetBounds(inner.x(), center - current.height() / 2,
                               current.width(), current.height());
      duration_->SetBounds(inner.right() - duration.width(),
                           center - duration.height() / 2, duration.width(),
                           duration.height());
      const int bar_x = current_time_->bounds().right() + kMediaProgressMargin;
      const int bar_height = progress_->GetPreferredSize().height();
      progress_->SetBounds(
          bar_x, center - bar_height / 2,
          std::max(0, duration_->x() - kMediaProgressMargin - bar_x),
          bar_height);
    }

    // Above that, the title and artist, with picture-in-picture and close.
    y -= InfoHeight();
    int buttons_x = inner.right() - kMediaButtonSize;
    close_->SetBounds(buttons_x, y, kMediaButtonSize, kMediaButtonSize);
    if (pip_->GetVisible()) {
      buttons_x -= kMediaButtonSize;
      pip_->SetBounds(buttons_x, y, kMediaButtonSize, kMediaButtonSize);
    }
    const int text_width = std::max(0, buttons_x - inner.x());
    title_->SetBounds(inner.x(), y, text_width, kMediaTitleHeight);
    artist_->SetBounds(inner.x(), y + kMediaTitleHeight, text_width,
                       kMediaArtistHeight);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // zen-theme.css: --zen-sidebar-notification-bg, -shadow and -outline.
    const ui::ColorProvider* colors = GetColorProvider();
    gfx::RectF card(GetLocalBounds());
    card.Inset(kMediaCardShadowMargin);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorKoanMediaCard));
    flags.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(gfx::Vector2d(), kMediaCardShadowBlur,
                          colors->GetColor(kColorKoanMediaCardShadow))}));
    canvas->DrawRoundRect(card, kMediaCardCornerRadius, flags);
    flags.setLooper(nullptr);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(colors->GetColor(kColorKoanMediaCardOutline));
    card.Inset(0.5f);
    canvas->DrawRoundRect(card, kMediaCardCornerRadius - 0.5f, flags);
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    // Clicking the card itself (not a button) goes to its tab.
    return event.IsOnlyLeftMouseButton();
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (HitTestPoint(event.location())) {
      card_->Focus();
    }
  }

 private:
  int InfoHeight() const {
    return kMediaTitleHeight +
           (card_->artist().empty() ? 0 : kMediaArtistHeight) + kMediaInfoGap;
  }

  void UpdatePosition() {
    if (!card_->HasProgress()) {
      return;
    }
    const base::TimeDelta duration = card_->position()->duration();
    const base::TimeDelta position = std::clamp(
        card_->position()->GetPosition(), base::TimeDelta(), duration);
    current_time_->SetText(FormatTime(position));
    duration_->SetText(FormatTime(duration));
    progress_->SetValue(position / duration);
  }

  void OnSeek(double fraction, bool done) {
    if (!card_->HasProgress()) {
      return;
    }
    current_time_->SetText(
        FormatTime(card_->position()->duration() * fraction));
    if (done) {
      card_->SeekTo(fraction);
    }
  }

  const raw_ptr<KoanMediaCard> card_;
  raw_ptr<views::ImageButton> favicon_;
  raw_ptr<views::ImageButton> previous_;
  raw_ptr<views::ImageButton> play_pause_;
  raw_ptr<views::ImageButton> next_;
  raw_ptr<views::ImageButton> mute_;
  raw_ptr<views::Label> title_;
  raw_ptr<views::Label> artist_;
  raw_ptr<views::ImageButton> pip_;
  raw_ptr<views::ImageButton> close_;
  raw_ptr<views::Label> current_time_;
  raw_ptr<views::Label> duration_;
  raw_ptr<KoanMediaProgressBar> progress_;
  bool expanded_ = false;
  base::RepeatingTimer position_timer_;
};

BEGIN_METADATA(KoanMediaCardView)
END_METADATA

// The stack of cards at the bottom of the sidebar. Collapsed, the newest
// card is in front and up to two more peek out above it; on hover the stack
// expands into a column of full cards, growing upwards.
class KoanMediaControlsView : public views::View,
                              public gfx::AnimationDelegate {
  METADATA_HEADER(KoanMediaControlsView, views::View)

 public:
  explicit KoanMediaControlsView(KoanMediaController* controller)
      : controller_(controller) {
    SetNotifyEnterExitOnChild(true);
    expand_.SetSlideDuration(kExpandDuration);
    expand_.SetTweenType(gfx::Tween::EASE_OUT_3);
  }

  // Matches the card views to the cards to show.
  void Update() {
    const std::vector<KoanMediaCard*> cards = controller_->GetShownCards();
    for (KoanMediaCardView* view : std::vector(card_views_)) {
      if (!std::ranges::contains(cards, view->card())) {
        std::erase(card_views_, view);
        RemoveChildViewT(view);
      }
    }
    std::vector<KoanMediaCardView*> views;
    for (KoanMediaCard* card : cards) {
      KoanMediaCardView* view = FindView(card);
      if (!view) {
        view = AddChildView(std::make_unique<KoanMediaCardView>(card));
        view->SetExpanded(expanded_);
        view->SetPaintToLayer();
        view->layer()->SetFillsBoundsOpaquely(false);
        // Zen's cards fade in (@starting-style).
        view->layer()->SetOpacity(0);
        views::AnimationBuilder()
            .Once()
            .SetDuration(kFadeInDuration)
            .SetOpacity(view, 1, gfx::Tween::EASE_OUT);
      } else {
        view->Refresh();
      }
      views.push_back(view);
    }
    card_views_ = std::move(views);
    // Paint back to front.
    for (size_t i = 0; i < card_views_.size(); ++i) {
      ReorderChildView(card_views_[card_views_.size() - 1 - i], i);
    }
    PreferredSizeChanged();
  }

  void RefreshCard(KoanMediaCard* card) {
    if (KoanMediaCardView* view = FindView(card)) {
      view->Refresh();
      PreferredSizeChanged();
    }
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, StackHeight(expand_.GetCurrentValue()));
  }

  void Layout(PassKey) override {
    const double expansion = expand_.GetCurrentValue();
    // The card's shadow margin reaches into the sidebar's own inset.
    const int side = std::max(0, SidebarStripInset() - kMediaCardShadowMargin);
    int expanded_bottom = 0;
    for (size_t i = 0; i < card_views_.size(); ++i) {
      KoanMediaCardView* view = card_views_[i];
      const int level = std::min<int>(i, kMaxPeekLevels);
      const int collapsed_height = card_views_.front()->CollapsedHeight();
      const int height = gfx::Tween::IntValueBetween(
          expansion, collapsed_height, view->ExpandedHeight());
      // Distance from the stack's bottom edge to the card's.
      const int bottom = gfx::Tween::IntValueBetween(
          expansion, kMediaStackPeek * level, expanded_bottom);
      // In line with the tab rows; cards behind are narrower.
      const int inset =
          side + gfx::Tween::IntValueBetween(
                     expansion,
                     static_cast<int>(width() * kPeekScaleStep * level / 2), 0);
      view->SetBounds(inset, this->height() - bottom - height,
                      width() - 2 * inset, height);
      if (!view->layer()->GetAnimator()->is_animating()) {
        view->layer()->SetOpacity(gfx::Tween::FloatValueBetween(
            expansion, 1 - kPeekOpacityStep * level, 1));
      }
      expanded_bottom += view->ExpandedHeight();
    }
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    SetExpanded(true);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    SetExpanded(false);
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    PreferredSizeChanged();
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    PreferredSizeChanged();
  }

 private:
  KoanMediaCardView* FindView(KoanMediaCard* card) const {
    for (KoanMediaCardView* view : card_views_) {
      if (view->card() == card) {
        return view;
      }
    }
    return nullptr;
  }

  int StackHeight(double expansion) const {
    if (card_views_.empty()) {
      return 0;
    }
    const int collapsed =
        card_views_.front()->CollapsedHeight() +
        kMediaStackPeek * std::min<int>(card_views_.size() - 1, kMaxPeekLevels);
    int expanded = 0;
    for (const KoanMediaCardView* view : card_views_) {
      expanded += view->ExpandedHeight();
    }
    return gfx::Tween::IntValueBetween(expansion, collapsed, expanded);
  }

  const raw_ptr<KoanMediaController> controller_;
  // Newest (front) first.
  std::vector<KoanMediaCardView*> card_views_;
  void SetExpanded(bool expanded) {
    expanded_ = expanded;
    for (KoanMediaCardView* view : card_views_) {
      view->SetExpanded(expanded);
    }
    if (expanded) {
      expand_.Show();
    } else {
      expand_.Hide();
    }
  }

  bool expanded_ = false;
  gfx::SlideAnimation expand_{this};
};

BEGIN_METADATA(KoanMediaControlsView)
END_METADATA

// Tells the controller when a tab's sound starts or stops.
class KoanMediaController::TabWatcher : public content::WebContentsObserver {
 public:
  TabWatcher(KoanMediaController* controller, content::WebContents* contents)
      : content::WebContentsObserver(contents), controller_(controller) {}

  // content::WebContentsObserver:
  void OnAudioStateChanged(bool audible) override {
    controller_->OnAudibleChanged(web_contents(), audible);
  }
  void DidUpdateAudioMutingState(bool muted) override {
    if (KoanMediaCard* card = controller_->CardFor(web_contents())) {
      controller_->OnCardChanged(card);
    }
  }
  void MediaPictureInPictureChanged(bool is_picture_in_picture) override {
    if (KoanMediaCard* card = controller_->CardFor(web_contents())) {
      controller_->OnCardChanged(card);
    }
  }

 private:
  const raw_ptr<KoanMediaController> controller_;
};

KoanMediaController::KoanMediaController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  model->AddObserver(this);
  for (int i = 0; i < model->count(); ++i) {
    Watch(model->GetWebContentsAt(i));
  }
}

KoanMediaController::~KoanMediaController() = default;

void KoanMediaController::OnAddedToWidget() {
  InstallView();
}

void KoanMediaController::OnBrowserViewLayout() {
  // The tab strip may have been rebuilt (e.g. a tab strip mode change).
  if (!view() && !cards_.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanMediaController::InstallView,
                                  weak_factory_.GetWeakPtr()));
  }
}

void KoanMediaController::OnBrowserViewDestroying() {
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  tab_switch_timer_.Stop();
  sidebar_observation_.Reset();
  if (views::View* view = view_.view()) {
    view_.SetView(nullptr);
    view->parent()->RemoveChildViewT(view);
  }
  cards_.clear();
  watchers_.clear();
}

bool KoanMediaController::IsEnabled() const {
  return features::IsMediaControlsEnabled() &&
         browser_view_->GetProfile()->GetPrefs()->GetBoolean(
             prefs::kMediaControlsEnabled);
}

void KoanMediaController::Watch(content::WebContents* contents) {
  if (watchers_.contains(contents)) {
    return;
  }
  watchers_[contents] = std::make_unique<TabWatcher>(this, contents);
  // A tab moved in from another window may be playing already.
  if (contents->IsCurrentlyAudible()) {
    OnAudibleChanged(contents, true);
  }
}

void KoanMediaController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted) {
    for (const auto& contents : change.GetInsert()->contents) {
      Watch(contents.contents);
    }
  } else if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& contents : change.GetRemove()->contents) {
      if (KoanMediaCard* card = CardFor(contents.contents)) {
        RemoveCard(card);
      }
      watchers_.erase(contents.contents);
    }
  } else if (change.type() == TabStripModelChange::kReplaced) {
    const auto* replace = change.GetReplace();
    if (KoanMediaCard* card = CardFor(replace->old_contents)) {
      RemoveCard(card);
    }
    watchers_.erase(replace->old_contents);
    Watch(replace->new_contents);
  }
  if (selection.active_tab_changed()) {
    OnActiveTabChanged();
  }
}

void KoanMediaController::OnViewBoundsChanged(views::View* observed_view) {
  UpdateVisibility();
}

void KoanMediaController::OnViewIsDeleting(views::View* observed_view) {
  sidebar_observation_.Reset();
}

void KoanMediaController::OnAudibleChanged(content::WebContents* contents,
                                           bool audible) {
  if (KoanMediaCard* card = CardFor(contents)) {
    OnCardChanged(card);
  }
  if (!audible || !IsEnabled()) {
    return;
  }
  // Only media sessions (not e.g. Web Audio) get a card, one per tab.
  if (!CardFor(contents)) {
    content::MediaSession* session = content::MediaSession::Get(contents);
    if (session->GetMediaSessionInfoSync()->state ==
        media_session::mojom::MediaSessionInfo::SessionState::kInactive) {
      return;
    }
    cards_.push_back(std::make_unique<KoanMediaCard>(this, contents));
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&KoanMediaController::CheckStillPlaying,
                     weak_factory_.GetWeakPtr(), contents),
      kStillPlayingDelay);
}

void KoanMediaController::CheckStillPlaying(content::WebContents* contents) {
  // `contents` is only compared: the card is gone if the tab closed.
  KoanMediaCard* card = CardFor(contents);
  if (!card) {
    return;
  }
  if (card->playing()) {
    SetCardShown(card, !card->in_picture_in_picture() &&
                           contents != browser_view_->browser()
                                           ->GetTabStripModel()
                                           ->GetActiveWebContents());
  } else if (!card->ever_shown()) {
    // Over before it was ever shown (a notification ping): no card.
    RemoveCard(card);
  }
}

void KoanMediaController::OnActiveTabChanged() {
  // The selected tab's card hides at once; the others come back after a
  // moment.
  content::WebContents* active =
      browser_view_->browser()->GetTabStripModel()->GetActiveWebContents();
  bool reveal = false;
  for (const auto& card : cards_) {
    if (card->contents() == active) {
      SetCardShown(card.get(), false);
    } else if (!card->shown()) {
      reveal = true;
    }
  }
  if (reveal) {
    tab_switch_timer_.Start(FROM_HERE, kTabSwitchDelay, this,
                            &KoanMediaController::RevealAfterTabSwitch);
  }
}

void KoanMediaController::RevealAfterTabSwitch() {
  content::WebContents* active =
      browser_view_->browser()->GetTabStripModel()->GetActiveWebContents();
  for (const auto& card : cards_) {
    if (!card->shown() && card->contents() != active &&
        !card->in_picture_in_picture()) {
      SetCardShown(card.get(), true);
    }
  }
}

void KoanMediaController::OnCardChanged(KoanMediaCard* card) {
  // Picture-in-picture hides the card, leaving it shows it again.
  if (card->ever_shown()) {
    const bool shown = !card->in_picture_in_picture() &&
                       card->contents() != browser_view_->browser()
                                               ->GetTabStripModel()
                                               ->GetActiveWebContents();
    if (shown != card->shown()) {
      SetCardShown(card, shown);
      return;
    }
  }
  if (KoanMediaControlsView* controls = view(); controls && card->shown()) {
    controls->RefreshCard(card);
  }
}

void KoanMediaController::SetCardShown(KoanMediaCard* card, bool shown) {
  if (card->shown() == shown) {
    return;
  }
  card->set_shown(shown);
  UpdateView();
}

void KoanMediaController::RemoveCardSoon(KoanMediaCard* card) {
  // Called from the card's own callbacks and buttons.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanMediaController> self,
                        content::WebContents* web_contents) {
                       if (!self) {
                         return;
                       }
                       if (KoanMediaCard* found = self->CardFor(web_contents)) {
                         self->RemoveCard(found);
                       }
                     },
                     weak_factory_.GetWeakPtr(), card->contents()));
}

void KoanMediaController::RemoveCard(KoanMediaCard* card) {
  auto it =
      std::ranges::find(cards_, card, &std::unique_ptr<KoanMediaCard>::get);
  if (it == cards_.end()) {
    return;
  }
  // The card view goes first: it points at the card.
  std::unique_ptr<KoanMediaCard> removed = std::move(*it);
  cards_.erase(it);
  UpdateView();
}

KoanMediaCard* KoanMediaController::CardFor(
    content::WebContents* contents) const {
  for (const auto& card : cards_) {
    if (card->contents() == contents) {
      return card.get();
    }
  }
  return nullptr;
}

std::vector<KoanMediaCard*> KoanMediaController::GetShownCards() const {
  std::vector<KoanMediaCard*> shown;
  for (const auto& card : base::Reversed(cards_)) {
    if (card->shown() && shown.size() < kMaxStackedCards) {
      shown.push_back(card.get());
    }
  }
  return shown;
}

void KoanMediaController::FocusTab(content::WebContents* contents) {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const int index = model->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  if (KoanWorkspaceController* workspaces =
          controller ? controller->workspaces() : nullptr;
      workspaces && workspaces->IsEnabled()) {
    const std::optional<tab_groups::TabGroupId> group =
        model->GetTabGroupForTab(index);
    if (group && group != workspaces->GetActiveWorkspace()) {
      workspaces->SwitchTo(*group);
    }
  }
  model->ActivateTabAt(model->GetIndexOfWebContents(contents));
}

void KoanMediaController::InstallView() {
  if (view()) {
    return;
  }
  // At the bottom of the vertical tab strip's content area, above the
  // workspace switcher (see KoanWorkspaceController::InstallSwitcher).
  views::View* region = browser_view_->tab_strip_view();
  if (!region || region->GetClassName() != "VerticalTabStripRegionView") {
    return;
  }
  for (views::View* child : region->children()) {
    if (child->GetClassName() != "FlexLayoutView") {
      continue;
    }
    size_t index = child->children().size();
    for (size_t i = 0; i < child->children().size(); ++i) {
      if (child->children()[i]->GetClassName() == "KoanWorkspaceSwitcherView") {
        index = i;
      }
    }
    view_.SetView(child->AddChildViewAt(
        std::make_unique<KoanMediaControlsView>(this), index));
    sidebar_observation_.Reset();
    sidebar_observation_.Observe(region);
    UpdateView();
    return;
  }
}

KoanMediaControlsView* KoanMediaController::view() {
  return static_cast<KoanMediaControlsView*>(view_.view());
}

void KoanMediaController::UpdateView() {
  KoanMediaControlsView* controls = view();
  if (!controls) {
    // InstallView() updates the view it adds.
    InstallView();
    return;
  }
  controls->Update();
  UpdateVisibility();
}

void KoanMediaController::UpdateVisibility() {
  KoanMediaControlsView* controls = view();
  if (!controls) {
    return;
  }
  // Like Zen, nothing while the sidebar is collapsed to icons.
  const views::View* region = browser_view_->tab_strip_view();
  controls->SetVisible(!GetShownCards().empty() && region &&
                       region->width() >= kMediaMinSidebarWidth);
}

}  // namespace koan
