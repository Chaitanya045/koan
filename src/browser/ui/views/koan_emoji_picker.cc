// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_emoji_picker.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Koan's emoji, in picker order, with the words they're found by.
constexpr struct {
  const char16_t* emoji;
  const char* keywords;
} kEmojis[] = {
    // Smileys.
    {u"\U0001F600", "grinning happy smile face"},
    {u"\U0001F604", "smile happy joy laugh face"},
    {u"\U0001F602", "joy laugh tears lol funny face"},
    {u"\U0001F60A", "blush smile happy face"},
    {u"\U0001F60D", "love heart eyes face"},
    {u"\U0001F970", "love hearts adore face"},
    {u"\U0001F60E", "cool sunglasses face"},
    {u"\U0001F913", "nerd glasses study face"},
    {u"\U0001F914", "thinking think hmm face"},
    {u"\U0001F634", "sleep sleeping tired zzz face"},
    {u"\U0001F973", "party celebrate birthday face"},
    {u"\U0001F607", "angel halo innocent face"},
    {u"\U0001F92F", "mind blown exploding face"},
    {u"\U0001F624", "angry triumph steam face"},
    {u"\U0001F631", "scream shock fear face"},
    {u"\U0001F916", "robot bot ai machine"},
    {u"\U0001F47B", "ghost spooky halloween"},
    {u"\U0001F47D", "alien ufo space"},
    {u"\U0001F480", "skull dead death"},
    {u"\U0001F383", "pumpkin halloween jack"},
    // People and gestures.
    {u"\U0001F44B", "wave hello hi hand"},
    {u"\U0001F44D", "thumbs up like yes ok hand"},
    {u"\U0001F44F", "clap applause hands"},
    {u"\U0001F64F", "pray please thanks hands"},
    {u"\U0001F4AA", "muscle strong gym fitness arm"},
    {u"\U0001F9E0", "brain mind think smart"},
    {u"\U0001F440", "eyes look see watch"},
    {u"\U0001F9D1\u200D\U0001F4BB",
     "coder developer programmer laptop work technologist"},
    {u"\U0001F9D1\u200D\U0001F3A8", "artist painter art"},
    {u"\U0001F9D1\u200D\U0001F373", "cook chef kitchen food"},
    {u"\U0001F9D1\u200D\U0001F393", "student graduate school university"},
    {u"\U0001F46A", "family home people"},
    // Hearts and symbols.
    {u"\u2764\uFE0F", "heart love red"},
    {u"\U0001F9E1", "heart orange love"},
    {u"\U0001F49B", "heart yellow love"},
    {u"\U0001F49A", "heart green love"},
    {u"\U0001F499", "heart blue love"},
    {u"\U0001F49C", "heart purple love"},
    {u"\U0001F5A4", "heart black love"},
    {u"\u2728", "sparkles magic shine stars"},
    {u"\u2B50", "star favorite"},
    {u"\U0001F31F", "star glowing shine"},
    {u"\u26A1", "lightning zap fast electric power"},
    {u"\U0001F525", "fire hot flame lit"},
    {u"\U0001F4A7", "droplet water drop"},
    {u"\U0001F30A", "wave water ocean sea surf"},
    {u"\U0001F4A1", "idea light bulb"},
    {u"\u2705", "check done tick yes"},
    {u"\u2757", "exclamation important alert"},
    {u"\u2753", "question help"},
    {u"\u267B\uFE0F", "recycle green environment"},
    {u"\u262E\uFE0F", "peace"},
    {u"\u262F\uFE0F", "yin yang balance zen"},
    {u"\U0001F534", "red circle dot"},
    {u"\U0001F7E2", "green circle dot"},
    {u"\U0001F535", "blue circle dot"},
    // Animals.
    {u"\U0001F436", "dog puppy pet animal"},
    {u"\U0001F431", "cat kitten pet animal"},
    {u"\U0001F98A", "fox animal"},
    {u"\U0001F43B", "bear animal"},
    {u"\U0001F43C", "panda bear animal"},
    {u"\U0001F428", "koala animal"},
    {u"\U0001F981", "lion animal king"},
    {u"\U0001F42F", "tiger animal"},
    {u"\U0001F438", "frog animal"},
    {u"\U0001F435", "monkey animal"},
    {u"\U0001F427", "penguin bird animal"},
    {u"\U0001F989", "owl bird night animal"},
    {u"\U0001F98B", "butterfly insect"},
    {u"\U0001F41D", "bee honey insect busy"},
    {u"\U0001F422", "turtle slow animal"},
    {u"\U0001F419", "octopus sea animal"},
    {u"\U0001F433", "whale sea ocean animal"},
    {u"\U0001F984", "unicorn magic fantasy"},
    {u"\U0001F409", "dragon fantasy"},
    {u"\U0001F996", "dinosaur trex"},
    // Nature.
    {u"\U0001F331", "seedling plant grow sprout"},
    {u"\U0001F33F", "herb leaf plant green"},
    {u"\U0001F340", "clover luck four leaf"},
    {u"\U0001F335", "cactus desert plant"},
    {u"\U0001F332", "tree evergreen forest pine"},
    {u"\U0001F333", "tree deciduous nature"},
    {u"\U0001F341", "maple leaf autumn fall"},
    {u"\U0001F338", "cherry blossom flower spring pink"},
    {u"\U0001F33B", "sunflower flower summer"},
    {u"\U0001F339", "rose flower red"},
    {u"\U0001F337", "tulip flower"},
    {u"\U0001F344", "mushroom fungus"},
    {u"\u2600\uFE0F", "sun sunny weather day"},
    {u"\U0001F319", "moon night crescent"},
    {u"\u26C5", "cloud sun weather"},
    {u"\U0001F308", "rainbow colors"},
    {u"\u2744\uFE0F", "snowflake snow winter cold"},
    {u"\U0001F30D", "earth globe world europe africa"},
    {u"\U0001F30F", "earth globe world asia"},
    {u"\U0001FA90", "planet saturn space"},
    // Food and drink.
    {u"\U0001F34E", "apple fruit red"},
    {u"\U0001F34A", "tangerine orange fruit"},
    {u"\U0001F34B", "lemon fruit sour"},
    {u"\U0001F349", "watermelon fruit summer"},
    {u"\U0001F353", "strawberry fruit"},
    {u"\U0001F352", "cherries fruit"},
    {u"\U0001F951", "avocado fruit"},
    {u"\U0001F955", "carrot vegetable"},
    {u"\U0001F35E", "bread bakery"},
    {u"\U0001F9C0", "cheese"},
    {u"\U0001F355", "pizza food"},
    {u"\U0001F354", "burger hamburger food"},
    {u"\U0001F32E", "taco food mexican"},
    {u"\U0001F363", "sushi food japanese"},
    {u"\U0001F35C", "ramen noodles soup food"},
    {u"\U0001F369", "donut doughnut sweet"},
    {u"\U0001F36A", "cookie sweet"},
    {u"\U0001F382", "cake birthday sweet"},
    {u"\U0001F36B", "chocolate sweet"},
    {u"\u2615", "coffee tea cafe hot drink"},
    {u"\U0001F375", "tea matcha green drink"},
    {u"\U0001F377", "wine glass drink"},
    {u"\U0001F37A", "beer drink pub"},
    {u"\U0001F9CB", "bubble tea boba drink"},
    // Activities.
    {u"\u26BD", "soccer football ball sport"},
    {u"\U0001F3C0", "basketball ball sport"},
    {u"\U0001F3BE", "tennis ball sport"},
    {u"\U0001F3C8", "american football ball sport"},
    {u"\u26BE", "baseball ball sport"},
    {u"\U0001F3CB\uFE0F", "gym weights lifting fitness"},
    {u"\U0001F6B4", "cycling bike bicycle sport"},
    {u"\U0001F3C3", "running run jog sport"},
    {u"\U0001F9D8", "yoga meditation zen calm"},
    {u"\U0001F3AE", "game gaming controller video"},
    {u"\U0001F3B2", "dice game board"},
    {u"\u265F\uFE0F", "chess game strategy"},
    {u"\U0001F3A8", "art palette paint design"},
    {u"\U0001F3AC", "movie film clapper cinema"},
    {u"\U0001F3B5", "music note song"},
    {u"\U0001F3A7", "headphones music listen podcast"},
    {u"\U0001F3B8", "guitar music rock"},
    {u"\U0001F3B9", "piano keyboard music"},
    {u"\U0001F4F7", "camera photo picture"},
    {u"\U0001F389", "party tada celebrate confetti"},
    {u"\U0001F381", "gift present birthday"},
    {u"\U0001F3C6", "trophy win award cup"},
    // Travel and places.
    {u"\U0001F3E0", "home house"},
    {u"\U0001F3E2", "office building work"},
    {u"\U0001F3EB", "school building education"},
    {u"\U0001F3E5", "hospital health medical"},
    {u"\U0001F3E6", "bank money finance"},
    {u"\U0001F3D6\uFE0F", "beach vacation holiday sea"},
    {u"\u26F0\uFE0F", "mountain hiking nature"},
    {u"\U0001F3D5\uFE0F", "camping tent outdoors"},
    {u"\U0001F5FA\uFE0F", "map world travel"},
    {u"\u2708\uFE0F", "airplane plane travel flight"},
    {u"\U0001F680", "rocket launch space startup"},
    {u"\U0001F697", "car drive auto"},
    {u"\U0001F6B2", "bicycle bike"},
    {u"\U0001F686", "train rail travel"},
    {u"\u26F5", "sailboat boat sea"},
    // Objects.
    {u"\U0001F4BC", "briefcase work business job"},
    {u"\U0001F4BB", "laptop computer work code"},
    {u"\U0001F5A5\uFE0F", "desktop computer monitor screen"},
    {u"\U0001F4F1", "phone mobile smartphone"},
    {u"\u2328\uFE0F", "keyboard type"},
    {u"\U0001F5B1\uFE0F", "mouse computer"},
    {u"\U0001F4DA", "books library study read"},
    {u"\U0001F4D6", "book open read"},
    {u"\U0001F4DD", "memo note write"},
    {u"\u270F\uFE0F", "pencil write edit"},
    {u"\U0001F4CC", "pin pushpin"},
    {u"\U0001F4CE", "paperclip attachment"},
    {u"\U0001F4C1", "folder files directory"},
    {u"\U0001F4C5", "calendar date schedule"},
    {u"\U0001F4CA", "chart bar stats data"},
    {u"\U0001F4C8", "chart up growth trend"},
    {u"\U0001F4B0", "money bag finance"},
    {u"\U0001F4B3", "card credit payment"},
    {u"\U0001F6D2", "cart shopping store"},
    {u"\U0001F6CD\uFE0F", "shopping bags store"},
    {u"\U0001F4E6", "package box delivery"},
    {u"\u2709\uFE0F", "envelope mail email letter"},
    {u"\U0001F4E3", "megaphone announcement marketing"},
    {u"\U0001F514", "bell notification alert"},
    {u"\U0001F512", "lock locked private secure"},
    {u"\U0001F511", "key password access"},
    {u"\U0001F527", "wrench tool fix settings"},
    {u"\U0001F528", "hammer tool build"},
    {u"\u2699\uFE0F", "gear settings cog"},
    {u"\U0001F9EA", "test tube science lab experiment"},
    {u"\U0001F52C", "microscope science research"},
    {u"\U0001F52D", "telescope astronomy space"},
    {u"\U0001F48A", "pill medicine health"},
    {u"\U0001FA7A", "stethoscope doctor health"},
    {u"\U0001F9E9", "puzzle piece extension"},
    {u"\U0001F5D1\uFE0F", "trash bin delete"},
    {u"\u23F0", "alarm clock time"},
    {u"\u231B", "hourglass time wait"},
    {u"\U0001F9ED", "compass navigate direction"},
    {u"\u2602\uFE0F", "umbrella rain"},
    {u"\U0001F393", "graduation cap school university"},
    {u"\U0001F3F3\uFE0F\u200D\U0001F308", "rainbow flag pride"},
    {u"\U0001F3C1", "checkered flag finish race"},
    {u"\U0001F6A9", "flag red triangular"},
};

