// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_agent_indicator.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/i18n/rtl.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/agent/koan_agent_forwarder.h"
#include "koan/browser/agent/koan_agent_page_surfaces.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_space_bar.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_vertical_tabs_policy.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/base_window.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

using koan::agent::AgentControlService;
using koan::agent::ChipContent;
using koan::agent::ConnectedClient;
using koan::agent::ControlledTab;
using koan::agent::HeldPrompt;
using koan::agent::IndicatorContent;
using koan::agent::IndicatorPrompt;
using koan::agent::IndicatorTab;
using koan::agent::MenuEntry;
using koan::agent::PillContent;

// The chip's menu: its "Go to" items, then Stop all agents.
constexpr int kStopAllCommand = 1;
constexpr int kFirstEntryCommand = 100;

// The agent's dot, the chip's height and insets, the corner dot's size, and
// the width of the border round a controlled page.
constexpr int kDotSize = 8;
constexpr int kChipHeight = 26;
constexpr int kChipPadding = 10;
constexpr int kChipGap = 6;
constexpr int kChipRowInsets = 6;
constexpr int kCornerDotSize = 28;
constexpr int kBorderWidth = 2;
constexpr int kBorderContrastWidth = 1;

// The buttons act from a fresh task: what they do changes the pill, which
// replaces this view, and a button must not be destroyed inside its own
// callback.
void Later(void (AgentControlService::*action)(const std::string&),
           const std::string& target_id) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](void (AgentControlService::*act)(const std::string&),
             const std::string& id) {
            if (AgentControlService* service = AgentControlService::Get()) {
              (service->*act)(id);
            }
          },
          action, target_id));
}

void PromptLater(void (AgentControlService::*action)(uint64_t), uint64_t id) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](void (AgentControlService::*act)(uint64_t), uint64_t prompt_id) {
            if (AgentControlService* service = AgentControlService::Get()) {
              (service->*act)(prompt_id);
            }
          },
          action, id));
}

void StopAllLater() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce([] {
        if (AgentControlService* service = AgentControlService::Get()) {
          service->StopAllAgents();
        }
      }));
}

// The chip's "Go to": the tab comes forward in its window, and the window to
// the front, as when the user clicks a tab.
void BringForward(content::WebContents* contents) {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  BrowserWindowInterface* window =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  if (!window) {
    return;
  }
  TabStripModel* strip = window->GetTabStripModel();
  const int index = strip->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab) {
    strip->ActivateTabAt(index);
  }
  if (ui::BaseWindow* base_window = window->GetWindow()) {
    base_window->Activate();
  }
}

// Whether `contents`, a tab of `strip`, is shown: the active tab, or a pane
// of the active split.
bool IsShownIn(const TabStripModel* strip, content::WebContents* contents) {
  const tabs::TabInterface* active = strip->GetActiveTab();
  const tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(contents);
  if (!active || !tab) {
    return false;
  }
  return tab == active ||
         (tab->GetSplit() && tab->GetSplit() == active->GetSplit());
}

// A child widget of the window, above the page.
std::unique_ptr<views::Widget> MakeChildWidget(BrowserView* browser_view,
                                               const char* name,
                                               bool accept_events) {
  views::Widget* browser_widget = browser_view->GetWidget();
  if (!browser_widget) {
    return nullptr;
  }
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_widget->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.accept_events = accept_events;
  params.name = name;
  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  return widget;
}

// Repaints every sidebar tab under `view` (their agent dot may have
// changed).
void RepaintTabViews(views::View* view) {
  if (views::IsViewClass<TabView>(view)) {
    view->SchedulePaint();
    return;
  }
  for (views::View* child : view->children()) {
    RepaintTabViews(child);
  }
}

// The agent's dot.
class KoanAgentDot : public views::View {
  METADATA_HEADER(KoanAgentDot, views::View)

 public:
  KoanAgentDot() {
    SetPreferredSize(gfx::Size(kDotSize, kDotSize));
    SetCanProcessEventsWithinSubtree(false);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(GetColorProvider()->GetColor(kColorKoanAgentAccent));
    canvas->DrawCircle(gfx::RectF(GetLocalBounds()).CenterPoint(),
                       std::min(width(), height()) / 2.f, flags);
  }
};

