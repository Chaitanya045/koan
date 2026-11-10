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
#include "chrome/browser/ui/views/frame/browser_view.h"
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

class KoanAgentIndicatorView : public views::View {
  METADATA_HEADER(KoanAgentIndicatorView, views::View)

 public:
  explicit KoanAgentIndicatorView(const std::u16string& text) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, kToastPadding), kToastGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    label_ = AddChildView(std::make_unique<views::Label>(text));
    label_->SetEnabledColor(kColorKoanToastForeground);
    auto* stop = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating([] {
          if (AgentControlService* service = AgentControlService::Get()) {
            service->StopAllAgents();
          }
        }),
        u"Stop"));
    stop->SetEnabledTextColors(kColorKoanToastForeground);
    stop->GetViewAccessibility().SetName(u"Stop all agents");
    SetBackground(views::CreateRoundedRectBackground(
        kColorKoanToastBackgroundTop, ToastCornerRadius()));
    GetViewAccessibility().SetRole(ax::mojom::Role::kStatus);
  }

  void SetText(const std::u16string& text) {
    label_->SetText(text);
    GetViewAccessibility().AnnounceText(text);
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

bool KoanAgentIndicator::IsShowingForTesting() const {
  return widget_ && widget_->IsVisible();
}

void KoanAgentIndicator::OnAgentControlChanged() {
  AgentControlService* service = AgentControlService::Get();
  const std::u16string text =
      service ? TextFor(service->clients()) : std::u16string();
  if (text.empty()) {
    Hide();
  } else {
    Show(text);
  }
}

void KoanAgentIndicator::Show(const std::u16string& text) {
  if (widget_) {
    static_cast<KoanAgentIndicatorView*>(widget_->GetContentsView())
        ->SetText(text);
    OnBrowserViewLayout();
    return;
  }
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
  widget_->SetContentsView(std::make_unique<KoanAgentIndicatorView>(text));
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