// One emoji in the grid.
class KoanEmojiCell : public views::Button {
  METADATA_HEADER(KoanEmojiCell, views::Button)

 public:
  KoanEmojiCell(PressedCallback callback,
                std::u16string emoji,
                std::u16string keywords)
      : views::Button(std::move(callback)),
        emoji_(std::move(emoji)),
        keywords_(std::move(keywords)) {
    GetViewAccessibility().SetName(emoji_);
  }

  const std::u16string& GetLabel() const { return emoji_; }

  bool Matches(const std::u16string& query) const {
    return query.empty() || keywords_.find(query) != std::u16string::npos;
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kEmojiCellSize, kEmojiCellSize);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(provider->GetColor(kColorKoanEmojiHovered));
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()),
                            kEmojiCellCornerRadius, flags);
    }
    canvas->DrawStringRectWithFlags(
        emoji_,
        gfx::FontList().DeriveWithSizeDelta(kEmojiSize -
                                            gfx::FontList().GetFontSize()),
        provider->GetColor(ui::kColorLabelForeground), GetLocalBounds(),
        gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const std::u16string emoji_;
  const std::u16string keywords_;
};

BEGIN_METADATA(KoanEmojiCell)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Label)
END_METADATA

// The emoji grid: as many 28px+ columns as fit, matching cells only.
class KoanEmojiGrid : public views::View {
  METADATA_HEADER(KoanEmojiGrid, views::View)