BEGIN_METADATA(KoanAgentDot)
END_METADATA

// The pill on a controlled tab.
class KoanAgentIndicatorView : public views::View {
  METADATA_HEADER(KoanAgentIndicatorView, views::View)

 public:
  explicit KoanAgentIndicatorView(const PillContent& content) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kToastPadding), kToastGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    label_ = AddChildView(std::make_unique<views::Label>(content.text));
    label_->SetEnabledColor(kColorKoanToastForeground);
    // The buttons act on the tab or the prompt they were shown for.
    const std::string target_id = content.target_id;
    if (content.prompt_id) {
      AddButton(
          u"Open", u"Show what the page asks",
          base::BindRepeating(&PromptLater, &AgentControlService::OpenPrompt,
                              content.prompt_id));
      AddButton(
          u"Dismiss", u"Say no to what the page asks",
          base::BindRepeating(&PromptLater, &AgentControlService::DismissPrompt,
                              content.prompt_id));
    }
    if (content.keep_paused) {
      AddButton(u"Keep paused",
                u"Keep the agent paused until you hand this tab back",
                base::BindRepeating(&Later, &AgentControlService::KeepPaused,
                                    target_id));
    }
    if (content.hand_back) {
      AddButton(u"Hand back", u"Let the agent continue in this tab",
                base::BindRepeating(&Later, &AgentControlService::HandBack,
                                    target_id));
    }
    if (content.take_back) {
      AddButton(u"Take back", u"Take this tab back from the agent",
                base::BindRepeating(&Later, &AgentControlService::TakeBack,
                                    target_id));
    }
    if (content.take_over) {
      AddButton(u"Take over", u"Pause the agent in this tab",
                base::BindRepeating(&Later, &AgentControlService::TakeOver,
                                    target_id));
    }
    AddButton(u"Stop", u"Stop all agents", base::BindRepeating(&StopAllLater));
    SetBackground(views::CreateRoundedRectBackground(
        kColorKoanToastBackgroundTop, ToastCornerRadius()));
    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  }

  // The countdown's next second: the same pill, its text only.
  void SetText(const std::u16string& text) { label_->SetText(text); }

  void AddButton(const std::u16string& label,
                 const std::u16string& accessible_name,
                 views::Button::PressedCallback callback) {
    auto* button = AddChildView(
        std::make_unique<views::LabelButton>(std::move(callback), label));
    button->SetEnabledTextColors(kColorKoanToastForeground);
    button->GetViewAccessibility().SetName(accessible_name);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size size = views::View::CalculatePreferredSize(available_size);
    size.set_height(kToastHeight);
    return size;
  }

 private:
  raw_ptr<views::Label> label_ = nullptr;
};

BEGIN_METADATA(KoanAgentIndicatorView)
END_METADATA

// The border round a controlled page. Its widget takes no events.
class KoanAgentBorderView : public views::View {
  METADATA_HEADER(KoanAgentBorderView, views::View)

 public:
  KoanAgentBorderView() { GetViewAccessibility().SetIsIgnored(true); }

  void SetRadius(float radius) {
    if (radius_ != radius) {
      radius_ = radius;
      SchedulePaint();
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // The accent, and inside it a line in the colour on the accent: one of
    // the two shows on any page (dark mode's accent is near white).
    const ui::ColorProvider* colors = GetColorProvider();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kBorderWidth);
    flags.setColor(colors->GetColor(kColorKoanAgentBorder));
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(kBorderWidth / 2.f);
    canvas->DrawRoundRect(bounds, std::max(0.f, radius_ - kBorderWidth / 2.f),
                          flags);
    flags.setStrokeWidth(kBorderContrastWidth);
    flags.setColor(colors->GetColor(kColorKoanAgentBorderContrast));
    bounds.Inset((kBorderWidth + kBorderContrastWidth) / 2.f);
    canvas->DrawRoundRect(
        bounds,
        std::max(0.f, radius_ - kBorderWidth - kBorderContrastWidth / 2.f),
        flags);
  }

