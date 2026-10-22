// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_creation.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/path_effect.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_emoji_picker.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_space_menu.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"

namespace koan {

namespace {

// The title is a little larger (Zen's h1, font-size: large).
constexpr int kTitleSizeDelta = 4;

}  // namespace

// The Space's icon in the name row: the emoji in a dashed box (empty for
// none), which opens Zen's emoji picker.
class KoanSpaceIconField : public views::Button {
  METADATA_HEADER(KoanSpaceIconField, views::Button)

 public:
  explicit KoanSpaceIconField(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetTooltipText(u"Choose an icon");
    GetViewAccessibility().SetName(u"Space icon");
  }

  const std::u16string& GetIcon() const { return icon_; }
  void SetIcon(const std::u16string& icon) {
    icon_ = icon;
    SchedulePaint();
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kSpaceFormIconWidth, kSpaceFormIconHeight);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(0.5f);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.f);
    flags.setColor(provider->GetColor(kColorKoanSpaceIconBorder));
    constexpr float kDash[] = {3.f, 2.f};
    flags.setPathEffect(cc::PathEffect::MakeDash(kDash, 2, 0));
    canvas->DrawRoundRect(bounds, kSpaceFormIconCornerRadius, flags);
    if (!icon_.empty()) {
      canvas->DrawStringRectWithFlags(
          icon_,
          gfx::FontList().DeriveWithSizeDelta(kSpaceButtonEmojiSize -
                                              gfx::FontList().GetFontSize()),
          provider->GetColor(kColorKoanSpaceForeground), GetLocalBounds(),
          gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }

 private:
  std::u16string icon_;
};

BEGIN_METADATA(KoanSpaceIconField)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Icon)
END_METADATA

// Edit Theme...: a tinted, rounded field, darker while hovered.
class KoanSpaceFormButton : public views::LabelButton {
  METADATA_HEADER(KoanSpaceFormButton, views::LabelButton)

 public:
  KoanSpaceFormButton(PressedCallback callback, std::u16string_view text)
      : views::LabelButton(std::move(callback), text) {
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetBorder(views::CreateEmptyBorder(gfx::Insets(kSpaceFormPadding)));
    SetEnabledTextColors(kColorKoanSpaceForeground);
    SetTextSubpixelRenderingEnabled(false);
    UpdateBackground();
  }

  // views::LabelButton:
  void StateChanged(ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    UpdateBackground();
  }

 private:
  void UpdateBackground() {
    SetBackground(views::CreateRoundedRectBackground(
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED
            ? kColorKoanSpaceFormFieldHovered
            : kColorKoanSpaceFormField,
        kSpaceFormFieldCornerRadius));
  }
};

BEGIN_METADATA(KoanSpaceFormButton)
END_METADATA

KoanSpaceCreationView::KoanSpaceCreationView(
    KoanWorkspaceController* controller)
    : controller_(controller) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kSpaceFormPadding)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  title_ = AddChildView(std::make_unique<views::Label>(u"Create a Space"));
  title_->SetFontList(gfx::FontList()
                          .DeriveWithSizeDelta(kTitleSizeDelta)
                          .DeriveWithWeight(gfx::Font::Weight::BOLD));
  title_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  title_->SetEnabledColor(kColorKoanSpaceForeground);
  title_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 5, 0));
  description_ = AddChildView(std::make_unique<views::Label>(
      u"Spaces are used to organize your tabs and sessions."));
  description_->SetMultiLine(true);
  description_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  // Faded (opacity: 0.4).
  description_->SetEnabledColor(kColorKoanSpaceFormDescription);
  description_->SetProperty(views::kMarginsKey,
                            gfx::Insets::TLBR(0, 0, kSpaceFormSectionGap, 0));

  // The name row: the icon and the name field on one tinted, rounded field.
  auto* row = AddChildView(std::make_unique<views::BoxLayoutView>());
  row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  row->SetInsideBorderInsets(gfx::Insets::TLBR(9, 8, 9, 6));
  row->SetBetweenChildSpacing(8);
  row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  row->SetBackground(views::CreateRoundedRectBackground(
      kColorKoanSpaceFormField, kSpaceFormFieldCornerRadius));
  row->SetProperty(views::kMarginsKey,
                   gfx::Insets::TLBR(0, 0, kSpaceFormFieldGap, 0));
  icon_ = row->AddChildView(
      std::make_unique<KoanSpaceIconField>(base::BindRepeating(
          &KoanSpaceCreationView::PickIcon, base::Unretained(this))));
  name_ = row->AddChildView(std::make_unique<views::Textfield>());
  name_->SetPlaceholderText(u"Space Name");
  name_->GetViewAccessibility().SetName(u"Space Name");
  name_->SetBorder(nullptr);
  // Transparent on the tinted field (which also turns off subpixel text,
  // wrong on the sidebar's transparent layer).
  name_->SetBackgroundColor(SK_ColorTRANSPARENT);
  name_->SetController(this);
  name_->SetTextColorId(kColorKoanSpaceForeground);
  row->SetFlexForView(name_, 1);

  theme_button_ = AddChildView(std::make_unique<KoanSpaceFormButton>(
      base::BindRepeating(&KoanSpaceCreationView::EditTheme,
                          base::Unretained(this)),
      u"Edit Theme\u2026"));

  auto* spacer = AddChildView(std::make_unique<views::View>());
  layout->SetFlexForView(spacer, 1);

  create_ = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&KoanSpaceCreationView::Finish,
                          base::Unretained(this), /*create=*/true),
      u"Create Space"));
  create_->SetStyle(ui::ButtonStyle::kProminent);
  create_->SetEnabled(false);
  create_->SetProperty(views::kMarginsKey,
                       gfx::Insets::TLBR(0, 0, kSpaceFormButtonGap, 0));
  auto* cancel = AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&KoanSpaceCreationView::Finish,
                          base::Unretained(this), /*create=*/false),
      u"Cancel"));
  cancel->SetStyle(ui::ButtonStyle::kText);

  // The sidebar paints to a transparent layer.
  title_->SetSubpixelRenderingEnabled(false);
  description_->SetSubpixelRenderingEnabled(false);
  create_->SetTextSubpixelRenderingEnabled(false);
  cancel->SetTextSubpixelRenderingEnabled(false);
}

