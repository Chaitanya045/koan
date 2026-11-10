// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   zen-omnibox.css, ZenUIManager.mjs (the floating URL bar card: size,
//   position, input, divider) and the zen.urlbar preferences (when the bar
//   floats)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/common/url_constants.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/omnibox/omnibox_next_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_starter_pack_data.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_omnibox_disposition.h"
#include "koan/browser/ui/views/koan_pinned.h"
#include "koan/browser/ui/views/koan_pinned_site.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_sidebar_nav_view.h"
#include "koan/browser/ui/views/koan_sidebar_tabs_controller.h"
#include "koan/browser/ui/views/koan_site_panel_actions.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_strip_on_share.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_url_bar.h"
#include "koan/browser/ui/views/koan_welcome.h"
#include "koan/browser/ui/views/koan_window_glass.h"
#include "koan/common/koan_url_constants.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

// A new-tab session's empty list is asked for again every 150 ms, up to ten
// times, if it has not come up.
constexpr int kZeroSuggestRetries = 10;
constexpr base::TimeDelta kZeroSuggestRetryDelay = base::Milliseconds(150);

// Whether a tab of this profile is on `url` (ignoring the fragment), which is
// what Navigate()'s kSwitchToTab looks for.
bool HasOpenTabWithUrl(BrowserWindowInterface* browser, const GURL& url) {
  for (BrowserWindowInterface* other : GetAllBrowserWindowInterfaces()) {
    if (other->GetProfile() != browser->GetProfile()) {
      continue;
    }
    const TabStripModel* model = other->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      if (model->GetWebContentsAt(i)->GetLastCommittedURL().EqualsIgnoringRef(
              url)) {
        return true;
      }
    }
  }
  return false;
}

// Zen's zen.urlbar.wait-to-clear.
constexpr base::TimeDelta kKeepLastSearch = base::Seconds(45);

}  // namespace

// Behind the floating card, over the whole window: paints Zen's shadow of
// the card and closes the URL bar when clicked (Zen: a click outside closes
// it without opening anything).
class KoanUrlBarBackdrop : public views::View {
  METADATA_HEADER(KoanUrlBarBackdrop, views::View)

 public:
  explicit KoanUrlBarBackdrop(base::RepeatingClosure on_press)
      : on_press_(std::move(on_press)) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  void SetCard(const gfx::Rect& card) {
    if (card_ != card) {
      card_ = card;
      SchedulePaint();
    }
  }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override {
    // Closing removes this view; not while it handles the event.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             on_press_);
    return true;
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (card_.IsEmpty()) {
      return;
    }
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::ShadowValues shadows = {gfx::ShadowValue(
        gfx::Vector2d(0, FloatingUrlBarShadowY()), FloatingUrlBarShadowBlur(),
        colors->GetColor(kColorKoanFloatingUrlBarShadow))};
    // The shape itself, in the card's colour, is hidden under the card.
    gfx::Rect shape = card_;
    shape.Inset(-FloatingUrlBarShadowSpread());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorToolbar));
    flags.setLooper(gfx::CreateShadowDrawLooper(shadows));
    {
      // Only the shadow, outside the card: a glass card blurs what is under
      // it, which must be the page, not this shape.
      gfx::ScopedCanvas scoped(canvas);
      canvas->sk_canvas()->clipRRect(
          SkRRect::MakeRectXY(gfx::RectToSkRect(card_),
                              FloatingUrlBarCornerRadius(),
                              FloatingUrlBarCornerRadius()),
          SkClipOp::kDifference, /*doAntiAlias=*/true);
      canvas->DrawRoundRect(gfx::RectF(shape), FloatingUrlBarCornerRadius(),
                            flags);
    }
    // shadcn's 1px `border` around the card, just outside it.
    cc::PaintFlags border;
    border.setAntiAlias(true);
    border.setStyle(cc::PaintFlags::kStroke_Style);
    border.setStrokeWidth(FloatingUrlBarBorderWidth());
    border.setColor(colors->GetColor(kColorKoanFloatingUrlBarBorder));
    // Just outside the card, as Zen's outline is.
    gfx::RectF ring(card_);
    ring.Outset(FloatingUrlBarBorderWidth() / 2);
    canvas->DrawRoundRect(
        ring, FloatingUrlBarCornerRadius() + FloatingUrlBarBorderWidth() / 2,
        border);
  }

 private:
  base::RepeatingClosure on_press_;
  gfx::Rect card_;
};

BEGIN_METADATA(KoanUrlBarBackdrop)
END_METADATA

// The card's flat surface: behind the location bar (inside the toolbar), so
// the whole input row, including what the bar's rounded background leaves
// uncovered, is the card's colour.
class KoanUrlBarCardFill : public views::View {
  METADATA_HEADER(KoanUrlBarCardFill, views::View)

 public:
  KoanUrlBarCardFill() {
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);
  }

  // A glass card's fill is translucent (Zen's acrylic-urlbar fill over the
  // blurred page).
  void SetGlass(bool glass) {
    if (glass_ != glass) {
      glass_ = glass;
      SchedulePaint();
    }
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->FillRect(GetLocalBounds(),
                     GetColorProvider()->GetColor(
                         glass_ ? kColorKoanFloatingUrlBarGlass
                                : kColorKoanFloatingUrlBarBackground));
  }

 private:
  bool glass_ = false;
};

BEGIN_METADATA(KoanUrlBarCardFill)
END_METADATA

// Over the floating card, in a layer of its own so that it is above the
// location bar's own layers (its icon's ink drop): the magnifier in place of
// the location icon, and the divider under the input that separates it from
// the suggestions.
class KoanUrlBarCardDecoration : public views::View {
  METADATA_HEADER(KoanUrlBarCardDecoration, views::View)

