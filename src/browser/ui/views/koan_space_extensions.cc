// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_extensions.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/case_conversion.h"
#include "base/scoped_multi_source_observation.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_process_host_observer.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/result_codes.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "koan/browser/containers/koan_container_extensions.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_extensions_hooks.h"
#include "koan/browser/ui/views/koan_space_profile_menu.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/widget/widget.h"

namespace koan {

// The hooks' copy of the pref's name is the pref.
static_assert(std::string_view(prefs::kUserContainers) ==
              std::string_view(containers::kUserContainersPref));

namespace {

DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanSpaceExtensionsDone);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanSpaceExtensionsDone);

constexpr char16_t kNoProfileTitle[] = u"Extensions in This Space";
constexpr char16_t kProfileText[] =
    u"An extension that is off can't see or change this profile's pages, in "
    u"every Space that uses it. The profile's open tabs reload when you "
    u"press Done.";
constexpr char16_t kNoProfileText[] =
    u"This Space uses the Default profile, which every Space without a "
    u"profile of its own shares. Turning an extension off gives this Space a "
    u"profile of its own.";
constexpr char16_t kNoExtensionsText[] = u"No extensions are installed.";
constexpr char16_t kContextMenuItem[] = u"Extensions in This Space…";

// How long a restart waits for the profile's processes to end before it
// reloads the tabs anyway.
constexpr base::TimeDelta kRestartTimeout = base::Seconds(5);

// The user's containers are the original profile's
// (koan_space_profile_menu.cc).
PrefService* PrefsOf(Profile* profile) {
  return profile->GetOriginalProfile()->GetPrefs();
}

// An extension the dialog lists: the ones chrome://extensions shows, enabled
// or not, by name.
struct ListedExtension {
  std::string id;
  std::u16string name;
};

std::vector<ListedExtension> ListedExtensions(Profile* profile) {
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile->GetOriginalProfile());
  std::vector<ListedExtension> listed;
  for (const extensions::ExtensionSet* set :
       {&registry->enabled_extensions(), &registry->disabled_extensions()}) {
    for (const auto& extension : *set) {
      if (!extension->is_theme() &&
          extensions::ui_util::ShouldDisplayInExtensionSettings(*extension)) {
        listed.push_back({.id = extension->id(),
                          .name = base::UTF8ToUTF16(extension->name())});
      }
    }
  }
  std::ranges::sort(
      listed, [](const ListedExtension& a, const ListedExtension& b) {
        return base::i18n::ToLower(a.name) < base::i18n::ToLower(b.name);
      });
  return listed;
}

// The dialog's list: a row with the name and a switch per extension.
class SpaceExtensionsList : public views::BoxLayoutView {
  METADATA_HEADER(SpaceExtensionsList, views::BoxLayoutView)

 public:
  // Without a profile, turning a switch off runs `turn_off_without_profile`
  // with the extension instead (and the switch stays on).
  SpaceExtensionsList(const std::vector<ListedExtension>& extensions,
                      std::set<std::string> off,
                      base::RepeatingCallback<void(const std::string&)>
                          turn_off_without_profile)
      : off_(std::move(off)),
        turn_off_without_profile_(std::move(turn_off_without_profile)) {
    const views::LayoutProvider* layout = views::LayoutProvider::Get();
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetBetweenChildSpacing(
        layout->GetDistanceMetric(views::DISTANCE_RELATED_CONTROL_VERTICAL));
    if (extensions.empty()) {
      auto* empty =
          AddChildView(std::make_unique<views::Label>(kNoExtensionsText));
      empty->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    }
    for (const ListedExtension& extension : extensions) {
      auto* row = AddChildView(std::make_unique<views::BoxLayoutView>());
      row->SetBetweenChildSpacing(layout->GetDistanceMetric(
          views::DISTANCE_RELATED_CONTROL_HORIZONTAL));
      row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
      auto* name =
          row->AddChildView(std::make_unique<views::Label>(extension.name));
      name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      name->SetElideBehavior(gfx::ELIDE_TAIL);
      row->SetFlexForView(name, 1);
      auto* toggle = row->AddChildView(std::make_unique<views::ToggleButton>());
      toggle->SetIsOn(!off_.contains(extension.id));
      toggle->GetViewAccessibility().SetName(extension.name);
      toggle->SetCallback(base::BindRepeating(
          &SpaceExtensionsList::OnToggled, base::Unretained(this), extension.id,
          base::Unretained(toggle)));
    }
  }
  SpaceExtensionsList(const SpaceExtensionsList&) = delete;
  SpaceExtensionsList& operator=(const SpaceExtensionsList&) = delete;
  ~SpaceExtensionsList() override = default;