 private:
  float radius_ = 0;
};

BEGIN_METADATA(KoanAgentBorderView)
END_METADATA

// The chip: the agent's dot and name; pressed, its menu.
class KoanAgentChipButton : public views::Button {
  METADATA_HEADER(KoanAgentChipButton, views::Button)

 public:
  KoanAgentChipButton(ui::ColorId background, ui::ColorId foreground) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kChipPadding), kChipGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    AddChildView(std::make_unique<KoanAgentDot>());
    label_ = AddChildView(std::make_unique<views::Label>());
    label_->SetEnabledColor(foreground);
    label_->SetElideBehavior(gfx::ELIDE_TAIL);
    label_->SetCanProcessEventsWithinSubtree(false);
    layout->SetFlexForView(label_, 1);
    SetBackground(
        views::CreateRoundedRectBackground(background, kChipHeight / 2));
  }

  // `text` empty: the dot only.
  void SetContent(const std::u16string& text,
                  const std::u16string& accessible_name) {
    label_->SetText(text);
    label_->SetVisible(!text.empty());
    SetTooltipText(accessible_name);
    GetViewAccessibility().SetName(accessible_name);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size size = views::Button::CalculatePreferredSize(available_size);
    size.set_height(kChipHeight);
    return size;
  }

  // The dot alone: a narrow sidebar elides the name.
  gfx::Size GetMinimumSize() const override {
    return gfx::Size(2 * kChipPadding + kDotSize, kChipHeight);
  }

 private:
  raw_ptr<views::Label> label_ = nullptr;
};

BEGIN_METADATA(KoanAgentChipButton)
END_METADATA

// The chip's row in the sidebar, above the Spaces' bottom bar: the chip at
// its leading edge, shrinking (its name elided) in a narrow sidebar.
class KoanAgentChipRow : public views::View {
  METADATA_HEADER(KoanAgentChipRow, views::View)

 public:
  KoanAgentChipRow() {
    SetLayoutManager(std::make_unique<views::FlexLayout>())
        ->SetOrientation(views::LayoutOrientation::kHorizontal)
        .SetMainAxisAlignment(views::LayoutAlignment::kStart)
        .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
        .SetInteriorMargin(gfx::Insets::VH(kChipRowInsets / 2, kChipRowInsets));
    chip_ = AddChildView(std::make_unique<KoanAgentChipButton>(
        kColorKoanAgentChip, kColorKoanAgentChipForeground));
    chip_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                                 views::MaximumFlexSizeRule::kPreferred));
  }

  KoanAgentChipButton* chip() { return chip_; }

  // Runs when the row is laid out somewhere else (or gets its first size):
  // whether the chip can be seen is known only then.
  void set_on_bounds_changed(base::RepeatingClosure callback) {
    on_bounds_changed_ = std::move(callback);
  }

  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override {
    if (on_bounds_changed_) {
      on_bounds_changed_.Run();
    }
  }

 private:
  raw_ptr<KoanAgentChipButton> chip_ = nullptr;
  base::RepeatingClosure on_bounds_changed_;
};

BEGIN_METADATA(KoanAgentChipRow)
END_METADATA

// The chip's stand-in while the sidebar is hidden: the dot (and how many
// tabs wait) in a pill in the page's corner, in the toast's colours.
class KoanAgentCornerDot : public KoanAgentChipButton {
  METADATA_HEADER(KoanAgentCornerDot, KoanAgentChipButton)

 public:
  KoanAgentCornerDot()
      : KoanAgentChipButton(kColorKoanToastBackgroundTop,
                            kColorKoanToastForeground) {}

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size size =
        KoanAgentChipButton::CalculatePreferredSize(available_size);
    size.SetToMax(gfx::Size(kCornerDotSize, kCornerDotSize));
    return size;
  }
};

BEGIN_METADATA(KoanAgentCornerDot)
END_METADATA

}  // namespace

KoanAgentIndicator::KoanAgentIndicator(BrowserView* browser_view)
    : browser_view_(browser_view) {
  browser_view_->browser()->tab_strip_model()->AddObserver(this);
  if (AgentControlService* service = AgentControlService::GetOrCreate()) {
    observation_.Observe(service);
  }
  // No toast for what was there before the window.
  const Snapshot snapshot = TakeSnapshot();
  events_ = koan::agent::EventStateFor(snapshot.clients, snapshot.tabs);
  Update();
}