 public:
  KoanUrlBarCardDecoration() {
    // Paints over the toolbar but never takes an event.
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  // `icon` is the location icon's bounds in this view. With `glyph_x`, the
  // magnifier's glyph starts there (Zen's inset), else it is centred on the
  // icon. `leading` is the width at the card's start that the toolbar, moved
  // right for Zen's text inset, leaves uncovered.
  void SetIconBounds(const gfx::Rect& icon,
                     std::optional<int> glyph_x,
                     int leading) {
    if (icon_ != icon || glyph_x_ != glyph_x || leading_ != leading) {
      icon_ = icon;
      glyph_x_ = glyph_x;
      leading_ = leading;
      SchedulePaint();
    }
  }

  // As KoanUrlBarCardFill::SetGlass(), for the strip SetIconBounds() names.
  void SetGlass(bool glass) {
    if (glass_ != glass) {
      glass_ = glass;
      SchedulePaint();
    }
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    if (leading_ > 0) {
      // The card's fill there, as KoanUrlBarCardFill paints it in the toolbar.
      canvas->FillRect(
          gfx::Rect(0, 0, leading_, height()),
          colors->GetColor(glass_ ? kColorKoanFloatingUrlBarGlass
                                  : kColorKoanFloatingUrlBarBackground));
    }
    if (!icon_.IsEmpty()) {
      // A plain, muted magnifier: the location icon under it is hidden
      // (KoanUrlBarController::SetLocationIconHidden()), not painted over, so
      // nothing of its chip or its ink drop shows, glass or not.
      const gfx::ImageSkia magnifier = gfx::CreateVectorIcon(
          kKoanUrlbarSearchGlassIcon, kFloatingUrlBarMagnifierSize,
          colors->GetColor(ui::kColorIcon));
      const int x =
          glyph_x_
              ? *glyph_x_ -
                    static_cast<int>(std::lround(kFloatingUrlBarMagnifierSize *
                                                 kSearchGlassGlyphStart))
              : icon_.x() + (icon_.width() - magnifier.width()) / 2;
      canvas->DrawImageInt(
          magnifier, x, icon_.y() + (icon_.height() - magnifier.height()) / 2);
    }
    // A thin line inset from the card's edges (Zen's input row padding).
    canvas->FillRect(
        gfx::Rect(kFloatingUrlBarDividerInset, height() - 1,
                  std::max(0, width() - 2 * kFloatingUrlBarDividerInset), 1),
        colors->GetColor(kColorKoanFloatingUrlBarDivider));
  }

 private:
  gfx::Rect icon_;
  std::optional<int> glyph_x_;
  int leading_ = 0;
  bool glass_ = false;
};

BEGIN_METADATA(KoanUrlBarCardDecoration)
END_METADATA

KoanUrlBarController::KoanUrlBarController(
    BrowserView* browser_view,
    KoanCompactModeController* compact_mode)
    : browser_view_(browser_view), compact_mode_(compact_mode) {
  // Audit O15, the leak detector: Koan's card and its row styling are for
  // Chromium's Views suggestions popup; koan_feature_overrides.cc turns the
  // WebUI one off. Something turning it back on (a roll, the chrome://flags
  // entry) says so in the log, which the Mac harness and omnibox_lockdown
  // grep for: not a crash, a user can flip that flag.
  if (omnibox::IsWebUIOmniboxPopupEnabled()) {
    LOG(ERROR) << "Koan: Chromium's WebUI omnibox popup is on; the URL bar's "
                  "suggestions are not Zen's";
  }
  // The glass card follows its setting and Reduce Transparency live (the
  // window's activity is subscribed once it has a widget).
  if (g_browser_process && g_browser_process->local_state()) {
    prefs_registrar_.Init(g_browser_process->local_state());
    prefs_registrar_.Add(
        prefs::kLookAcrylicUrlBar,
        base::BindRepeating(&KoanUrlBarController::UpdateGlassCard,
                            base::Unretained(this)));
  }
  theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
}

KoanUrlBarController::~KoanUrlBarController() {
  if (views::View* omnibox = key_target_.view()) {
    omnibox->RemovePreTargetHandler(this);
  }
}

void KoanUrlBarController::UpdateKeyHandler() {
  // While the bar is open, or open for a new tab in the toolbar (the
  // multiple-toolbars layout), or over a Glance (wherever it edits: Esc
  // gives the Glance page focus back). Otherwise, without the Koan URL bar,
  // Chrome's Esc.
  LocationBarView* bar = location_bar();
  const bool handles_keys =
      (features::IsUrlBarEnabled() && (is_open() || new_tab_)) || glance_edit_;
  views::View* omnibox = handles_keys && bar ? bar->omnibox_view() : nullptr;
  if (key_target_.view() == omnibox) {
    return;
  }
  if (views::View* old_omnibox = key_target_.view()) {
    old_omnibox->RemovePreTargetHandler(this);
  }
  key_target_.SetView(omnibox);
  if (omnibox) {
    omnibox->AddPreTargetHandler(this);
  }
}

bool KoanUrlBarController::CanOpenBar() const {
  LocationBarView* bar = location_bar();
  // (In fullscreen, the toolbar is out of sight, except where Koan's stays
  // hidden: on macOS, see HidesFullscreenTopChrome().)
  return features::IsUrlBarEnabled() && browser_view_->GetIsNormalType() &&
         (!IsWindowFullscreen(browser_view_) ||
          compact_mode_->hides_toolbar()) &&
         !IsContentFullscreen(browser_view_) &&
         !browser_view_->browser()->GetTabStripModel()->empty() && bar &&
         bar->omnibox_view();
}

bool KoanUrlBarController::OpenForTabSearch() {
  if (!CanOpenBar()) {
    return false;
  }
  // The scope first: Zen's "search tabs" is the URL bar in its tabs search
  // mode: a "Tabs" chip and an empty input, with Enter on a match switching to
  // that tab. Chromium's is the @tabs keyword mode. Without it (the starter
  // pack is off, or the TemplateURLService isn't loaded yet) nothing is done
  // here, and the caller focuses the omnibox as Chromium's shortcut does.
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(browser_view_->GetProfile());
  const TemplateURL* tabs =
      service ? service->FindStarterPackTemplateURL(
                    template_url_starter_pack_data::StarterPackId::kTabs)
              : nullptr;
  if (!tabs || !tabs->CanBeUsedForKeywordMatching()) {
    return false;
  }
  // A new-tab bar that is open is not this one (its Enter would open a tab).
  if (new_tab_) {
    EndNewTabSession(/*committed=*/false);
  }
  LocationBarView* bar = location_bar();
  bar->FocusLocation(/*is_user_initiated=*/true,
                     /*clear_focus_if_failed=*/false);
  if (!bar->omnibox_view()->HasFocus()) {
    return false;
  }
  // Already focused before (no focus change): place it.
  if (is_open()) {
    SetPlacement(DesiredPlacement());
  }
  // The keyword mode has to be entered explicitly: SetUserText() clears it, and
  // only typing the space after the keyword turns it on, so setting "@tabs "
  // as text would be a plain web search.
  OmniboxView* view = bar->omnibox_view();
  view->SetUserText(std::u16string(), /*update_popup=*/false);
  bar->GetOmniboxController()->edit_model()->EnterKeywordMode(
      metrics::OmniboxEventProto::KEYBOARD_SHORTCUT, tabs,
      AutocompleteMatch::GetKeywordPlaceholder(
          tabs, /*is_history_embeddings_enabled=*/false));
  return true;
}

bool KoanUrlBarController::OpenForNewTab() {
  LocationBarView* bar = location_bar();
  if (!CanOpenBar() || !browser_view_->GetProfile()
                            ->GetOriginalProfile()
                            ->GetPrefs()
                            ->GetBoolean(prefs::kUrlBarReplaceNewTab)) {
    return false;
  }
  // Zen: the shortcut again closes the bar it opened.
  if (new_tab_) {
    Close();
    return true;
  }
  new_tab_ = true;
  // The keyboard: floating, even if the pill was being edited.
  edit_in_pill_ = false;
  UpdateKeyHandler();
  SetNewTabRowSelected(true);
  std::u16string text;
  if (base::TimeTicks::Now() - last_search_time_ < kKeepLastSearch) {
    text = last_search_;
  }
  last_search_.clear();
  // Not "user initiated": that would start the zero-prefix request with the
  // page's address, and the empty box needs its own, below.
  bar->FocusLocation(/*is_user_initiated=*/false,
                     /*clear_focus_if_failed=*/false);
  if (!bar->omnibox_view()->HasFocus()) {
    DLOG(WARNING) << "Koan: Cmd+T could not focus the URL bar; opening a tab";
    new_tab_ = false;
    UpdateKeyHandler();
    SetNewTabRowSelected(false);
    return false;
  }
  // Already focused before (no focus change): place it for the new tab.
  if (is_open()) {
    SetPlacement(DesiredPlacement());
  }
  // Zen opens the new-tab bar empty (or with the unfinished search). Empty,
  // it lists the open tabs and the most visited sites at once
  // (koan_zero_suggest.h).
  if (text.empty()) {
    bar->omnibox_view()->SetUserText(text, /*update_popup=*/false);
    OmniboxController* omnibox_controller = bar->GetOmniboxController();
    omnibox_controller->StopAutocomplete(/*clear_result=*/true);
    omnibox_controller->edit_model()->StartZeroSuggestRequest(
        /*user_clobbered_permanent_text=*/true);
    // Upstream drops that request while the page has no entry yet (a window's
    // first tab, still starting) or the popup is still closing: ask again
    // until the list is up.
    text_was_empty_ = true;
    ScheduleEmptyListRetry(kZeroSuggestRetries);
  } else {
    text_was_empty_ = false;
    bar->omnibox_view()->SetUserText(text, /*update_popup=*/true);
  }
  return true;
}

void KoanUrlBarController::ScheduleEmptyListRetry(int retries_left) {
  // One chain at a time, however often the text goes empty meanwhile.
  if (retries_left == kZeroSuggestRetries) {
    if (empty_list_retry_running_) {
      return;
    }
    empty_list_retry_running_ = true;
  }
  // The first attempt in the next task (the list is back at once when the
  // previous request is done); the retries after a pause.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&KoanUrlBarController::RetryEmptyList,
                     weak_factory_.GetWeakPtr(), retries_left),
      retries_left == kZeroSuggestRetries ? base::TimeDelta()
                                          : kZeroSuggestRetryDelay);
}