 public:
  KoanEmojiGrid() = default;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int rows = (VisibleCount() + Columns() - 1) / Columns();
    return gfx::Size(kEmojiPickerWidth,
                     kEmojiPickerPadding / 2 + kEmojiPickerPadding +
                         std::max(0, rows * (kEmojiCellSize + kEmojiPickerGap) -
                                         kEmojiPickerGap));
  }

  void Layout(PassKey) override {
    const int columns = Columns();
    const int column_width = (kEmojiPickerWidth - 2 * kEmojiPickerPadding -
                              (columns - 1) * kEmojiPickerGap) /
                             columns;
    int index = 0;
    for (views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      const int column = index % columns;
      const int row = index / columns;
      child->SetBounds(
          kEmojiPickerPadding + column * (column_width + kEmojiPickerGap) +
              (column_width - kEmojiCellSize) / 2,
          kEmojiPickerPadding / 2 + row * (kEmojiCellSize + kEmojiPickerGap),
          kEmojiCellSize, kEmojiCellSize);
      ++index;
    }
  }

 private:
  static int Columns() {
    return (kEmojiPickerWidth - 2 * kEmojiPickerPadding + kEmojiPickerGap) /
           (kEmojiPickerMinColumnWidth + kEmojiPickerGap);
  }

  int VisibleCount() const {
    return static_cast<int>(std::ranges::count_if(
        children(),
        [](const views::View* child) { return child->GetVisible(); }));
  }
};