KoanAgentIndicator::~KoanAgentIndicator() {
  OnBrowserViewDestroying();
}

void KoanAgentIndicator::OnAddedToWidget() {
  Update();
}

void KoanAgentIndicator::OnBrowserViewDestroying() {
  if (torn_down_) {
    return;
  }
  torn_down_ = true;
  observation_.Reset();
  browser_view_->browser()->tab_strip_model()->RemoveObserver(this);
  menu_runner_.reset();
  pill_.reset();
  border_.reset();
  corner_.reset();
  if (views::View* chip = chip_.view()) {
    chip_.SetView(nullptr);
    chip->parent()->RemoveChildViewT(chip);
  }
  marked_.clear();
  pill_contents_ = nullptr;
}

bool KoanAgentIndicator::IsAgentTab(
    const content::WebContents* contents) const {
  return marked_.contains(contents);
}

bool KoanAgentIndicator::IsShowingForTesting() const {
  return pill_ && pill_->IsVisible();
}

KoanAgentIndicator::Snapshot KoanAgentIndicator::TakeSnapshot() const {
  Snapshot snapshot;
  AgentControlService* service = AgentControlService::Get();
  if (!service) {
    return snapshot;
  }
  for (const ConnectedClient& client : service->clients()) {
    snapshot.clients.push_back(
        {.client_id = client.client_id, .label = client.label});
  }
  const TabStripModel* strip = browser_view_->browser()->tab_strip_model();
  for (const ControlledTab& controlled : service->controlled_tabs()) {
    IndicatorTab tab;
    tab.target_id = controlled.target_id;
    tab.label = controlled.label;
    tab.state = controlled.state;
    tab.agent_owned = controlled.agent_owned;
    tab.resume_asked = controlled.resume_asked;
    tab.auto_resumes = controlled.auto_resumes;
    tab.attached = controlled.attached;
    tab.attached_seq = controlled.attached_seq;
    if (controlled.countdown_ends) {
      tab.countdown_seconds = base::ClampCeil(
          (*controlled.countdown_ends - base::TimeTicks::Now()).InSecondsF());
    }
    if (content::WebContents* contents = controlled.contents) {
      tab.title = contents->GetTitle();
      tab.in_window =
          strip->GetIndexOfWebContents(contents) != TabStripModel::kNoTab;
      tab.shown = tab.in_window && IsShownIn(strip, contents);
      if (tab.in_window) {
        snapshot.tabs_in_window.insert(contents);
      }
      snapshot.tab_contents[tab.target_id] = contents;
    }
    snapshot.tabs.push_back(std::move(tab));
  }
  for (const HeldPrompt& held : service->user_prompts()) {
    IndicatorPrompt prompt;
    prompt.prompt = held.prompt;
    if (content::WebContents* contents = held.contents) {
      prompt.shown =
          strip->GetIndexOfWebContents(contents) != TabStripModel::kNoTab &&
          IsShownIn(strip, contents);
      snapshot.prompt_contents[held.prompt.id] = contents;
    }
    snapshot.prompts.push_back(std::move(prompt));
  }
  return snapshot;
}

void KoanAgentIndicator::OnAgentControlChanged() {
  Update();
}

void KoanAgentIndicator::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // A tab shown, inserted (an agent's new tab is registered before it is in
  // a window) or gone.
  if (selection.active_tab_changed() ||
      change.type() != TabStripModelChange::kSelectionOnly) {
    Update();
  }
}

void KoanAgentIndicator::OnSplitTabChanged(const SplitTabChange& change) {
  Update();
}