void KoanUrlBarController::RetryEmptyList(int retries_left) {
  LocationBarView* bar = location_bar();
  if ((!new_tab_ && !is_open()) || !bar || !bar->omnibox_view() ||
      !bar->omnibox_view()->HasFocus() ||
      !bar->omnibox_view()->GetText().empty()) {
    empty_list_retry_running_ = false;
    return;
  }
  OmniboxController* omnibox_controller = bar->GetOmniboxController();
  if (omnibox_controller->IsPopupOpen() ||
      omnibox_controller->edit_model()->is_keyword_selected()) {
    empty_list_retry_running_ = false;
    return;
  }
  // Not while a request is running (the top sites are on their way): that
  // would drop its answer. Ask again once it is done.
  if (omnibox_controller->autocomplete_controller()->done()) {
    omnibox_controller->edit_model()->StartZeroSuggestRequest(
        /*user_clobbered_permanent_text=*/true);
  }
  if (retries_left > 1) {
    ScheduleEmptyListRetry(retries_left - 1);
  } else {
    empty_list_retry_running_ = false;
  }
}

bool KoanUrlBarController::OpenForGlance(const GURL& url) {
  LocationBarView* bar = location_bar();
  if (!browser_view_->GetIsNormalType() || IsContentFullscreen(browser_view_) ||
      !bar || !bar->omnibox_view()) {
    return false;
  }
  EndNewTabSession(/*committed=*/false);
  glance_edit_ = true;
  UpdateKeyHandler();
#if BUILDFLAG(IS_MAC)
  // The card's window is key; the omnibox is in the browser window.
  browser_view_->GetWidget()->Activate();
#endif
  bar->FocusLocation(/*is_user_initiated=*/true,
                     /*clear_focus_if_failed=*/false);
  if (!bar->omnibox_view()->HasFocus()) {
    glance_edit_ = false;
    UpdateKeyHandler();
    return false;
  }
  // Already focused before (no focus change): place it for the Glance.
  if (is_open()) {
    SetPlacement(DesiredPlacement());
  }
  // The Glance page's address, not the tab's (which the omnibox shows), to
  // edit; Zen selects it all.
  bar->omnibox_view()->SetUserText(base::UTF8ToUTF16(url.spec()),
                                   /*update_popup=*/false);
  bar->omnibox_view()->SelectAll(/*reversed=*/true);
  return true;
}

bool KoanUrlBarController::TakeGlanceEdit() {
  if (!glance_edit_) {
    return false;
  }
  glance_edit_ = false;
  UpdateKeyHandler();
  return true;
}

void KoanUrlBarController::OnAddedToWidget() {
  // After the window has been laid out and shown (and the welcome has had its
  // say).
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanUrlBarController::OpenOnStartup,
                                weak_factory_.GetWeakPtr()));
}

void KoanUrlBarController::OpenOnStartup() {
  Profile* profile = browser_view_->GetProfile();
  BrowserWindowInterface* browser = browser_view_->browser();
  const KoanWelcome* welcome =
      KoanBrowserViewController::From(browser_view_)->welcome();
  if (!features::IsUrlBarEnabled() || profile->IsOffTheRecord() ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      !profile->GetPrefs()->GetBoolean(prefs::kUrlBarReplaceNewTab) ||
      (welcome && welcome->is_showing()) ||
      SessionRestore::IsRestoring(profile) || !browser_view_->GetWidget()) {
    return;
  }
  // Only a window that starts on a new tab page: its only tab, on the empty
  // page.
  const TabStripModel* model = browser->GetTabStripModel();
  if (model->count() != 1) {
    return;
  }
  content::WebContents* contents = model->GetWebContentsAt(0);
  // The window's own first tab, not one adopted from another window (a tab
  // dragged out, "Move to new window", chrome.windows.create({tabId})): that
  // one has committed pages already, where a tab made with the window is
  // still on its initial navigation. Zen opens the bar only on its own empty
  // tab. This relies on the task running before the first navigation commits;
  // if that order ever changes the bar just does not open on startup. (Not
  // during a tab drag either.)
  if (TabDragController::IsActive() ||
      !contents->GetController().IsInitialBlankNavigation()) {
    return;
  }
  const GURL& url = contents->GetVisibleURL();
  const bool empty_page =
      url.IsAboutBlank() || (url.SchemeIs(content::kChromeUIScheme) &&
                             (url.host() == chrome::kChromeUINewTabHost ||
                              url.host() == kNewTabHost));
  if (empty_page && !is_open()) {
    OpenForNewTab();
  }
}

void KoanUrlBarController::Close() {
  LocationBarView* bar = location_bar();
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  if (glance_edit_ && controller && controller->glance()->is_open() &&
      !controller->glance()->is_closing()) {
    controller->glance()->FocusPage();
  } else if (views::WebView* web_view =
                 browser_view_->GetActiveContentsWebView()) {
    web_view->RequestFocus();
  }
  views::FocusManager* focus_manager = browser_view_->GetFocusManager();
  if (bar && focus_manager && focus_manager->GetFocusedView() &&
      bar->Contains(focus_manager->GetFocusedView())) {
    focus_manager->ClearFocus();
  }
}

void KoanUrlBarController::OnBrowserViewLayout() {
  if (!is_open()) {
    return;
  }
  if (DesiredPlacement() != placement_) {
    // Placements change views; not during layout.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanUrlBarController::OnModeChanged,
                                  weak_factory_.GetWeakPtr()));
    return;
  }
  Layout();
}

void KoanUrlBarController::OnModeChanged() {
  views::FocusManager* focus_manager = browser_view_->GetFocusManager();
  LocationBarView* bar = location_bar();
  const bool editing = bar && focus_manager &&
                       focus_manager->GetFocusedView() &&
                       bar->Contains(focus_manager->GetFocusedView());
  SetPlacement(editing ? DesiredPlacement() : Placement::kNone);
}