KoanSpaceCreationView::~KoanSpaceCreationView() = default;

void KoanSpaceCreationView::FocusName() {
  focus_name_pending_ = true;
  MaybeFocusName();
}

void KoanSpaceCreationView::MaybeFocusName() {
  if (focus_name_pending_ && IsDrawn() && !size().IsEmpty()) {
    focus_name_pending_ = false;
    name_->RequestFocus();
  }
}

void KoanSpaceCreationView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  MaybeFocusName();
}

void KoanSpaceCreationView::VisibilityChanged(views::View* starting_from,
                                              bool is_visible) {
  MaybeFocusName();
}

void KoanSpaceCreationView::ContentsChanged(
    views::Textfield* sender,
    const std::u16string& new_contents) {
  std::u16string trimmed;
  base::TrimWhitespace(new_contents, base::TRIM_ALL, &trimmed);
  create_->SetEnabled(!trimmed.empty());
}

bool KoanSpaceCreationView::HandleKeyEvent(views::Textfield* sender,
                                           const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_RETURN) {
    if (create_->GetEnabled()) {
      Finish(/*create=*/true);
    }
    return true;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    Finish(/*create=*/false);
    return true;
  }
  return false;
}

void KoanSpaceCreationView::PickIcon() {
  picker_ =
      KoanEmojiPicker::Show(icon_, /*allow_none=*/!icon_->GetIcon().empty(),
                            base::BindRepeating(&KoanSpaceCreationView::SetIcon,
                                                weak_factory_.GetWeakPtr()));
}

void KoanSpaceCreationView::SetIcon(const std::u16string& icon) {
  icon_->SetIcon(icon);
}

void KoanSpaceCreationView::EditTheme() {
  theme_menu_ = std::make_unique<KoanThemeMenu>(
      theme_, base::BindRepeating(&KoanSpaceCreationView::SetTheme,
                                  weak_factory_.GetWeakPtr()));
  theme_menu_->RunMenuAt(theme_button_);
}

void KoanSpaceCreationView::SetTheme(
    const KoanWorkspaceController::SpaceTheme& theme) {
  theme_ = theme;
}

void KoanSpaceCreationView::Finish(bool create) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSpaceCreationView::DoFinish,
                                weak_factory_.GetWeakPtr(), create));
}

void KoanSpaceCreationView::DoFinish(bool create) {
  // Closing the form deletes it.
  KoanWorkspaceController* controller = controller_;
  const std::u16string name(name_->GetText());
  const std::u16string icon = icon_->GetIcon();
  const KoanWorkspaceController::SpaceTheme theme = theme_;
  controller->CloseCreateForm();
  if (create) {
    controller->CreateSpace(name, icon, theme);
  }
}

BEGIN_METADATA(KoanSpaceCreationView)
END_METADATA

}  // namespace koan