void KoanAgentIndicator::Update() {
  if (torn_down_) {
    return;
  }
  const Snapshot snapshot = TakeSnapshot();
  IndicatorContent content = koan::agent::IndicatorFor(
      snapshot.clients, snapshot.tabs, snapshot.prompts,
      AgentControlService::Get() &&
          AgentControlService::Get()->keeping_display_awake());
  ShowEventToasts(koan::agent::EventStateFor(snapshot.clients, snapshot.tabs));

  std::set<const content::WebContents*> marked;
  if (content.connected) {
    marked = snapshot.tabs_in_window;
  }
  if (marked != marked_) {
    marked_ = std::move(marked);
    RepaintTabs();
  }

  pill_contents_ = nullptr;
  if (content.pill) {
    const auto& contents_of = snapshot.tab_contents;
    if (content.pill->prompt_id) {
      auto it = snapshot.prompt_contents.find(content.pill->prompt_id);
      pill_contents_ =
          it == snapshot.prompt_contents.end() ? nullptr : it->second;
    } else if (auto it = contents_of.find(content.pill->target_id);
               it != contents_of.end()) {
      pill_contents_ = it->second;
    }
    ShowPill(*content.pill);
  } else {
    HidePill();
  }
  content_ = std::move(content);
  UpdateBorder();
  UpdateChip();
  UpdateCornerDot();
}

void KoanAgentIndicator::ShowEventToasts(const koan::agent::EventState& state) {
  const koan::agent::EventToasts toasts =
      koan::agent::EventToastsFor(events_, state);
  events_ = state;
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  KoanToast* toast = controller ? controller->toast() : nullptr;
  if (!toast) {
    return;
  }
  // A pause in this window says more than an upload to one of its tabs,
  // which says more than a connection; a connection is told in the front
  // window only.
  std::u16string text = toasts.pause.empty() ? toasts.upload : toasts.pause;
  if (text.empty() && !toasts.connection.empty() &&
      GetLastActiveBrowserWindowInterfaceWithAnyProfile() ==
          browser_view_->browser()) {
    text = toasts.connection;
  }
  if (!text.empty()) {
    toast->Show(text);
  }
}

void KoanAgentIndicator::ShowPill(const PillContent& content) {
  if (pill_) {
    // From one second of the countdown to the next only the text changes:
    // the pill stays (a click on Keep paused is not lost to a new view), and
    // a screen reader hears the countdown once.
    if (content_.pill && content.countdown_seconds &&
        content_.pill->countdown_seconds) {
      PillContent same = content;
      same.text = content_.pill->text;
      same.countdown_seconds = content_.pill->countdown_seconds;
      if (same == *content_.pill) {
        static_cast<KoanAgentIndicatorView*>(pill_->GetContentsView())
            ->SetText(content.text);
        PlaceInCorner(pill_.get());
        return;
      }
    }
    if (!content_.pill || content != *content_.pill) {
      pill_->SetContentsView(std::make_unique<KoanAgentIndicatorView>(content));
      pill_->GetContentsView()->GetViewAccessibility().AnnounceText(
          content.text);
    }
    PlaceInCorner(pill_.get());
    return;
  }
  pill_ = MakeChildWidget(browser_view_, "KoanAgentIndicator",
                          /*accept_events=*/true);
  if (!pill_) {
    return;
  }
  pill_->SetContentsView(std::make_unique<KoanAgentIndicatorView>(content));
  PlaceInCorner(pill_.get());
  pill_->Show();
  pill_->GetContentsView()->GetViewAccessibility().AnnounceText(content.text);
}

void KoanAgentIndicator::HidePill() {
  pill_.reset();
}

void KoanAgentIndicator::UpdateBorder() {
  MultiContentsView* multi = browser_view_->multi_contents_view();
  ContentsContainerView* container =
      pill_contents_ && multi
          ? multi->GetContentsContainerViewFor(pill_contents_)
          : nullptr;
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (!container || !browser_widget || !container->IsDrawn()) {
    border_.reset();
    return;
  }
  if (!border_) {
    // Under the pill (made first when both are new), and the page's events
    // go through it.
    border_ = MakeChildWidget(browser_view_, "KoanAgentBorder",
                              /*accept_events=*/false);
    if (!border_) {
      return;
    }
    border_->SetContentsView(std::make_unique<KoanAgentBorderView>());
    border_->ShowInactive();
    if (pill_) {
      // Keep the pill above the border.
      pill_->StackAboveWidget(border_.get());
    }
  }
  static_cast<KoanAgentBorderView*>(border_->GetContentsView())
      ->SetRadius(UseContentCard(browser_view_)
                      ? ContentCardCornerRadius()
                      : multi->GetBackgroundRadii().upper_left());
  border_->SetBounds(views::View::ConvertRectToTarget(
      container, browser_widget->GetRootView(), container->GetLocalBounds()));
}