void KoanUrlBarController::OnFocusChanged(views::View* focused_now) {
  // The focus leaves with the window (Alt-Tab, another window) and comes
  // back with it: keep the bar, what was typed and the new-tab session, like
  // Zen. Views clears the focus after the widget is inactive.
  views::Widget* widget = browser_view_->GetWidget();
  if (!focused_now && widget && !widget->IsActive()) {
    return;
  }
  LocationBarView* bar = location_bar();
  if (focused_now && bar && bar->Contains(focused_now)) {
    if (!is_open()) {
      user_input_in_progress_ =
          bar->GetOmniboxController()->edit_model()->user_input_in_progress();
    }
    SetPlacement(DesiredPlacement());
    // An invisible focus (search::FocusOmnibox(OMNIBOX_FOCUS_INVISIBLE), from
    // a search page's own box) is not the user asking for the URL bar. Upstream
    // focuses the omnibox first and marks the focus invisible afterwards, so
    // this can only be told once it has finished: look again then.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanUrlBarController::CloseIfFocusInvisible,
                                  weak_factory_.GetWeakPtr()));
    return;
  }
  const bool was_open = is_open() || new_tab_;
  EndNewTabSession(/*committed=*/false);
  glance_edit_ = false;
  EndEditing();
  // Like Zen's single toolbar, a closed bar shows the page's address again
  // (the pill shows it; the omnibox is out of sight).
  if (was_open && bar && features::IsUrlBarEnabled()) {
    bar->Revert();
  }
}

void KoanUrlBarController::CloseIfFocusInvisible() {
  LocationBarView* bar = location_bar();
  if (!is_open() || new_tab_ || !bar || !bar->omnibox_view() ||
      !bar->omnibox_view()->HasFocus() ||
      bar->GetOmniboxController()->edit_model()->focus_state() !=
          OMNIBOX_FOCUS_INVISIBLE) {
    return;
  }
  // Not just EndEditing(): the omnibox would keep its hidden focus, and a
  // click on the pill, Cmd+L (no focus change) and typing would do nothing
  // visible. The page gets the focus; a page that wants the omnibox for its
  // own box (a fakebox) asks again.
  Close();
}

void KoanUrlBarController::OnFocusLocationCommand() {
  // Cmd+L floats the bar, also when it is already open in the pill (no focus
  // change then, so nothing else would move it).
  edit_in_pill_ = false;
  if (is_open()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanUrlBarController::FloatIfOpenInPill,
                                  weak_factory_.GetWeakPtr()));
  }
}

void KoanUrlBarController::FloatIfOpenInPill() {
  if (placement_ == Placement::kInPill) {
    SetPlacement(DesiredPlacement());
  }
}

void KoanUrlBarController::NoteEditInPill() {
  edit_in_pill_ = true;
}

void KoanUrlBarController::FocusLocationInPill() {
  LocationBarView* bar = location_bar();
  if (!bar) {
    return;
  }
  edit_in_pill_ = true;
  bar->FocusLocation(/*is_user_initiated=*/true,
                     /*clear_focus_if_failed=*/false);
  // A focus that failed (content fullscreen, no omnibox) must not leave the
  // next keyboard-only focus (Ctrl+K, F6) editing in the pill.
  if (!bar->omnibox_view() || !bar->omnibox_view()->HasFocus()) {
    edit_in_pill_ = false;
  }
}

void KoanUrlBarController::EndEditing() {
  edit_in_pill_ = false;
  text_was_empty_ = false;
  SetPlacement(Placement::kNone);
  floated_ = false;
  user_input_in_progress_ = false;
}

void KoanUrlBarController::OnOmniboxChanged(bool user_input_in_progress) {
  // The text was deleted back to nothing: Zen lists the open tabs and top
  // sites again. Chromium asks for zero-prefix suggestions only when the user
  // *starts* editing with an empty box (OmniboxEditModel::UpdateInput()); text
  // deleted to empty is an ordinary empty input with no suggestions, which
  // the zero-prefix provider (koan_zero_suggest.h) does not answer. Only on
  // the change from text to none: asking again makes this run again (the
  // first pass of a request always reports a change), so every later empty
  // call would ask once more, for ever when there is nothing to list.
  if ((is_open() || new_tab_) && features::IsUrlBarEnabled() &&
      location_bar() && location_bar()->omnibox_view()) {
    const bool empty = location_bar()->omnibox_view()->GetText().empty() &&
                       !location_bar()
                            ->GetOmniboxController()
                            ->edit_model()
                            ->is_keyword_selected();
    if (empty && !text_was_empty_) {
      ScheduleEmptyListRetry(kZeroSuggestRetries);
    }
    text_was_empty_ = empty;
  }
  if ((!is_open() && !new_tab_) ||
      user_input_in_progress == user_input_in_progress_) {
    return;
  }
  user_input_in_progress_ = user_input_in_progress;
  // Called from LocationBarView::OnChanged(): move the bar afterwards.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanUrlBarController::UpdatePlacementOnInput,
                                weak_factory_.GetWeakPtr()));
}

void KoanUrlBarController::UpdatePlacementOnInput() {
  if (!is_open()) {
    return;
  }
  SetPlacement(DesiredPlacement());
  // "normal" widens over the page while typing.
  Layout();
}

bool KoanUrlBarController::TakeNewTabDisposition() {
  if (!new_tab_) {
    return false;
  }
  EndNewTabSession(/*committed=*/true);
  return true;
}

void KoanUrlBarController::SetNewTabRowSelected(bool selected) {
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_);
      controller && controller->sidebar_tabs()) {
    controller->sidebar_tabs()->SetNewTabSelected(selected);
  }
}

void KoanUrlBarController::EndNewTabSession(bool committed) {
  if (!new_tab_) {
    return;
  }
  new_tab_ = false;
  text_was_empty_ = false;
  UpdateKeyHandler();
  SetNewTabRowSelected(false);
  LocationBarView* bar = location_bar();
  if (!committed && user_input_in_progress_ && bar && bar->omnibox_view()) {
    last_search_ = bar->omnibox_view()->GetText();
    last_search_time_ = base::TimeTicks::Now();
  }
}

std::string KoanUrlBarController::Behavior() const {
  return browser_view_->GetProfile()
      ->GetOriginalProfile()
      ->GetPrefs()
      ->GetString(prefs::kUrlBarBehavior);
}

LocationBarView* KoanUrlBarController::location_bar() const {
  return browser_view_->GetLocationBarView();
}

bool KoanUrlBarController::IsPillAvailable() const {
  KoanSidebarNavView* nav = compact_mode_->sidebar_nav();
  return nav && nav->url_pill()->IsDrawn() &&
         !nav->url_pill()->GetVisibleBounds().IsEmpty();
}

KoanUrlBarController::Placement KoanUrlBarController::DesiredPlacement() const {
  // In the multiple-toolbars layout the omnibox edits in the toolbar.
  // (Also in macOS' fullscreen: Koan's toolbar stays hidden there, see
  // HidesFullscreenTopChrome().)
  if (!compact_mode_->hides_toolbar() || IsContentFullscreen(browser_view_)) {
    return Placement::kNone;
  }
  // Without the Koan URL bar, Koan's earlier behaviour: always floating.
  if (!features::IsUrlBarEnabled()) {
    return Placement::kFloating;
  }
  // Over a Glance (⌘L edits the Glance page's address) it stays in the pill:
  // floating, it would be under the Glance card, a child window above the
  // window's contents. *(live check: compact mode with the sidebar hidden,
  // where it floats)*
  if (glance_edit_ && !compact_mode_->hides_sidebar() && IsPillAvailable()) {
    return Placement::kInPill;
  }
  // Compact mode with the sidebar hidden floats it too when it's opened from
  // a peeked sidebar's pill: Chromium collapses an expanded-on-hover sidebar
  // while the omnibox's suggestions are open, which would take the pill away.
  const std::string behavior = Behavior();
  if (floated_ || behavior == prefs::kUrlBarBehaviorFloat ||
      compact_mode_->hides_sidebar() || !IsPillAvailable()) {
    return Placement::kFloating;
  }
  // Zen's floating-on-type: the bar floats unless the input was focused by a
  // mouse press (UrlbarInputBase #openPopover, test_Click_Shoudnt_FLoat_
  // Urlbar): Cmd+L and Cmd+T float at once, and a click on the pill edits in
  // the pill, which grows over the page as you type. Zen decides this once,
  // when the bar opens (#openPopover returns early once it is open): typing
  // after a click never floats it.
  if (behavior != prefs::kUrlBarBehaviorNormal &&
      (!edit_in_pill_ || new_tab_)) {
    return Placement::kFloating;
  }
  return Placement::kInPill;
}

