// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_pinned_tab_views.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/scoped_multi_source_observation.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_types.h"
#include "chrome/browser/ui/views/tabs/tab/tab_icon.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_pinned_tabs_hooks.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rrect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

constexpr char16_t kBackToPinnedUrl[] = u"Back to pinned url";
constexpr char16_t kSeparateFromPinnedTab[] = u"Separate from pinned tab";

// Emoji offered by "Change Icon…".
constexpr const char16_t* kIconChoices[] = {
    u"\U0001F600", u"\U0001F60E", u"\U0001F913", u"\U0001F973", u"\U0001F63A",
    u"\U0001F436", u"\U0001F98A", u"\U0001F43C", u"\U0001F30A", u"\U0001F33F",
    u"\U0001F338", u"\U0001F319", u"⭐",         u"\U0001F525", u"⚡",
    u"\U0001F308", u"\U0001F34E", u"\U0001F34A", u"\U0001F34B", u"\U0001F349",
    u"☕",         u"\U0001F355", u"\U0001F382", u"\U0001F37F", u"\U0001F4BC",
    u"\U0001F4DA", u"\U0001F4DD", u"\U0001F4C5", u"\U0001F4CC", u"\U0001F4CE",
    u"\U0001F4A1", u"\U0001F512", u"\U0001F4BB", u"\U0001F4F1", u"\U0001F3A7",
    u"\U0001F3AE", u"\U0001F3B5", u"\U0001F3AC", u"\U0001F4F7", u"\U0001F6D2",
    u"❤️",          u"\U0001F4AC", u"\U0001F4E7", u"\U0001F3E0", u"✈️",
    u"\U0001F680", u"\U0001F3C6", u"\U0001F3A8",
};

class KoanTabRow;

// Every sidebar row, by its tab.
base::flat_map<const tabs::TabInterface*, raw_ptr<KoanTabRow>>& Rows() {
  static base::NoDestructor<
      base::flat_map<const tabs::TabInterface*, raw_ptr<KoanTabRow>>>
      rows;
  return *rows;
}

KoanTabRow* RowFor(const tabs::TabInterface* tab) {
  auto it = Rows().find(tab);
  return it == Rows().end() ? nullptr : it->second.get();
}

// "Back to pinned url" under the label while the reset button is hovered.
class KoanTabSublabel : public views::View {
  METADATA_HEADER(KoanTabSublabel, views::View)

 public:
  KoanTabSublabel() {
    SetCanProcessEventsWithinSubtree(false);
    SetVisible(false);
  }

  void SetStyle(std::u16string text, const gfx::FontList& font_list) {
    text_ = std::move(text);
    font_list_ = font_list;
    SchedulePaint();
  }

  const std::u16string& GetText() const { return text_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->DrawStringRectWithFlags(
        text_, font_list_,
        GetColorProvider()->GetColor(kColorKoanPinnedTabSublabel),
        GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_LEFT);
  }

 private:
  std::u16string text_;
  gfx::FontList font_list_;
};

BEGIN_METADATA(KoanTabSublabel)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Text)
END_METADATA

// Zen's inline label editor ("Change Label…"): Enter applies, Esc or
// clicking elsewhere cancels, an empty label goes back to the page title.
class KoanTabLabelEditor : public views::Textfield,
                           public views::TextfieldController {
  METADATA_HEADER(KoanTabLabelEditor, views::Textfield)

 public:
  KoanTabLabelEditor(base::OnceCallback<void(bool, std::u16string)> on_done,
                     const std::u16string& text,
                     const gfx::FontList& font_list)
      : on_done_(std::move(on_done)) {
    SetController(this);
    SetFontList(font_list);
    SetText(text);
    GetViewAccessibility().SetName(u"Tab label");
  }

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed) {
      return false;
    }
    if (event.key_code() == ui::VKEY_RETURN) {
      Finish(/*apply=*/true);
      return true;
    }
    if (event.key_code() == ui::VKEY_ESCAPE) {
      Finish(/*apply=*/false);
      return true;
    }
    return false;
  }

  // views::View:
  void OnBlur() override {
    views::Textfield::OnBlur();
    Finish(/*apply=*/false);
  }

 private:
  void Finish(bool apply) {
    if (on_done_) {
      std::move(on_done_).Run(apply, std::u16string(GetText()));
    }
  }

  base::OnceCallback<void(bool, std::u16string)> on_done_;
};

BEGIN_METADATA(KoanTabLabelEditor)
END_METADATA