void KoanAgentIndicator::InstallChip() {
  if (chip_.view()) {
    return;
  }
  // At the bottom of the sidebar's column, above the Spaces' bottom bar (as
  // the media cards, KoanMediaController::InstallView()), once the window and
  // its Spaces are set up.
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  views::View* column = browser_view_->GetWidget() && controller
                            ? GetSidebarContentColumn(browser_view_)
                            : nullptr;
  if (!column) {
    return;
  }
  const views::View* bar = controller && controller->workspaces()
                               ? controller->workspaces()->space_bar()
                               : nullptr;
  const size_t index = bar && bar->parent() == column
                           ? column->GetIndexOf(bar).value()
                           : column->children().size();
  auto* row =
      column->AddChildViewAt(std::make_unique<KoanAgentChipRow>(), index);
  KoanAgentChipButton* chip = row->chip();
  chip->SetCallback(base::BindRepeating(&KoanAgentIndicator::ShowMenu,
                                        weak_factory_.GetWeakPtr(),
                                        base::Unretained(chip)));
  // Laid out with the column, not the browser view (no
  // OnBrowserViewLayout()): the corner dot, put up while the new row had no
  // size yet, goes once it has one.
  row->set_on_bounds_changed(base::BindRepeating(
      &KoanAgentIndicator::ScheduleLayoutUpdate, weak_factory_.GetWeakPtr()));
  chip_.SetView(row);
}

// The chip and the corner dot are hidden rather than destroyed until the
// window goes: their menu runs inside their button's callback (a nested loop
// on the Mac), where what it does (Go to, Stop) updates the indicator.
void KoanAgentIndicator::UpdateChip() {
  if (!content_.connected) {
    if (views::View* chip = chip_.view()) {
      chip->SetVisible(false);
    }
    return;
  }
  InstallChip();
  if (auto* row = static_cast<KoanAgentChipRow*>(chip_.view())) {
    row->chip()->SetContent(content_.chip.text, content_.chip.accessible_name);
    row->SetVisible(true);
  }
}

void KoanAgentIndicator::UpdateCornerDot() {
  // The chip can be seen while it is drawn in a sidebar compact mode doesn't
  // hide.
  const KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  const KoanCompactModeController* compact =
      controller ? controller->compact_mode() : nullptr;
  const views::View* chip = chip_.view();
  const bool chip_seen = chip && chip->IsDrawn() && !chip->size().IsEmpty() &&
                         !(compact && compact->hides_sidebar());
  if (!content_.connected || content_.pill || chip_seen) {
    if (corner_) {
      corner_->GetContentsView()->SetVisible(false);
      corner_->Hide();
    }
    return;
  }
  // The dot, with how many tabs wait.
  const ChipContent& content = content_.chip;
  const std::u16string text = content.waiting
                                  ? base::NumberToString16(content.waiting)
                                  : std::u16string();
  if (!corner_) {
    corner_ = MakeChildWidget(browser_view_, "KoanAgentCornerDot",
                              /*accept_events=*/true);
    if (!corner_) {
      return;
    }
    auto* dot =
        corner_->SetContentsView(std::make_unique<KoanAgentCornerDot>());
    dot->SetCallback(base::BindRepeating(&KoanAgentIndicator::ShowMenu,
                                         weak_factory_.GetWeakPtr(),
                                         base::Unretained(dot)));
    dot->SetContent(text, content.accessible_name);
    PlaceInCorner(corner_.get());
    corner_->Show();
    return;
  }
  views::View* dot = corner_->GetContentsView();
  static_cast<KoanAgentCornerDot*>(dot)->SetContent(text,
                                                    content.accessible_name);
  dot->SetVisible(true);
  PlaceInCorner(corner_.get());
  if (!corner_->IsVisible()) {
    corner_->Show();
  }
}