void KoanUrlBarController::SetPlacement(Placement placement) {
  if (placement == placement_) {
    return;
  }
  views::View* top_container = browser_view_->top_container();
  LocationBarView* bar = location_bar();
  const bool opening = placement_ == Placement::kNone;
  if (placement == Placement::kNone || !bar || !bar->parent() ||
      !top_container->layer() || !top_container->parent() ||
      !browser_view_->toolbar()) {
    if (opening) {
      return;
    }
    // Closing: put everything back.
    if (KoanSidebarNavView* nav = compact_mode_->sidebar_nav()) {
      nav->SetEditing(false);
    }
    top_container_observation_.Reset();
    toolbar_observation_.Reset();
    sibling_observation_.RemoveAllObservations();
    {
      base::AutoReset<bool> changing(&changing_toolbar_visibility_, true);
      for (const auto& [view, visible] : restore_) {
        view->SetVisible(visible);
      }
    }
    restore_.clear();
    card_size_ = gfx::Size();
    if (ui::Layer* layer = top_container->layer()) {
      layer->GetAnimator()->AbortAllAnimations();
      layer->SetOpacity(1.0f);
      layer->SetRoundedCornerRadius(gfx::RoundedCornersF());
      layer->SetMasksToBounds(false);
    }
    placement_ = Placement::kNone;
    UpdateKeyHandler();
    ApplyCardStyle();
    RefreshPageActions();
    browser_view_->InvalidateLayout();
    compact_mode_->OnUrlBarOpenChanged();
    return;
  }

  placement_ = placement;
  if (placement == Placement::kFloating) {
    floated_ = true;
  }
  if (opening) {
    // Audit O15, also on every opening: the AI Mode popup (the composebox
    // that covers the location bar) must not be reachable in a Koan window.
    if (omnibox::IsAimPopupEnabled(browser_view_->GetProfile())) {
      LOG(ERROR) << "Koan: Chromium's AI Mode omnibox popup is available";
    }
    if (KoanSidebarNavView* nav = compact_mode_->sidebar_nav()) {
      nav->SetEditing(true);
    }
    // Just the location bar: hide the rest of the toolbar, remembering what
    // to restore (and watching for other code showing things meanwhile).
    for (views::View* sibling : bar->parent()->children()) {
      if (sibling != bar) {
        restore_[sibling] = sibling->GetVisible();
        sibling_observation_.AddObservation(sibling);
      }
    }
    HideToolbarSiblings();
    top_container->SetCanProcessEventsWithinSubtree(true);
    top_container_observation_.Observe(top_container);
    toolbar_observation_.Observe(browser_view_->toolbar());
    UpdateKeyHandler();
    ui::Layer* layer = top_container->layer();
    layer->GetAnimator()->AbortAllAnimations();
    layer->SetTransform(gfx::Transform());
    RefreshPageActions();
    compact_mode_->OnUrlBarOpenChanged();
  }
  // The style first: the larger floating font can change the toolbar's
  // preferred height.
  ApplyCardStyle();
  Layout();
  // Zen 1.24b on macOS opens and closes the bar in a single frame: no fade.
}

void KoanUrlBarController::RefreshPageActions() {
  // The location bar asks HidesPageActionIcons() when it changes. Not while
  // it is out of a window (OnChanged() needs its colour provider).
  LocationBarView* bar = location_bar();
  if (bar && bar->GetWidget()) {
    LocationBar* location = bar;
    location->OnChanged();
    location->UpdateContentSettingsIcons();
  }
}

void KoanUrlBarController::HideToolbarSiblings() {
  if (!is_open()) {
    return;
  }
  base::AutoReset<bool> changing(&changing_toolbar_visibility_, true);
  for (const auto& [view, visible] : restore_) {
    view->SetVisible(false);
  }
}

void KoanUrlBarController::ApplyCardStyle() {
  const bool floating = placement_ == Placement::kFloating;
  // Zen's look (larger font, the shadow and click-outside backdrop) comes
  // with the Koan URL bar; without it, the plain card.
  const bool zen_card = floating && features::IsUrlBarEnabled();
  LocationBarView* bar = location_bar();
  OmniboxViewViews* omnibox = bar ? bar->omnibox_view() : nullptr;

  // Zen's floating bar has a larger font than the toolbar's.
  if (zen_card && !omnibox_font_ && omnibox) {
    omnibox_font_ = omnibox->GetFontList();
    // Zen: 1.5em of 11pt, weight 500.
    omnibox->SetFontList(
        omnibox_font_
            ->DeriveWithSizeDelta(
                FloatingUrlBarFontSize(omnibox_font_->GetFontSize()) -
                omnibox_font_->GetFontSize())
            .DeriveWithWeight(look::IsShadcnStyle()
                                  ? gfx::Font::Weight::NORMAL
                                  : gfx::Font::Weight::MEDIUM));
  } else if (!zen_card && omnibox_font_) {
    if (omnibox) {
      omnibox->SetFontList(*omnibox_font_);
    }
    omnibox_font_.reset();
  }

  views::View* top_container = browser_view_->top_container();
  if (zen_card && !backdrop_ && top_container->parent()) {
    views::View* overlay = top_container->parent();
    backdrop_ = overlay->AddChildViewAt(
        std::make_unique<KoanUrlBarBackdrop>(base::BindRepeating(
            &KoanUrlBarController::Close, weak_factory_.GetWeakPtr())),
        overlay->GetIndexOf(top_container).value());
    backdrop_->SetPaintToLayer();
    backdrop_->layer()->SetFillsBoundsOpaquely(false);
  } else if (!zen_card && backdrop_) {
    views::View* overlay = backdrop_->parent();
    overlay->RemoveChildViewT(std::exchange(backdrop_, nullptr));
  }

  ToolbarView* toolbar = browser_view_->toolbar();
  if (IsCommandCard() && !decoration_) {
    // The fill goes first in the toolbar, so it is under the location bar.
    fill_ = toolbar->AddChildViewAt(std::make_unique<KoanUrlBarCardFill>(), 0);
    decoration_ = top_container->AddChildView(
        std::make_unique<KoanUrlBarCardDecoration>());
    SetLocationIconHidden(true);
  } else if (!IsCommandCard() && decoration_) {
    SetLocationIconHidden(false);
    top_container->RemoveChildViewT(std::exchange(decoration_, nullptr));
    toolbar->RemoveChildViewT(std::exchange(fill_, nullptr));
  }
  if (bar) {
    // Zen's card has no focus ring around the input.
    if (views::FocusRing* ring = views::FocusRing::Get(bar)) {
      ring->SetColorId(IsCommandCard() ? std::optional<ui::ColorId>(
                                             kColorKoanFloatingUrlBarFocusRing)
                                       : std::nullopt);
    }
  }
  if (omnibox) {
    // "Search..." in the card, Chromium's text elsewhere.
    omnibox->InstallPlaceholderText();
  }

  if (!paint_as_active_subscription_ && browser_view_->GetWidget()) {
    paint_as_active_subscription_ =
        browser_view_->GetWidget()->RegisterPaintAsActiveChangedCallback(
            base::BindRepeating(&KoanUrlBarController::UpdateGlassCard,
                                base::Unretained(this)));
  }
  UpdateGlassCard();

  if (!is_open()) {
    return;
  }
  ui::Layer* layer = top_container->layer();
  layer->SetRoundedCornerRadius(gfx::RoundedCornersF(
      floating ? FloatingUrlBarCornerRadius() : UrlPillCornerRadius()));
  layer->SetIsFastRoundedCorner(true);
  layer->SetMasksToBounds(true);
}