BEGIN_METADATA(KoanEmojiGrid)
END_METADATA

}  // namespace

// static
std::unique_ptr<KoanEmojiPicker::Bubble> KoanEmojiPicker::Show(
    views::View* anchor,
    bool allow_none,
    PickedCallback on_pick) {
  return std::make_unique<Bubble>(anchor, allow_none, std::move(on_pick));
}

KoanEmojiPicker::Bubble::Bubble(views::View* anchor,
                                bool allow_none,
                                PickedCallback on_pick)
    // Below the anchor, lined up with its start ("after_start").
    : delegate_(std::make_unique<views::BubbleDialogDelegate>(
          anchor,
          views::BubbleBorder::TOP_LEFT)) {
  delegate_->SetShowTitle(false);
  delegate_->SetShowCloseButton(false);
  delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate_->set_margins(gfx::Insets());
  delegate_->set_fixed_width(kEmojiPickerWidth);
  delegate_->SetTitle(u"Choose an icon");
  KoanEmojiPicker* picker = delegate_->SetContentsView(
      std::make_unique<KoanEmojiPicker>(allow_none, std::move(on_pick)));
  // The client owns the widget; once closed (e.g. a click elsewhere) it is
  // dropped, not kept as an empty shell.
  widget_ = views::BubbleDialogDelegate::CreateBubble(
      delegate_.get(),
      base::BindOnce(
          [](base::WeakPtr<Bubble> bubble, views::Widget::ClosedReason reason) {
            if (bubble) {
              bubble->OnClosed();
            }
          },
          weak_factory_.GetWeakPtr()));
  widget_->Show();
  picker->search_->RequestFocus();
}