  const std::set<std::string>& off() const { return off_; }

  base::WeakPtr<SpaceExtensionsList> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  void OnToggled(const std::string& id, views::ToggleButton* toggle) {
    if (toggle->GetIsOn()) {
      off_.erase(id);
      return;
    }
    if (turn_off_without_profile_) {
      // The prompt for a new profile takes this dialog's place.
      toggle->SetIsOn(true);
      GetWidget()->Close();
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(turn_off_without_profile_, id));
      return;
    }
    off_.insert(id);
  }

  std::set<std::string> off_;
  const base::RepeatingCallback<void(const std::string&)>
      turn_off_without_profile_;
  base::WeakPtrFactory<SpaceExtensionsList> weak_factory_{this};
};

BEGIN_METADATA(SpaceExtensionsList)
END_METADATA

// Done: the list goes with the profile, and its pages restart if it changed.
void SaveList(base::WeakPtr<Profile> profile,
              const containers::ContainerId& container,
              base::WeakPtr<SpaceExtensionsList> list) {
  if (!profile || !list) {
    return;
  }
  bool changed = false;
  {
    ScopedListPrefUpdate update(PrefsOf(profile.get()), prefs::kUserContainers);
    changed =
        containers::SetExtensionsOff(update.Get(), container, list->off());
  }
  if (changed) {
    RestartProfileTabs(profile.get(), container);
  }
}

// New Profile... from the dialog of a Space without one: the new profile
// starts with `extension_id` off, the Space takes it, and the dialog opens
// again for it.
void OnProfileForExtension(base::WeakPtr<KoanWorkspaceController> controller,
                           tab_groups::TabGroupId space,
                           std::string extension_id,
                           const std::string& container) {
  const std::optional<containers::ContainerId> id =
      containers::SpaceContainerFromString(container);
  if (!controller || !id) {
    return;
  }
  {
    ScopedListPrefUpdate update(
        PrefsOf(controller->browser_view()->GetProfile()),
        prefs::kUserContainers);
    containers::SetExtensionsOff(update.Get(), *id, {extension_id});
  }
  controller->SetSpaceContainer(space, container);
  ShowSpaceExtensions(controller.get(), space);
}

void AskForProfile(base::WeakPtr<KoanWorkspaceController> controller,
                   tab_groups::TabGroupId space,
                   const std::string& extension_id) {
  if (!controller) {
    return;
  }
  ShowNewProfilePrompt(
      controller->browser_view()->browser(),
      base::BindOnce(&OnProfileForExtension, controller, space, extension_id));
}

// Ends a profile's renderer processes, then reloads its tabs (or after
// kRestartTimeout, whichever is first). Owns itself.
class ProfileRestart : public content::RenderProcessHostObserver {
 public:
  ProfileRestart(Profile* profile, const containers::ContainerId& container)
      : profile_(profile->GetWeakPtr()), container_(container) {}
  ProfileRestart(const ProfileRestart&) = delete;
  ProfileRestart& operator=(const ProfileRestart&) = delete;
  ~ProfileRestart() override = default;

  void Start() {
    content::StoragePartition* partition = profile_->GetStoragePartition(
        containers::PartitionConfigFor(profile_.get(), container_),
        /*can_create=*/false);
    std::vector<content::RenderProcessHost*> processes;
    for (auto it = content::RenderProcessHost::AllHostsIterator();
         partition && !it.IsAtEnd(); it.Advance()) {
      if (it.GetCurrentValue()->GetStoragePartition() == partition) {
        processes.push_back(it.GetCurrentValue());
      }
    }
    starting_ = true;
    for (content::RenderProcessHost* process : processes) {
      // Watched first, in case it ends at once.
      observations_.AddObservation(process);
      if (!process->Shutdown(content::RESULT_CODE_KILLED) &&
          observations_.IsObservingSource(process)) {
        // Not running: nothing to wait for.
        observations_.RemoveObservation(process);
      }
    }
    starting_ = false;
    if (!observations_.IsObservingAnySource()) {
      ReloadTabs();
      return;
    }
    timeout_.Start(
        FROM_HERE, kRestartTimeout,
        base::BindOnce(&ProfileRestart::ReloadTabs, base::Unretained(this)));
  }

