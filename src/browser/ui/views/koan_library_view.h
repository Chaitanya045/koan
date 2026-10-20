// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_VIEW_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_VIEW_H_

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "koan/browser/ui/views/koan_library_ui.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/menu/menu_types.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class BrowserView;

namespace views {
class ImageView;
class MenuRunner;
class ScrollView;
class Textfield;
}  // namespace views

namespace koan {

class KoanLibraryController;
class KoanLibrarySection;
class KoanLibraryTab;

// Asks before something that can't be undone, in a browser-modal dialog;
// `on_accept` runs if the user agrees.
void ShowLibraryConfirmation(BrowserView* browser_view,
                             std::u16string title,
                             std::u16string message,
                             std::u16string accept,
                             base::OnceClosure on_accept);

// A menu built from closures, for the library's context menus.
class KoanLibraryMenu : public ui::SimpleMenuModel::Delegate {
 public:
  KoanLibraryMenu();
  KoanLibraryMenu(const KoanLibraryMenu&) = delete;
  KoanLibraryMenu& operator=(const KoanLibraryMenu&) = delete;
  ~KoanLibraryMenu() override;

  void AddItem(std::u16string label,
               base::RepeatingClosure action,
               bool enabled = true);
  void AddItemWithIcon(std::u16string label,
                       const ui::ImageModel& icon,
                       base::RepeatingClosure action);
  void AddSeparator();
  KoanLibraryMenu* AddSubMenu(std::u16string label);

  // Shows the menu at `bounds` (screen coordinates) for `source`; `on_closed`
  // runs once it is gone. The menu must outlive the run.
  void Run(views::View* source,
           const gfx::Rect& bounds,
           views::MenuAnchorPosition anchor,
           ui::mojom::MenuSourceType source_type,
           base::RepeatingClosure on_closed);
  bool IsRunning() const;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;
  bool IsCommandIdEnabled(int command_id) const override;

 private:
  ui::SimpleMenuModel model_{this};
  std::vector<base::RepeatingClosure> actions_;
  std::vector<bool> enabled_;
  std::vector<std::unique_ptr<KoanLibraryMenu>> submenus_;
  std::unique_ptr<views::MenuRunner> runner_;
};

// A pill button (Zen's filter button, chips and "Done").
class KoanLibraryPill : public views::LabelButton {
  METADATA_HEADER(KoanLibraryPill, views::LabelButton)

 public:
  KoanLibraryPill(PressedCallback callback,
                  std::u16string text,
                  gfx::Font::Weight weight,
                  const gfx::Insets& padding);
  ~KoanLibraryPill() override;

  void SetActive(bool active);
  bool GetActive() const { return active_; }
  // Greys the pill out and turns it off, like Zen's :disabled chips.
  void SetUnavailable(bool unavailable);

 private:
  bool active_ = false;
};

// A row of a list, like Zen's .zen-library-row: an icon, a title, a
// subtitle (which can read differently while the row is hovered) and action
// buttons that show while it is hovered or its menu is open.
class KoanLibraryRow : public views::Button,
                       public views::ContextMenuController {
  METADATA_HEADER(KoanLibraryRow, views::Button)

 public:
  using MenuCallback = base::RepeatingCallback<
      void(KoanLibraryRow*, const gfx::Point&, ui::mojom::MenuSourceType)>;

  explicit KoanLibraryRow(PressedCallback callback);
  ~KoanLibraryRow() override;

  // Replaces the leading icon.
  void SetIcon(const ui::ImageModel& icon, int size);
  void SetIconView(std::unique_ptr<views::View> view);
  void SetTitle(std::u16string title);
  std::u16string GetTitle() const;
  // `hovered` is shown instead of `normal` while the row is hovered.
  void SetSubtitle(std::u16string normal, std::u16string hovered);
  std::u16string GetSubtitle() const;
  views::View* AddAction(std::unique_ptr<views::View> action);
  void ClearActions();
  void SetMenuCallback(MenuCallback callback);
  // Keeps the row looking hovered while its menu is open.
  void SetMenuOpen(bool open);
  // Dims the row (a download whose file is gone).
  void SetDimmed(bool dimmed);
  bool GetHovered() const;

  // views::Button:
  void StateChanged(ButtonState old_state) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

 private:
  void UpdateHover();

  raw_ptr<views::View> icon_box_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> subtitle_ = nullptr;
  raw_ptr<views::View> actions_ = nullptr;
  std::u16string subtitle_normal_;
  std::u16string subtitle_hovered_;
  MenuCallback menu_callback_;
  bool menu_open_ = false;
  bool hovered_ = false;
};

// One section of the library.
class KoanLibrarySection : public views::View {
  METADATA_HEADER(KoanLibrarySection, views::View)

 public:
  KoanLibrarySection(KoanLibraryController* library, LibrarySection id);
  ~KoanLibrarySection() override;

  LibrarySection id() const { return id_; }
  KoanLibraryController* library() const { return library_; }

  // The width the section wants, not counting the side column.
  virtual int GetContentWidth() const;
  // The section is shown, or the library opens on it.
  virtual void OnShown() {}
  // Esc while the library is open: whether the section used it.
  virtual bool HandleEscape();
  // What a right click on the section's tab offers (Zen's tabMenu).
  virtual void FillTabMenu(KoanLibraryMenu* menu) {}