// Zen's pinned-tab decoration of one sidebar row (a TabView). For a pinned
// tab away from its pinned URL: the reset button over the favicon (hover
// shows the pinned page's icon and "Back to pinned url"; click goes back,
// Ctrl+click (Cmd+click on macOS) first moves the current page to a new tab).
// Also hosts the inline label editor.
class KoanTabRow : public views::Button {
  METADATA_HEADER(KoanTabRow, views::Button)

 public:
  KoanTabRow(TabView* tab_view, KoanTabSublabel* sublabel)
      : views::Button(base::BindRepeating(&KoanTabRow::OnPressed,
                                          base::Unretained(this))),
        tab_view_(tab_view),
        sublabel_(sublabel) {
    for (views::View* child : tab_view->children()) {
      if (views::IsViewClass<TabIcon>(child)) {
        icon_ = child;
      } else if (auto* title = views::AsViewClass<TabTitle>(child)) {
        title_ = title;
      }
    }
    // Like the row itself: reachable by screen readers, not a Tab stop.
    SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
    SetTooltipText(u"Reset and pin tab");
    GetViewAccessibility().SetName(u"Reset and pin tab");
    SetVisible(false);
    watcher_.Watch(tab_view);
    watcher_.Watch(sublabel);
    if (icon_) {
      watcher_.Watch(icon_);
      icon_layout_bounds_ = icon_->bounds();
    }
    if (title_) {
      watcher_.Watch(title_);
      title_layout_bounds_ = title_->bounds();
    }
    Rows()[tab_view->GetTabInterface()] = this;
  }

  ~KoanTabRow() override {
    // The tab may be gone already: look for the row, not the tab.
    base::EraseIf(Rows(),
                  [this](const auto& entry) { return entry.second == this; });
  }

  // The row's tab; null once the tab closed (its view animates out).
  const tabs::TabInterface* tab() const {
    return tab_view_ ? tab_view_->GetTabInterface() : nullptr;
  }

  // Whether the reset button is up (for tests: the view's Visible is the
  // same, but this also reads while the row is scrolled away).
  bool GetAwayFromPinnedUrl() const { return IsAwayFromPinnedUrl(tab()); }
  std::u16string GetPinnedUrl() const {
    const KoanTabState* state = GetTabState(tab());
    return state && state->pinned_url.is_valid()
               ? base::UTF8ToUTF16(state->pinned_url.spec())
               : std::u16string();
  }
  bool GetPinnedInSpace() const { return IsPinnedInSpace(tab()); }
  // What the tab shows, also while it isn't loaded.
  std::u16string GetUrl() const {
    return tab()
               ? base::UTF8ToUTF16(tab()->GetContents()->GetVisibleURL().spec())
               : std::u16string();
  }
  bool GetDiscarded() const {
    return tab() && tab()->GetContents()->WasDiscarded();
  }
  bool GetPending() const { return IsPendingTab(tab()); }

  bool CanRename() const {
    return tab() && title_ && title_->GetVisible() &&
           !title_->bounds().IsEmpty() && !tab()->IsPinned();
  }

  void Update() {
    const bool show = features::IsPinnedTabsEnabled() &&
                      IsPinnedInSpace(tab()) && IsAwayFromPinnedUrl(tab()) &&
                      CanRename() && !editor_;
    if (show && !layer()) {
      // Above the favicon, which may paint to a layer of its own. Only once
      // the button shows: most rows never need it.
      SetPaintToLayer();
      layer()->SetFillsBoundsOpaquely(false);
    }
    if (show != GetVisible()) {
      SetVisible(show);
    }
    UpdateBounds();
    UpdateSublabel();
    SchedulePaint();
  }

  void StartRenaming() {
    if (editor_ || !CanRename()) {
      return;
    }
    const KoanTabState* state = GetTabState(tab());
    had_custom_title_ = state && !state->custom_title.empty();
    original_title_ = title_->GetText();
    editor_ = tab_view_->AddChildView(std::make_unique<KoanTabLabelEditor>(
        base::BindOnce(&KoanTabRow::OnRenamed, weak_factory_.GetWeakPtr()),
        original_title_, title_->font_list()));
    const gfx::Rect title = title_->bounds();
    const int height =
        std::min(editor_->GetPreferredSize().height(), tab_view_->height());
    editor_->SetBounds(title.x() - 2, (tab_view_->height() - height) / 2,
                       title.width() + 2, height);
    editor_->SelectAll(/*reversed=*/true);
    editor_->RequestFocus();
    Update();
  }