void KoanEmojiPicker::Bubble::OnClosed() {
  // Not from inside the widget's own closing. The delegate goes too: it
  // still observes the anchor's widget.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&Bubble::Drop, weak_factory_.GetWeakPtr()));
}

void KoanEmojiPicker::Bubble::Drop() {
  // The widget goes first: it uses the delegate until it is gone.
  widget_.reset();
  delegate_.reset();
}

KoanEmojiPicker::Bubble::~Bubble() {
  Drop();
}

KoanEmojiPicker::KoanEmojiPicker(bool allow_none, PickedCallback on_pick)
    : on_pick_(std::move(on_pick)) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
                       views::BoxLayout::Orientation::kVertical))
      ->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kStretch);

  // #PanelUI-zen-picker-header: the page name and None.
  auto* header = AddChildView(std::make_unique<views::BoxLayoutView>());
  header->SetInsideBorderInsets(gfx::Insets::VH(6, 16));
  header->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* page = header->AddChildView(std::make_unique<views::Label>(u"Emojis"));
  page->SetFontList(
      gfx::FontList().DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  header->SetFlexForView(page, 1);
  page->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  none_ = header->AddChildView(views::CreateVectorImageButtonWithNativeTheme(
      base::BindRepeating(&KoanEmojiPicker::Pick, base::Unretained(this),
                          std::u16string()),
      kDeleteIcon, /*dip_size=*/16));
  none_->SetTooltipText(u"No icon");
  none_->GetViewAccessibility().SetName(u"No icon");
  none_->SetVisible(allow_none);

  // #PanelUI-zen-emojis-picker-header: the search field.
  auto* search_row = AddChildView(std::make_unique<views::BoxLayoutView>());
  search_row->SetInsideBorderInsets(gfx::Insets::TLBR(0, 10, 6, 10));
  search_ = search_row->AddChildView(std::make_unique<views::Textfield>());
  search_->SetPlaceholderText(u"Search emojis");
  search_->GetViewAccessibility().SetName(u"Search emojis");
  search_->SetController(this);
  search_row->SetFlexForView(search_, 1);

  auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->ClipHeightTo(0, kEmojiPickerMaxHeight);
  scroll->SetBackgroundColor(std::nullopt);
  grid_ = scroll->SetContents(std::make_unique<KoanEmojiGrid>());
  for (const auto& entry : kEmojis) {
    const std::u16string emoji(entry.emoji);
    grid_->AddChildView(std::make_unique<KoanEmojiCell>(
        base::BindRepeating(&KoanEmojiPicker::Pick, base::Unretained(this),
                            emoji),
        emoji, base::UTF8ToUTF16(entry.keywords)));
  }
}

KoanEmojiPicker::~KoanEmojiPicker() = default;

void KoanEmojiPicker::ContentsChanged(views::Textfield* sender,
                                      const std::u16string& new_contents) {
  const std::u16string query =
      base::ToLowerASCII(base::TrimWhitespace(new_contents, base::TRIM_ALL));
  for (views::View* child : grid_->children()) {
    child->SetVisible(static_cast<KoanEmojiCell*>(child)->Matches(query));
  }
  grid_->InvalidateLayout();
}

void KoanEmojiPicker::Pick(const std::u16string& emoji) {
  // Zen offers None once there is an icon to remove.
  none_->SetVisible(!emoji.empty());
  on_pick_.Run(emoji);
}

BEGIN_METADATA(KoanEmojiPicker)
END_METADATA

}  // namespace koan