  // Shows `menu` (kept until the next one) for `source`.
  void RunMenu(std::unique_ptr<KoanLibraryMenu> menu,
               views::View* source,
               const gfx::Rect& screen_bounds,
               views::MenuAnchorPosition anchor,
               ui::mojom::MenuSourceType source_type,
               base::RepeatingClosure on_closed = base::DoNothing());

 private:
  const raw_ptr<KoanLibraryController> library_;
  const LibrarySection id_;
  std::unique_ptr<KoanLibraryMenu> menu_;
};

// A section with Zen's search box, filter panel and a scrolling list
// (ZenLibrarySearchSection.mjs): History and Downloads.
class KoanLibrarySearchSection : public KoanLibrarySection,
                                 public views::TextfieldController {
  METADATA_HEADER(KoanLibrarySearchSection, KoanLibrarySection)

 public:
  struct FilterOption {
    FilterOption(std::string id, std::u16string label, bool disabled = false);
    FilterOption(const FilterOption&);
    ~FilterOption();
    std::string id;
    std::u16string label;
    bool disabled = false;
  };
  struct FilterGroup {
    FilterGroup();
    FilterGroup(std::string id,
                std::u16string title,
                bool exclusive,
                std::vector<FilterOption> options);
    FilterGroup(const FilterGroup&);
    ~FilterGroup();
    std::string id;
    std::u16string title;
    // An exclusive group works like radio buttons whose active option can
    // be switched back off.
    bool exclusive = false;
    std::vector<FilterOption> options;
  };

  KoanLibrarySearchSection(KoanLibraryController* library,
                           LibrarySection id,
                           std::u16string placeholder,
                           std::u16string filter_title);
  ~KoanLibrarySearchSection() override;

  bool IsFilterActive(const std::string& group,
                      const std::string& option) const;
  void SetFilterActive(const std::string& group,
                       const std::string& option,
                       bool active);
  // Days covered by the "when" filter (Today, This Week, This Month).
  std::optional<int> GetWhenDays() const;
  const std::u16string& query() const { return query_; }

  views::Textfield* search_field() { return search_field_; }
  bool GetFiltersOpen() const { return filters_open_; }
  void SetFiltersOpen(bool open);

  // KoanLibrarySection:
  void OnShown() override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

 protected:
  // Zen's "when" group, shared by History and Downloads.
  static FilterGroup WhenFilterGroup(std::u16string title);

  // Builds the filter panel; call from the subclass's constructor.
  void InitFilters();
  virtual std::vector<FilterGroup> GetFilterGroups() const = 0;
  virtual void OnSearchChanged() = 0;
  virtual void OnFiltersChanged() = 0;
  virtual void OnScrolledToEnd() {}

  // The list's content: rows grouped under optional headers.
  views::View* list() { return list_; }
  void ClearList();
  views::View* AddGroup(const std::u16string& header);
  void ShowEmpty(std::u16string text);
  // Rebuilds the chips, e.g. when an option became (un)available.
  void UpdateFilterPanel();

 private:
  void ToggleFilter(const FilterGroup& group, const std::string& option);
  void OnSearchTimer();
  void OnScrolled();

  raw_ptr<views::View> search_header_ = nullptr;
  raw_ptr<views::Textfield> search_field_ = nullptr;
  raw_ptr<views::View> filter_header_ = nullptr;
  raw_ptr<views::View> filter_panel_ = nullptr;
  raw_ptr<views::ScrollView> scroll_ = nullptr;
  raw_ptr<views::View> list_ = nullptr;
  std::set<std::string> active_filters_;
  std::u16string query_;
  bool filters_open_ = false;
  base::OneShotTimer search_timer_;
  base::CallbackListSubscription scroll_subscription_;
  base::WeakPtrFactory<KoanLibrarySearchSection> weak_factory_{this};
};

// The library's panel: the side column (section tabs, back button) and the
// shown section.
class KoanLibraryView : public views::View {
  METADATA_HEADER(KoanLibraryView, views::View)

 public:
  explicit KoanLibraryView(KoanLibraryController* library);
  ~KoanLibraryView() override;

  void ShowSection(LibrarySection section);
  KoanLibrarySection* GetSection(LibrarySection section) const;
  KoanLibrarySection* active_section() const;
  // The width the panel wants for the shown section.
  int GetPreferredWidth() const;
  // Whether the library is open (for tests: the panel outlives closing).
  bool GetOpen() const;
  // The library opened (or was asked for again).
  void OnOpened();
  bool HandleEscape();
  // Zen's toast, e.g. "Opened tab in a new tab".
  void ShowToast(std::u16string text);
  // Progress of the current downloads for the Downloads tab's ring; < 0 when
  // nothing downloads.
  void SetDownloadProgress(float progress);

  // Shows what a right click on a section's tab offers.
  void ShowTabMenu(LibrarySection section, const gfx::Point& screen_point);

  // views::View:
  void Layout(PassKey) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

 private:
  const raw_ptr<KoanLibraryController> library_;
  raw_ptr<views::View> side_ = nullptr;
  raw_ptr<views::View> footer_ = nullptr;
  std::array<raw_ptr<KoanLibraryTab>, 3> tabs_{};
  std::array<raw_ptr<KoanLibrarySection>, 3> sections_{};
  raw_ptr<views::View> toast_ = nullptr;
  base::OneShotTimer toast_timer_;
  std::optional<LibrarySection> shown_;
  std::unique_ptr<KoanLibraryMenu> tab_menu_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_VIEW_H_