void KoanUrlBarController::SetLocationIconHidden(bool hidden) {
  LocationBarView* bar = location_bar();
  LocationIconView* icon = bar ? bar->location_icon_view() : nullptr;
  if (!icon) {
    return;
  }
  if (hidden) {
    // The ink drop first: off, it takes away the layers views gave the icon
    // for it (hover, the open page info bubble), and SetPaintToLayer() below
    // would trip View::DestroyLayerImpl()'s DCHECK on them. The ink drop is
    // drawn beside the icon's layer, so it could not be hidden by opacity.
    views::InkDrop::Get(icon)->SetMode(views::InkDropHost::InkDropMode::OFF);
    // Hidden by opacity, not visibility: the layout is unchanged, and the
    // icon's chip goes with it. The layer is explicit, so no ink drop's layer
    // coming and going takes it away.
    icon->SetPaintToLayer();
    icon->layer()->SetFillsBoundsOpaquely(false);
    icon->layer()->SetOpacity(0.0f);
    // Nothing of the icon can be reached either: no focus, no click, no screen
    // reader.
    icon->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    icon->SetCanProcessEventsWithinSubtree(false);
    icon->GetViewAccessibility().SetIsIgnored(true);
  } else {
    // Upstream never gives this view a layer of its own; views keeps the ink
    // drop's.
    icon->DestroyLayer();
    icon->SetCanProcessEventsWithinSubtree(true);
    icon->GetViewAccessibility().SetIsIgnored(false);
    // Chromium's own state: the icon is inert (no highlight, no focus) while
    // the omnibox is editing or empty (LocationIconView::Update()).
    const bool inert = bar->IsEditingOrEmpty();
    views::InkDrop::Get(icon)->SetMode(
        inert ? views::InkDropHost::InkDropMode::OFF
              : views::InkDropHost::InkDropMode::ON);
    icon->SetFocusBehavior(inert ? views::View::FocusBehavior::NEVER
                                 : views::PlatformStyle::kDefaultFocusBehavior);
  }
}

namespace {

// A glass card's labels (the keyword chip, ...) sit on a translucent fill: no
// subpixel text there (views::Label's DCHECK that its background is opaque).
void SetLabelsSubpixelRendering(views::View* view, bool enabled) {
  if (views::Label* label = views::AsViewClass<views::Label>(view)) {
    label->SetSubpixelRenderingEnabled(enabled);
  }
  for (views::View* child : view->children()) {
    SetLabelsSubpixelRendering(child, enabled);
  }
}

}  // namespace

void KoanUrlBarController::UpdateGlassCard() {
  const views::Widget* widget = browser_view_->GetWidget();
  const bool glass =
      widget && is_open() && IsCommandCard() && IsWindowGlassAllowed() &&
      widget->ShouldPaintAsActive() && g_browser_process &&
      g_browser_process->local_state() &&
      g_browser_process->local_state()->GetBoolean(prefs::kLookAcrylicUrlBar);
  LocationBarView* bar = location_bar();
  if (glass && bar) {
    // Also labels built since (the lazily created ones of the location bar's
    // chips).
    SetLabelsSubpixelRendering(bar, false);
  }
  if (glass == glass_card_) {
    return;
  }
  glass_card_ = glass;
  // Zen's acrylic-urlbar (zen-omnibox.css): the page blurred behind a card
  // filled rgba(20, 20, 20, .92) / rgba(255, 255, 255, .93), at full opacity
  // itself, so its text stays crisp.
  if (ui::Layer* card = browser_view_->top_container()->layer()) {
    card->SetBackgroundBlur(glass ? kFloatingUrlBarBlurRadius : 0.0f);
  }
  if (fill_) {
    fill_->SetGlass(glass);
  }
  if (decoration_) {
    decoration_->SetGlass(glass);
  }
  if (bar) {
    SetLabelsSubpixelRendering(bar, !glass);
  }
  // The location bar and its omnibox take the clear background through
  // AdjustLocationBarBackground() (the location bar refreshes itself; it is in
  // whatever widget the top container is in), and the toolbar and top
  // container through OnGlassLayout(): this window's colours only, so no
  // colour provider is rebuilt and no other view re-themed.
  if (bar) {
    bar->OnThemeChanged();
  }
  browser_view_->InvalidateLayout();
}

void KoanUrlBarController::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  UpdateGlassCard();
}

bool KoanUrlBarController::IsFloatingZenCard() const {
  return IsCommandCard() && !look::IsShadcnStyle();
}

bool KoanUrlBarController::IsCommandCard() const {
  return placement_ == Placement::kFloating && features::IsUrlBarEnabled();
}

ui::Layer* KoanUrlBarController::card_layer() const {
  return IsCommandCard() ? browser_view_->top_container()->layer() : nullptr;
}

void KoanUrlBarController::Layout() {
  views::View* top_container = browser_view_->top_container();
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!is_open() || laying_out_ || !toolbar || !top_container->parent()) {
    return;
  }
  base::AutoReset<bool> laying_out(&laying_out_, true);
  const gfx::Rect card = CardBounds();
  if (card.IsEmpty()) {
    // The pill went away (e.g. the sidebar collapsed); place it again.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanUrlBarController::OnModeChanged,
                                  weak_factory_.GetWeakPtr()));
    return;
  }
  // Zen's card is the URL field itself: size the card to the location bar
  // and shift the toolbar so the location bar fills it (the rounded mask
  // clips the toolbar's padding). Lay the toolbar out at the card's width
  // first to find where it puts the location bar.
  LocationBarView* bar = location_bar();
  if (!bar) {
    return;
  }
  toolbar->SetBoundsRect(gfx::Rect(card.size()));
  toolbar->DeprecatedLayoutImmediately();
  const gfx::Rect field =
      views::View::ConvertRectToTarget(bar->parent(), toolbar, bar->bounds());
  const int right_padding = toolbar->width() - field.right();
  const bool zen_card = IsFloatingZenCard();
  // Zen's text starts kFloatingUrlBarTextInset into the card: move the field
  // right by what the location bar's own insets leave short of that.
  int lead = 0;
  if (zen_card && bar->omnibox_view()) {
    views::View* omnibox = bar->omnibox_view();
    const int text_x = views::View::ConvertRectToTarget(
                           omnibox, toolbar, omnibox->GetContentsBounds())
                           .x() -
                       field.x();
    lead = std::max(0, kFloatingUrlBarTextInset - text_x);
  }
  // Zen's input row is 62pt tall (--urlbar-container-height); the field sits
  // in the middle of it, the rest filled with the card's colour (the
  // decoration). Its top is Zen's --zen-urlbar-top, the top of `card`; the
  // input row grows down from there, as Zen's results do.
  const int card_height =
      zen_card ? std::max(field.height(), kFloatingUrlBarInputHeight)
               : field.height();
  const gfx::Rect bar_card(
      card.x(), zen_card ? card.y() : card.CenterPoint().y() - card_height / 2,
      card.width(), card_height);
  top_container->SetBoundsRect(bar_card);
  const int toolbar_x = lead - field.x();
  toolbar->SetBoundsRect(gfx::Rect(
      toolbar_x, -field.y() + (card_height - field.height()) / 2,
      card.width() + field.x() + right_padding - lead, card.height()));
  toolbar->DeprecatedLayoutImmediately();
  if (card_size_ != bar_card.size()) {
    card_size_ = bar_card.size();
    // BrowserView's layout clips the top container and the toolbar to their
    // size at the end of each pass (BrowserViewLayoutImpl, crbug.com/
    // 390669712). A size change outside that pass (floating out of the pill
    // as you type) needs another one, or the card stays clipped.
    browser_view_->InvalidateLayout();
  }
  if (backdrop_) {
    backdrop_->SetBoundsRect(top_container->parent()->GetLocalBounds());
    backdrop_->SetCard(bar_card);
  }
  if (fill_) {
    fill_->SetBoundsRect(toolbar->GetLocalBounds());
  }
  if (decoration_) {
    decoration_->SetBoundsRect(top_container->GetLocalBounds());
    LocationIconView* icon = bar->location_icon_view();
    decoration_->SetIconBounds(
        icon && icon->GetVisible()
            ? views::View::ConvertRectToTarget(icon->parent(), top_container,
                                               icon->bounds())
            : gfx::Rect(),
        zen_card ? std::optional<int>(kFloatingUrlBarGlyphInset) : std::nullopt,
        std::max(0, toolbar_x));
  }
}