  // content::RenderProcessHostObserver:
  void RenderProcessExited(
      content::RenderProcessHost* host,
      const content::ChildProcessTerminationInfo& info) override {
    OnProcessGone(host);
  }
  void RenderProcessHostDestroyed(content::RenderProcessHost* host) override {
    OnProcessGone(host);
  }

 private:
  void OnProcessGone(content::RenderProcessHost* host) {
    observations_.RemoveObservation(host);
    if (!starting_ && !observations_.IsObservingAnySource()) {
      // Not from inside the process's notification.
      timeout_.Stop();
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&ProfileRestart::ReloadTabs, base::Unretained(this)));
    }
  }

  // Each of the profile's tabs, in every window, loads again in a new
  // process. Ends the restart.
  void ReloadTabs() {
    observations_.RemoveAllObservations();
    for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
      if (!profile_ || browser->GetProfile() != profile_.get()) {
        continue;
      }
      TabStripModel* tab_strip = browser->GetTabStripModel();
      for (int i = 0; i < tab_strip->count(); ++i) {
        content::WebContents* contents = tab_strip->GetWebContentsAt(i);
        if (!contents->WasDiscarded() &&
            containers::ContainerOf(contents) == container_) {
          contents->GetController().Reload(content::ReloadType::NORMAL,
                                           /*check_for_repost=*/true);
        }
      }
    }
    delete this;
  }

  base::WeakPtr<Profile> profile_;
  const containers::ContainerId container_;
  // While Start() ends the processes (Start() goes on when they are gone).
  bool starting_ = false;
  base::OneShotTimer timeout_;
  base::ScopedMultiSourceObservation<content::RenderProcessHost,
                                     content::RenderProcessHostObserver>
      observations_{this};
};

// The active tab's contents of `browser`, or null.
content::WebContents* ActiveContents(BrowserWindowInterface* browser) {
  tabs::TabInterface* tab =
      browser ? browser->GetActiveTabInterface() : nullptr;
  return tab ? tab->GetContents() : nullptr;
}

}  // namespace

void ShowSpaceExtensions(KoanWorkspaceController* controller,
                         const tab_groups::TabGroupId& space) {
  BrowserView* browser_view = controller->browser_view();
  if (!browser_view->GetWidget()) {
    return;
  }
  Profile* profile = browser_view->GetProfile();
  const std::string container_text = controller->GetSpaceContainer(space);
  const std::optional<containers::ContainerId> container =
      containers::SpaceContainerFromString(container_text);
  std::set<std::string> off;
  if (container) {
    off = containers::ExtensionsOffIn(
        PrefsOf(profile)->GetList(prefs::kUserContainers), *container);
  }
  auto list = std::make_unique<SpaceExtensionsList>(
      ListedExtensions(profile), std::move(off),
      container ? base::RepeatingCallback<void(const std::string&)>()
                : base::BindRepeating(&AskForProfile, controller->GetWeakPtr(),
                                      space));
  base::WeakPtr<SpaceExtensionsList> list_ptr = list->GetWeakPtr();

  ui::DialogModel::Builder builder(std::make_unique<ui::DialogModelDelegate>());
  builder
      .SetTitle(container
                    ? base::StrCat({u"Extensions in ",
                                    SpaceProfileName(profile, container_text)})
                    : std::u16string(kNoProfileTitle))
      .AddParagraph(
          ui::DialogModelLabel(container ? kProfileText : kNoProfileText))
      .AddCustomField(
          std::make_unique<views::BubbleDialogModelHost::CustomView>(
              std::move(list),
              views::BubbleDialogModelHost::FieldType::kControl));
  if (container) {
    builder
        .AddOkButton(base::BindOnce(&SaveList,
                                    profile->GetOriginalProfile()->GetWeakPtr(),
                                    *container, list_ptr),
                     ui::DialogModel::Button::Params().SetLabel(u"Done").SetId(
                         kKoanSpaceExtensionsDone))
        .AddCancelButton(base::DoNothing());
  } else {
    // Nothing to save: a switch turned off makes a profile (AskForProfile()).
    builder.AddOkButton(
        base::DoNothing(),
        ui::DialogModel::Button::Params().SetLabel(u"Done").SetId(
            kKoanSpaceExtensionsDone));
  }
  constrained_window::ShowBrowserModal(builder.Build(),
                                       browser_view->GetNativeWindow());
}