  // views::Button:
  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdateSublabel();
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    separate_ = IsAccelDown(event);
    views::Button::OnMouseEntered(event);
    UpdateSublabel();
  }

  void OnMouseMoved(const ui::MouseEvent& event) override {
    separate_ = IsAccelDown(event);
    views::Button::OnMouseMoved(event);
    UpdateSublabel();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (!IsResetHovered()) {
      // The slash between the favicon and the label.
      flags.setColor(colors->GetColor(kColorKoanPinnedTabSlash));
      canvas->Save();
      canvas->sk_canvas()->translate(bounds.right() - kPinnedSlashWidth,
                                     bounds.CenterPoint().y());
      canvas->sk_canvas()->rotate(kPinnedSlashAngle);
      canvas->DrawRoundRect(
          gfx::RectF(-kPinnedSlashWidth / 2, -kPinnedSlashHeight / 2,
                     kPinnedSlashWidth, kPinnedSlashHeight),
          kPinnedSlashRadius, flags);
      canvas->Restore();
      return;
    }
    // Hovered: Zen's tinted button, rounded on the row's side, with the
    // pinned page's icon where the current one was (it is hidden meanwhile).
    const float radius = SidebarRowCornerRadius();
    flags.setColor(colors->GetColor(GetState() == STATE_PRESSED
                                        ? kColorKoanPinnedTabResetPressed
                                        : kColorKoanPinnedTabResetHovered));
    canvas->sk_canvas()->drawRRect(
        static_cast<SkRRect>(
            gfx::RRectF(bounds, radius, radius, 0, 0, 0, 0, radius, radius)),
        flags);
    const KoanTabState* state = GetTabState(tab());
    if (state && !state->pinned_icon.IsEmpty()) {
      const gfx::ImageSkia icon = state->pinned_icon.Rasterize(colors);
      gfx::Point center = icon_layout_bounds_.CenterPoint();
      center.Offset(-x(), -y());
      canvas->DrawImageInt(icon, center.x() - kPinnedIconSize / 2,
                           center.y() - kPinnedIconSize / 2);
    }
  }

  // Changes of the views the row watches (its tab view, the favicon, the
  // label and the sublabel).
  void OnWatchedBoundsChanged(views::View* observed) {
    if (observed == sublabel_) {
      return;
    }
    if (adjusting_) {
      return;
    }
    // A new layout of the row: keep the label clear of the button.
    if (observed == title_) {
      title_layout_bounds_ = title_->bounds();
    } else if (observed == icon_) {
      icon_layout_bounds_ = icon_->bounds();
    }
    UpdateBounds();
  }

  void OnWatchedVisibilityChanged(views::View* observed) {
    if (observed == title_) {
      Update();
    }
  }

  void OnWatchedViewDeleting(views::View* observed) {
    if (observed == icon_) {
      icon_ = nullptr;
    } else if (observed == title_) {
      title_ = nullptr;
    } else if (observed == sublabel_) {
      sublabel_ = nullptr;
    } else if (observed == tab_view_) {
      tab_view_ = nullptr;
    }
  }

 private:
  // views::Button is a ViewObserver already (with final overrides), so the
  // row watches other views through this.
  class Watcher : public views::ViewObserver {
   public:
    explicit Watcher(KoanTabRow* row) : row_(row) {}

    void Watch(views::View* view) { observations_.AddObservation(view); }

    // views::ViewObserver:
    void OnViewBoundsChanged(views::View* observed) override {
      row_->OnWatchedBoundsChanged(observed);
    }
    void OnViewVisibilityChanged(views::View* observed,
                                 views::View* starting_view,
                                 bool visible) override {
      row_->OnWatchedVisibilityChanged(observed);
    }
    void OnViewIsDeleting(views::View* observed) override {
      observations_.RemoveObservation(observed);
      row_->OnWatchedViewDeleting(observed);
    }

   private:
    const raw_ptr<KoanTabRow> row_;
    base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
        observations_{this};
  };

  void OnPressed(const ui::Event& event) {
    if (KoanPinnedTabController* controller =
            KoanPinnedTabController::ForTab(tab())) {
      controller->OnResetButtonPressed(const_cast<tabs::TabInterface*>(tab()),
                                       IsAccelDown(event));
    }
  }

  void OnRenamed(bool apply, std::u16string text) {
    base::TrimWhitespace(text, base::TRIM_ALL, &text);
    KoanPinnedTabController* controller =
        KoanPinnedTabController::ForTab(tab());
    // Like Zen: a new (or kept custom) label sticks, anything else goes
    // back to the page title.
    if (apply && controller) {
      const bool changed = text != original_title_ || had_custom_title_;
      controller->SetCustomTitle(const_cast<tabs::TabInterface*>(tab()),
                                 changed ? text : std::u16string());
    }
    // Not from inside the editor's own event handling.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&KoanTabRow::RemoveEditor, weak_factory_.GetWeakPtr()));
  }

  void RemoveEditor() {
    KoanTabLabelEditor* editor = editor_;
    editor_ = nullptr;
    if (editor && tab_view_) {
      tab_view_->RemoveChildViewT(editor);
    }
    Update();
  }

  void UpdateBounds() {
    if (!tab_view_ || !icon_) {
      return;
    }
    const int center = icon_layout_bounds_.CenterPoint().x();
    const int left = std::max(0, center - kPinnedResetButtonHalfWidth);
    SetBounds(left, kPinnedResetButtonInset,
              center + kPinnedResetButtonHalfWidth - left,
              std::max(0, tab_view_->height() - 2 * kPinnedResetButtonInset));
    UpdateSublabel();
  }

  // Places the label, favicon and sublabel for the current state: the label
  // clear of the button, and while it is hovered the favicon hidden and the
  // label moved up over "Back to pinned url". The row's own layout puts
  // them back on its next pass, and this moves them again.
  void UpdateSublabel() {
    if (!title_ || !icon_ || !sublabel_) {
      return;
    }
    const bool hovered = GetVisible() && IsResetHovered();
    gfx::Rect title = title_layout_bounds_;
    if (GetVisible()) {
      const int left = bounds().right() + kPinnedResetLabelGap;
      title.Inset(gfx::Insets().set_left(std::max(0, left - title.x())));
    }
    if (hovered) {
      title.Offset(0, -kPinnedSublabelRise);
    }
    const gfx::Rect icon =
        hovered ? gfx::Rect(icon_layout_bounds_.origin(), gfx::Size())
                : icon_layout_bounds_;
    adjusting_ = true;
    title_->SetBoundsRect(title);
    icon_->SetBoundsRect(icon);
    adjusting_ = false;
    sublabel_->SetBounds(title.x(), title.bottom() - 1, title.width(),
                         kPinnedSublabelHeight);
    const gfx::FontList& font_list = title_->font_list();
    sublabel_->SetStyle(separate_ ? kSeparateFromPinnedTab : kBackToPinnedUrl,
                        font_list.DeriveWithSizeDelta(kPinnedSublabelFontSize -
                                                      font_list.GetFontSize()));
    sublabel_->SetVisible(hovered);
  }

  // Ctrl, or Cmd on macOS (Zen's "Accel").
  static bool IsAccelDown(const ui::Event& event) {
    return event.flags() & ui::EF_PLATFORM_ACCELERATOR;
  }

  bool IsResetHovered() const {
    return GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
  }

  raw_ptr<TabView> tab_view_;
  raw_ptr<KoanTabSublabel> sublabel_;
  raw_ptr<views::View> icon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<KoanTabLabelEditor> editor_ = nullptr;
  // Where the row's layout put the label and the favicon, before Koan moved
  // them.
  gfx::Rect title_layout_bounds_;
  gfx::Rect icon_layout_bounds_;
  bool adjusting_ = false;
  // Ctrl (Cmd) is down over the button: clicking separates the page.
  bool separate_ = false;
  std::u16string original_title_;
  bool had_custom_title_ = false;
  Watcher watcher_{this};

  base::WeakPtrFactory<KoanTabRow> weak_factory_{this};
};