gfx::Rect KoanUrlBarController::CardBounds() const {
  views::View* overlay = browser_view_->top_container()->parent();
  const int height = browser_view_->toolbar()->GetPreferredSize().height();
  if (height <= 0) {
    return gfx::Rect();
  }
  const gfx::Rect area = views::View::ConvertRectToTarget(
      browser_view_, overlay, browser_view_->GetLocalBounds());
  if (placement_ == Placement::kFloating && look::IsShadcnStyle()) {
    // shadcn's command dialog: about 55% of the content width, a little above
    // the middle.
    const int width = area.width() * 55 / 100;
    return gfx::Rect(area.x() + (area.width() - width) / 2,
                     area.y() + area.height() * 3 / 10, width, height);
  }
  if (placement_ == Placement::kFloating) {
    const int width = std::min(area.width() * 2 / 3, kFloatingUrlBarMaxWidth);
    const int centred_height = std::max(kFloatingUrlBarMinHeight, height);
    const int top =
        area.y() + std::max(0, (area.height() - centred_height) / 2);
    return gfx::Rect(area.x() + (area.width() - width) / 2, top, width, height);
  }
  KoanSidebarNavView* nav = compact_mode_->sidebar_nav();
  if (!nav || !IsPillAvailable()) {
    return gfx::Rect();
  }
  views::View* pill = nav->url_pill();
  const gfx::Rect pill_bounds =
      views::View::ConvertRectToTarget(pill, overlay, pill->GetLocalBounds());
  // "normal" widens it over the page, away from the sidebar's edge.
  const bool sidebar_on_right = IsSidebarOnRight(browser_view_);
  int width = pill_bounds.width();
  if (user_input_in_progress_) {
    const int room = sidebar_on_right ? pill_bounds.right() - area.x()
                                      : area.right() - pill_bounds.x();
    width = std::max(
        width, std::min({area.width() * 9 / 10, kUrlBarOpenMaxWidth, room}));
  }
  // Over the pill, keeping its centre, so it reads as the pill being edited.
  return gfx::Rect(
      sidebar_on_right ? pill_bounds.right() - width : pill_bounds.x(),
      pill_bounds.CenterPoint().y() - height / 2, width, height);
}

void KoanUrlBarController::OnViewVisibilityChanged(views::View* observed_view,
                                                   views::View* starting_view,
                                                   bool visible) {
  // A toolbar view that other code shows while the bar is open: it should
  // be visible afterwards, but not inside the card.
  auto it = restore_.find(observed_view);
  if (observed_view != starting_view || it == restore_.end() ||
      changing_toolbar_visibility_ || !visible) {
    return;
  }
  it->second = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanUrlBarController::HideToolbarSiblings,
                                weak_factory_.GetWeakPtr()));
}

void KoanUrlBarController::OnViewBoundsChanged(views::View* observed_view) {
  // BrowserView's layout puts the top container back at full width.
  Layout();
}

void KoanUrlBarController::OnViewIsDeleting(views::View* observed_view) {
  if (restore_.erase(observed_view)) {
    sibling_observation_.RemoveObservation(observed_view);
  }
  if (top_container_observation_.IsObservingSource(observed_view)) {
    top_container_observation_.Reset();
  }
  if (toolbar_observation_.IsObservingSource(observed_view)) {
    toolbar_observation_.Reset();
  }
}

void KoanUrlBarController::OnKeyEvent(ui::KeyEvent* event) {
  // Only plain keys: Shift+Esc is the task manager, and other modifiers may
  // be bound too.
  constexpr int kModifiers = ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN |
                             ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN;
  // Zen: Tab on an empty input enters commands mode, the command palette
  // (its ">" mode here; Tab keeps its meaning with the palette off).
  LocationBarView* bar = location_bar();
  if (features::IsCommandPaletteEnabled() &&
      event->type() == ui::EventType::kKeyPressed &&
      event->key_code() == ui::VKEY_TAB && (event->flags() & kModifiers) == 0 &&
      (is_open() || new_tab_) && bar && bar->omnibox_view() &&
      bar->omnibox_view()->GetText().empty()) {
    event->SetHandled();
    bar->omnibox_view()->SetUserText(u">", /*update_popup=*/true);
    return;
  }
  // Zen: Esc closes the bar (Chrome's omnibox would first close the
  // suggestions, then revert, then give the page focus).
  if (event->type() == ui::EventType::kKeyPressed &&
      event->key_code() == ui::VKEY_ESCAPE &&
      (event->flags() & kModifiers) == 0 &&
      (is_open() || new_tab_ || glance_edit_)) {
    event->SetHandled();
    // Not while the omnibox dispatches the key to this handler.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanUrlBarController::Close,
                                  weak_factory_.GetWeakPtr()));
  }
}

void CopyCurrentUrl(BrowserView* browser_view, bool markdown) {
  content::WebContents* contents =
      browser_view->browser()->GetTabStripModel()->GetActiveWebContents();
  // Over a Glance card, the Glance page's: Zen's Glance is the selected tab.
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view);
      controller && controller->glance()->is_open() &&
      !controller->glance()->is_closing()) {
    contents = controller->glance()->contents();
  }
  if (!contents) {
    return;
  }
  // Zen copies the URL without its tracking parameters (#78).
  const GURL page_url = StripTrackingParameters(contents->GetVisibleURL());
  const std::u16string url = base::UTF8ToUTF16(page_url.spec());
  std::u16string text = url;
  if (markdown) {
    std::u16string title = contents->GetTitle();
    if (title.empty()) {
      title = url;
    }
    text = base::StrCat({u"[", title, u"](", url, u")"});
  }
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(text);
  }
  // Zen's strings (zen-general.ftl).
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  if (!controller) {
    return;
  }
  const std::u16string done =
      markdown ? u"Copied current URL as Markdown!" : u"Copied current URL!";
  // On macOS Zen's toast for a web page's URL can share it
  // (copyCurrentURLToClipboard()).
  if (!markdown && CanSharePage() && page_url.SchemeIsHTTPOrHTTPS()) {
    controller->toast()->ShowWithAction(
        done, kKoanShareIcon, u"Share",
        base::BindOnce(
            [](base::WeakPtr<content::WebContents> contents, const GURL& url) {
              if (contents) {
                SharePage(contents.get(), url);
              }
            },
            contents->GetWeakPtr(), page_url));
    return;
  }
  controller->toast()->Show(done);
}

