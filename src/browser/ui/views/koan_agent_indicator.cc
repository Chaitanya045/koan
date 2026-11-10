// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_agent_indicator.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "koan/browser/agent/koan_agent_forwarder.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

using koan::agent::AgentControlService;
using koan::agent::ConnectedClient;
using koan::agent::ControlledTab;
using koan::agent::TabState;

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

void StopAllLater() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce([] {
        if (AgentControlService* service = AgentControlService::Get()) {
          service->StopAllAgents();
        }
      }));
}

class KoanAgentIndicatorView : public views::View {
  METADATA_HEADER(KoanAgentIndicatorView, views::View)

 public:
  explicit KoanAgentIndicatorView(const KoanAgentIndicator::Content& content) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kToastPadding), kToastGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    label_ = AddChildView(std::make_unique<views::Label>(content.text));
    label_->SetEnabledColor(kColorKoanToastForeground);
    // Hand back and Take back act on the tab they were shown for.
    const std::string target_id = content.target_id;
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
    auto* stop = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&StopAllLater), u"Stop"));
    stop->SetEnabledTextColors(kColorKoanToastForeground);
    stop->GetViewAccessibility().SetName(u"Stop all agents");
    SetBackground(views::CreateRoundedRectBackground(
        kColorKoanToastBackgroundTop, ToastCornerRadius()));
    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  }

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

}  // namespace

KoanAgentIndicator::KoanAgentIndicator(BrowserView* browser_view)
    : browser_view_(browser_view) {
  if (AgentControlService* service = AgentControlService::GetOrCreate()) {
    observation_.Observe(service);
    OnAgentControlChanged();
  }
}

KoanAgentIndicator::~KoanAgentIndicator() {
  Hide();
}

// static
std::u16string KoanAgentIndicator::TextFor(
    const std::vector<ConnectedClient>& clients) {
  if (clients.empty()) {
    return std::u16string();
  }
  // The same client on two connections is one agent.
  std::vector<std::string> ids;
  for (const ConnectedClient& client : clients) {
    if (std::ranges::find(ids, client.client_id) == ids.end()) {
      ids.push_back(client.client_id);
    }
  }
  if (ids.size() > 1) {
    return base::StrCat(
        {u"● ", base::NumberToString16(ids.size()), u" agents connected"});
  }
  const std::string& label = clients.front().label;
  return base::StrCat({u"● ",
                       label.empty() ? u"An agent" : base::UTF8ToUTF16(label),
                       u" is connected"});
}

// static
KoanAgentIndicator::Content KoanAgentIndicator::ContentFor(
    const std::vector<ConnectedClient>& clients,
    const std::vector<ControlledTab>& tabs_in_window) {
  Content content;
  content.text = TextFor(clients);
  // A paused tab first: the user has something to decide.
  const ControlledTab* paused = nullptr;
  const ControlledTab* controlled = nullptr;
  for (const ControlledTab& tab : tabs_in_window) {
    if (tab.state == TabState::kPaused && !paused) {
      paused = &tab;
    } else if (!tab.agent_owned && !controlled) {
      controlled = &tab;
    }
  }
  const ControlledTab* tab = paused ? paused : controlled;
  if (!tab) {
    return content;
  }
  const std::u16string agent =
      tab->label.empty() ? u"The agent" : base::UTF8ToUTF16(tab->label);
  content.target_id = tab->target_id;
  content.take_back = !tab->agent_owned;
  if (paused) {
    content.hand_back = true;
    content.text =
        base::StrCat({u"● ", agent,
                      tab->resume_asked ? u" asks to continue in this tab"
                                        : u" paused: you used its tab"});
  } else {
    content.text = base::StrCat({u"● ", agent,
                                 tab->state == TabState::kReadable
                                     ? u" can read a tab of yours"
                                     : u" is controlling a tab of yours"});
  }
  return content;
}

bool KoanAgentIndicator::IsShowingForTesting() const {
  return widget_ && widget_->IsVisible();
}

void KoanAgentIndicator::OnAgentControlChanged() {
  AgentControlService* service = AgentControlService::Get();
  if (!service) {
    Hide();
    return;
  }
  // The tabs of this window only: each window shows its own.
  std::vector<ControlledTab> tabs;
  const TabStripModel* strip = browser_view_->browser()->tab_strip_model();
  for (ControlledTab& tab : service->controlled_tabs()) {
    if (tab.contents &&
        strip->GetIndexOfWebContents(tab.contents) != TabStripModel::kNoTab) {
      tabs.push_back(std::move(tab));
    }
  }
  const Content content = ContentFor(service->clients(), tabs);
  if (content.text.empty()) {
    Hide();
  } else {
    Show(content);
  }
}

void KoanAgentIndicator::Show(const Content& content) {
  if (widget_) {
    if (content != shown_) {
      shown_ = content;
      widget_->SetContentsView(
          std::make_unique<KoanAgentIndicatorView>(content));
      widget_->GetContentsView()->GetViewAccessibility().AnnounceText(
          content.text);
    }
    OnBrowserViewLayout();
    return;
  }
  shown_ = content;
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (!browser_widget) {
    return;
  }
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_widget->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.name = "KoanAgentIndicator";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  widget_->SetContentsView(std::make_unique<KoanAgentIndicatorView>(content));
  OnBrowserViewLayout();
  widget_->Show();
}

void KoanAgentIndicator::Hide() {
  widget_.reset();
}

void KoanAgentIndicator::OnBrowserViewLayout() {
  views::Widget* browser_widget = browser_view_->GetWidget();
  if (!widget_ || !browser_widget) {
    return;
  }
  const gfx::Size size = widget_->GetContentsView()->GetPreferredSize();
  const gfx::Size root = browser_widget->GetRootView()->size();
  // The bottom corner away from the sidebar, on screen (as the toast's top
  // corner), so the two never overlap.
  const bool sidebar_on_screen_right =
      IsSidebarOnRight(browser_view_) != base::i18n::IsRTL();
  const int x = sidebar_on_screen_right
                    ? kToastMargin
                    : root.width() - kToastMargin - size.width();
  const int y = root.height() - kToastMargin - size.height();
  widget_->SetBounds(gfx::Rect(gfx::Point(x, y), size));
}

}  // namespace koan