KoanSpaceExtensionsContextMenu::KoanSpaceExtensionsContextMenu(
    BrowserView* browser_view)
    : browser_view_(browser_view) {
  model_.AddItem(/*command_id=*/1, kContextMenuItem);
}

KoanSpaceExtensionsContextMenu::~KoanSpaceExtensionsContextMenu() = default;

void KoanSpaceExtensionsContextMenu::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  runner_ = std::make_unique<views::MenuRunner>(
      &model_, views::MenuRunner::CONTEXT_MENU);
  runner_->RunMenuAt(source->GetWidget(), nullptr,
                     gfx::Rect(point, gfx::Size()),
                     views::MenuAnchorPosition::kTopLeft, source_type);
}

bool KoanSpaceExtensionsContextMenu::IsCommandIdEnabled(int command_id) const {
  KoanWorkspaceController* workspaces = Workspaces();
  return workspaces && workspaces->GetActiveSpace().has_value();
}

void KoanSpaceExtensionsContextMenu::ExecuteCommand(int command_id,
                                                    int event_flags) {
  // After the menu has closed: it opens a dialog.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSpaceExtensionsContextMenu::ShowDialog,
                                weak_factory_.GetWeakPtr()));
}

KoanWorkspaceController* KoanSpaceExtensionsContextMenu::Workspaces() const {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  return controller ? controller->workspaces() : nullptr;
}

void KoanSpaceExtensionsContextMenu::ShowDialog() {
  KoanWorkspaceController* workspaces = Workspaces();
  const std::optional<tab_groups::TabGroupId> space =
      workspaces ? workspaces->GetActiveSpace() : std::nullopt;
  if (space) {
    ShowSpaceExtensions(workspaces, *space);
  }
}

void RestartProfileTabs(Profile* profile,
                        const containers::ContainerId& container) {
  (new ProfileRestart(profile, container))->Start();
}

bool HidesExtensionAction(BrowserWindowInterface* browser,
                          std::string_view action_id) {
  return containers::IsExtensionOffInContents(ActiveContents(browser),
                                              action_id);
}

KoanSpaceExtensionsGuard::KoanSpaceExtensionsGuard(BrowserView* browser_view)
    : browser_view_(browser_view) {
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  pref_change_registrar_.Init(PrefsOf(browser_view_->GetProfile()));
  pref_change_registrar_.Add(
      prefs::kUserContainers,
      base::BindRepeating(&KoanSpaceExtensionsGuard::OnListChanged,
                          base::Unretained(this)));
}

KoanSpaceExtensionsGuard::~KoanSpaceExtensionsGuard() = default;

void KoanSpaceExtensionsGuard::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    ScheduleCheckSidePanel();
  }
}

void KoanSpaceExtensionsGuard::OnListChanged() {
  // The toolbar's buttons (ToolbarView::Update() asks every icon again).
  if (content::WebContents* contents =
          ActiveContents(browser_view_->browser())) {
    browser_view_->UpdateToolbar(contents);
  }
  ScheduleCheckSidePanel();
}

void KoanSpaceExtensionsGuard::ScheduleCheckSidePanel() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSpaceExtensionsGuard::CheckSidePanel,
                                weak_factory_.GetWeakPtr()));
}

void KoanSpaceExtensionsGuard::CheckSidePanel() {
  SidePanelUI* side_panel = SidePanelUI::From(browser_view_->browser());
  if (!side_panel) {
    return;
  }
  if (!side_panel_shown_subscription_) {
    side_panel_shown_subscription_ = side_panel->RegisterSidePanelShown(
        base::BindRepeating(&KoanSpaceExtensionsGuard::ScheduleCheckSidePanel,
                            base::Unretained(this)));
  }
  content::WebContents* contents = ActiveContents(browser_view_->browser());
  const std::optional<containers::ContainerId> container =
      contents ? containers::ContainerOf(contents) : std::nullopt;
  if (!container || !side_panel->IsSidePanelShowing()) {
    return;
  }
  for (const std::string& extension_id :
       containers::ExtensionsOffIn(PrefsOf(browser_view_->GetProfile())
                                       ->GetList(prefs::kUserContainers),
                                   *container)) {
    if (side_panel->IsSidePanelEntryShowing(
            SidePanelEntryKey(SidePanelEntryId::kExtension, extension_id))) {
      side_panel->Close();
      return;
    }
  }
}

}  // namespace koan