// The koan_url_bar.h hooks.

void MaybeAdjustOmniboxDisposition(BrowserWindowInterface* browser,
                                   bool is_open_tab_match,
                                   const GURL& destination_url,
                                   WindowOpenDisposition* disposition) {
  if (!browser || *disposition != WindowOpenDisposition::kCurrentTab) {
    return;
  }
  if (features::IsUrlBarEnabled()) {
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    KoanBrowserViewController* controller =
        browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
    // (Also ends the new-tab session, whichever row was taken.)
    const bool new_tab_session =
        controller && controller->url_bar()->TakeNewTabDisposition();
    // What the bar opens from a new-tab session, or from the empty tab
    // (which never navigates: Zen's UrlbarChildController.getWhere), opens
    // in a new tab.
    *disposition = AdjustedOmniboxDisposition(
        *disposition, is_open_tab_match,
        is_open_tab_match && HasOpenTabWithUrl(browser, destination_url),
        new_tab_session || IsEmptyTab(browser->GetActiveTabInterface()));
    if (*disposition != WindowOpenDisposition::kCurrentTab) {
      return;
    }
  }
  // A pinned tab or Essential keeps its site (Zen's getOpenUILinkWhere).
  content::WebContents* active =
      browser->GetTabStripModel()->GetActiveWebContents();
  if (active && IsPinnedForUser(active) &&
      browser->GetProfile()->GetPrefs()->GetBoolean(
          prefs::kOpenPinnedInNewTab) &&
      !StaysOnPinnedSite(active->GetLastCommittedURL(), destination_url)) {
    *disposition = WindowOpenDisposition::kNewForegroundTab;
  }
}

bool SuppressesDefaultUrlBarFocus(BrowserWindowInterface* browser) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller && features::IsUrlBarEnabled() &&
         controller->compact_mode()->hides_toolbar() &&
         browser_view->GetProfile()
             ->GetOriginalProfile()
             ->GetPrefs()
             ->GetBoolean(prefs::kUrlBarReplaceNewTab);
}

void OnFocusLocationBarCommand(BrowserWindowInterface* browser) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  if (KoanBrowserViewController* controller =
          browser_view ? KoanBrowserViewController::From(browser_view)
                       : nullptr) {
    controller->url_bar()->OnFocusLocationCommand();
  }
}

bool MaybeOpenUrlBarForNewTab(BrowserWindowInterface* browser) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller && controller->url_bar()->OpenForNewTab();
}

bool MaybeOpenOmniboxMatchInGlance(BrowserWindowInterface* browser,
                                   const GURL& url,
                                   ui::PageTransition transition,
                                   WindowOpenDisposition disposition,
                                   const std::string& extra_headers) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (!controller || !controller->url_bar()->is_glance_edit()) {
    return false;
  }
  // Alt+Enter and the like: a new tab, as from the tab. A background tab
  // keeps the omnibox (and the Glance session) open for the next pick.
  if (disposition != WindowOpenDisposition::kCurrentTab) {
    return false;
  }
  controller->url_bar()->TakeGlanceEdit();
  KoanGlanceController* glance = controller->glance();
  // Glance shows web pages (as it opens only http(s) links); anything else
  // (chrome://, file:, javascript:) goes to the tab, and the card closes.
  if (!url.SchemeIsHTTPOrHTTPS()) {
    glance->PostAction(&KoanGlanceController::Close);
    return false;
  }
  return glance->LoadFromUrlBar(url, transition, extra_headers);
}

bool KeepsReservedCommandsInFullscreen(BrowserWindowInterface* browser) {
  const BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  return browser_view && HidesFullscreenTopChrome(browser_view) &&
         !IsContentFullscreen(browser_view);
}

views::View* GetBubbleAnchorView(const views::View* view) {
  const auto* browser_view = views::AsViewClass<BrowserView>(view);
  if (!browser_view && view && view->GetWidget()) {
    browser_view =
        views::AsViewClass<BrowserView>(view->GetWidget()->client_view());
  }
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller ? controller->compact_mode()->GetBubbleAnchor() : nullptr;
}

bool IgnoresFullscreenToolbarToggle(BrowserWindowInterface* browser) {
  const BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  return browser_view && IsToolbarHidden(browser_view);
}

// The URL bar controller of the window `view` belongs to, if any: `view` is
// the location bar or a row of the omnibox popup, whose widget is a child of
// the browser window's. Null for no view (upstream's location_bar_view_ is
// null in some tests and popups) and for windows that aren't Koan's.
KoanUrlBarController* UrlBarOf(const views::View* view) {
  if (!view) {
    return nullptr;
  }
  const views::Widget* widget = view->GetWidget();
  const views::Widget* top_level =
      widget ? widget->GetTopLevelWidget() : nullptr;
  BrowserView* browser_view = top_level
                                  ? BrowserView::GetBrowserViewForNativeWindow(
                                        top_level->GetNativeWindow())
                                  : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller ? controller->url_bar() : nullptr;
}

void OnOmniboxChanged(views::View* location_bar, bool user_input_in_progress) {
  if (KoanUrlBarController* url_bar = UrlBarOf(location_bar)) {
    url_bar->OnOmniboxChanged(user_input_in_progress);
  }
}

bool ShowsCardPlaceholder(views::View* location_bar) {
  KoanUrlBarController* url_bar = UrlBarOf(location_bar);
  return url_bar && url_bar->IsCommandCard();
}

ui::ColorId AdjustOmniboxPlaceholderColorId(views::View* location_bar,
                                            ui::ColorId id) {
  KoanUrlBarController* url_bar = UrlBarOf(location_bar);
  return url_bar && url_bar->IsCommandCard()
             ? ui::ColorId(kColorKoanFloatingUrlBarPlaceholder)
             : id;
}

void AdjustOmniboxPlaceholder(views::View* location_bar, std::u16string& text) {
  KoanUrlBarController* url_bar = UrlBarOf(location_bar);
  if (!url_bar) {
    return;
  }
  // Zen's "Search..." in the card, and in the pill too (not Chromium's AI
  // Mode "Press Tab" hint), unless a search engine's keyword mode has its
  // own placeholder.
  const bool keyword_placeholder = !static_cast<LocationBarView*>(location_bar)
                                        ->GetOmniboxController()
                                        ->edit_model()
                                        ->keyword_placeholder()
                                        .empty();
  if (url_bar->IsCommandCard() ||
      (url_bar->is_open() && !keyword_placeholder)) {
    text = u"Search...";
  }
}

bool HidesPageActionIcons(const views::View* location_bar) {
  const KoanUrlBarController* url_bar = UrlBarOf(location_bar);
  return url_bar && url_bar->is_open();
}

SkColor AdjustLocationBarBackground(views::View* location_bar, SkColor color) {
  const KoanUrlBarController* url_bar = UrlBarOf(location_bar);
  return url_bar && url_bar->IsGlassCard() ? SK_ColorTRANSPARENT : color;
}

bool IsGlassUrlBarOpen(const views::View* browser_view) {
  const KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(
                         static_cast<const BrowserView*>(browser_view))
                   : nullptr;
  return controller && controller->url_bar()->IsGlassCard();
}

bool ShowsZenCardRows(const views::View* view) {
  const KoanUrlBarController* url_bar = UrlBarOf(view);
  return url_bar && url_bar->IsFloatingZenCard();
}

}  // namespace koan