BEGIN_METADATA(KoanTabRow)
ADD_READONLY_PROPERTY_METADATA(bool, AwayFromPinnedUrl)
ADD_READONLY_PROPERTY_METADATA(std::u16string, PinnedUrl)
ADD_READONLY_PROPERTY_METADATA(bool, PinnedInSpace)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Url)
ADD_READONLY_PROPERTY_METADATA(bool, Discarded)
ADD_READONLY_PROPERTY_METADATA(bool, Pending)
END_METADATA

// Applies an emoji from the icon picker's field.
class KoanIconField : public views::Textfield,
                      public views::TextfieldController {
  METADATA_HEADER(KoanIconField, views::Textfield)

 public:
  explicit KoanIconField(base::RepeatingCallback<void(std::u16string)> apply)
      : apply_(std::move(apply)) {
    SetController(this);
    SetPlaceholderText(u"Type or paste an emoji");
    GetViewAccessibility().SetName(u"Icon");
  }

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& event) override {
    if (event.type() != ui::EventType::kKeyPressed ||
        event.key_code() != ui::VKEY_RETURN) {
      return false;
    }
    std::u16string icon;
    base::TrimWhitespace(GetText(), base::TRIM_ALL, &icon);
    if (!icon.empty()) {
      apply_.Run(std::move(icon));
    }
    return true;
  }

 private:
  base::RepeatingCallback<void(std::u16string)> apply_;
};