void KoanAgentIndicator::ScheduleLayoutUpdate() {
  if (layout_update_pending_) {
    return;
  }
  layout_update_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KoanAgentIndicator> indicator) {
                       if (!indicator || indicator->torn_down_) {
                         return;
                       }
                       indicator->layout_update_pending_ = false;
                       // The tab strip may have been rebuilt (a tab strip
                       // mode change).
                       indicator->UpdateChip();
                       indicator->UpdateCornerDot();
                     },
                     weak_factory_.GetWeakPtr()));
}

void KoanAgentIndicator::RepaintTabs() {
  if (views::View* strip = browser_view_->tab_strip_view()) {
    RepaintTabViews(strip);
  }
}

void KoanAgentIndicator::ShowMenu(views::View* anchor) {
  if (torn_down_ || (menu_runner_ && menu_runner_->IsRunning()) ||
      !anchor->GetWidget()) {
    return;
  }
  const Snapshot snapshot = TakeSnapshot();
  menu_entries_ = koan::agent::MenuFor(snapshot.tabs, snapshot.prompts);
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < menu_entries_.size(); ++i) {
    menu_model_->AddItem(kFirstEntryCommand + static_cast<int>(i),
                         menu_entries_[i].label);
  }
  if (!menu_entries_.empty()) {
    menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  menu_model_->AddItem(kStopAllCommand, u"Stop all agents");
  menu_runner_ =
      std::make_unique<views::MenuRunner>(menu_model_.get(), /*run_types=*/0);
  gfx::Rect bounds = anchor->GetLocalBounds();
  views::View::ConvertRectToScreen(anchor, &bounds);
  menu_runner_->RunMenuAt(anchor->GetWidget(), nullptr, bounds,
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kMouse);
}

void KoanAgentIndicator::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kStopAllCommand) {
    StopAllLater();
    return;
  }
  if (command_id < kFirstEntryCommand ||
      static_cast<size_t>(command_id - kFirstEntryCommand) >=
          menu_entries_.size()) {
    return;
  }
  const MenuEntry entry = menu_entries_[command_id - kFirstEntryCommand];
  AgentControlService* service = AgentControlService::Get();
  if (!service) {
    return;
  }
  // Looked up again: the tab or the prompt may have gone while the menu was
  // open. A prompt's tab comes forward, where the pill shows Open and
  // Dismiss.
  if (entry.prompt_id) {
    for (const HeldPrompt& held : service->user_prompts()) {
      if (held.prompt.id == entry.prompt_id && held.contents) {
        BringForward(held.contents);
        return;
      }
    }
    return;
  }
  for (const ControlledTab& tab : service->controlled_tabs()) {
    if (tab.target_id == entry.target_id && tab.contents) {
      BringForward(tab.contents);
      return;
    }
  }
}

void KoanAgentIndicator::OnBrowserViewLayout() {
  if (torn_down_) {
    return;
  }
  if (pill_) {
    PlaceInCorner(pill_.get());
  }
  if (corner_ && corner_->IsVisible()) {
    PlaceInCorner(corner_.get());
  }
  UpdateBorder();
  // Whether the sidebar's chip shows depends on this layout: after it.
  if (content_.connected) {
    ScheduleLayoutUpdate();
  }
}

void KoanAgentIndicator::PlaceInCorner(views::Widget* widget) {
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (!widget || !browser_widget || !widget->GetContentsView()) {
    return;
  }
  const gfx::Size size = widget->GetContentsView()->GetPreferredSize();
  const gfx::Size root = browser_widget->GetRootView()->size();
  // The bottom corner away from the sidebar, on screen (as the toast's top
  // corner), so the two never overlap.
  const bool sidebar_on_screen_right =
      IsSidebarOnRight(browser_view_) != base::i18n::IsRTL();
  const int x = sidebar_on_screen_right
                    ? kToastMargin
                    : root.width() - kToastMargin - size.width();
  const int y = root.height() - kToastMargin - size.height();
  widget->SetBounds(gfx::Rect(gfx::Point(x, y), size));
}

}  // namespace koan