BEGIN_METADATA(KoanIconField)
END_METADATA

// The "Change Icon…" bubble. Owns its delegate and widget and deletes itself
// once closed.
class KoanIconPicker {
 public:
  static void Show(views::View* anchor, std::unique_ptr<views::View> contents) {
    auto* picker = new KoanIconPicker();
    picker->delegate_ = std::make_unique<views::BubbleDialogDelegate>(
        anchor, views::BubbleBorder::LEFT_CENTER);
    views::BubbleDialogDelegate* delegate = picker->delegate_.get();
    delegate->SetTitle(u"Change Icon");
    delegate->SetShowTitle(false);
    delegate->SetShowCloseButton(false);
    delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    delegate->SetContentsView(std::move(contents));
    picker->widget_ = views::BubbleDialogDelegate::CreateBubble(
        delegate,
        base::BindOnce(&KoanIconPicker::OnClosed, base::Unretained(picker)));
    picker->widget_->Show();
  }

 private:
  KoanIconPicker() = default;

  void OnClosed(views::Widget::ClosedReason reason) {
    // Not from inside the widget's own close.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

  // Declared first so the widget goes before its delegate.
  std::unique_ptr<views::BubbleDialogDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
};

void SetIcon(tabs::TabHandle handle, std::u16string icon) {
  tabs::TabInterface* tab = handle.Get();
  if (KoanPinnedTabController* controller =
          KoanPinnedTabController::ForTab(tab)) {
    controller->SetCustomIcon(tab, std::move(icon));
  }
}

}  // namespace

void UpdateTabRow(const tabs::TabInterface* tab) {
  if (KoanTabRow* row = RowFor(tab)) {
    row->Update();
  }
}

bool CanRenameTabRow(const tabs::TabInterface* tab) {
  KoanTabRow* row = RowFor(tab);
  return row && row->CanRename();
}

bool StartRenamingTabRow(const tabs::TabInterface* tab) {
  KoanTabRow* row = RowFor(tab);
  if (!row || !row->CanRename()) {
    return false;
  }
  row->StartRenaming();
  return true;
}

views::View* GetTabRowView(const tabs::TabInterface* tab) {
  KoanTabRow* row = RowFor(tab);
  return row ? row->parent() : nullptr;
}

void ShowTabIconPicker(views::View* anchor, tabs::TabInterface* tab) {
  if (!anchor || !anchor->GetWidget()) {
    return;
  }
  const tabs::TabHandle handle = tab->GetHandle();
  auto contents = std::make_unique<views::BoxLayoutView>();
  contents->SetOrientation(views::BoxLayout::Orientation::kVertical);
  contents->SetBetweenChildSpacing(kIconPickerSpacing);
  contents->AddChildView(
      std::make_unique<KoanIconField>(base::BindRepeating(&SetIcon, handle)));
  views::BoxLayoutView* row = nullptr;
  size_t column = 0;
  for (const char16_t* choice : kIconChoices) {
    if (column++ % kIconPickerColumns == 0) {
      row = contents->AddChildView(std::make_unique<views::BoxLayoutView>());
      row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    }
    const std::u16string icon(choice);
    auto* button = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SetIcon, handle, icon), icon));
    button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    button->SetPreferredSize(
        gfx::Size(kIconPickerButtonSize, kIconPickerButtonSize));
    button->GetViewAccessibility().SetName(icon);
  }
  // Zen offers "none" once the tab has an icon of its own.
  const KoanTabState* state = GetTabState(tab);
  if (state && !state->custom_icon.empty()) {
    contents->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&SetIcon, handle, std::u16string()),
        u"Remove Icon"));
  }

  KoanIconPicker::Show(anchor, std::move(contents));
}

void OnTabViewCreated(TabView* tab_view) {
  // The sidebar's rows only; horizontal tabs stay Chrome's.
  if (!features::IsPinnedTabsEnabled() ||
      tab_view->collection_node()->orientation() !=
          TabStripOrientation::kVertical) {
    return;
  }
  auto* sublabel = tab_view->AddChildView(std::make_unique<KoanTabSublabel>());
  tab_view->AddChildView(std::make_unique<KoanTabRow>(tab_view, sublabel))
      ->Update();
}

}  // namespace koan
