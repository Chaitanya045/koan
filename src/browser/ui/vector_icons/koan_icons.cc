// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/vector_icons/koan_icons.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/component_export.h"
#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/thread_annotations.h"
#include "build/build_config.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/koan_switches.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/vector_icons.h"

namespace gfx {
// Defined by the hook in ui/gfx/paint_vector_icon.cc.
COMPONENT_EXPORT(GFX)
void SetVectorIconOverrideForKoan(
    const VectorIcon& (*override_icon)(const VectorIcon&, int dip_size));
}  // namespace gfx

namespace koan {

namespace {

// Chromium's icons by what they show, grouped under the Zen icon (zen-icons/
// <name>.svg, selected by Zen's icons.css) that shows it there. Several
// variants (old, Chrome Refresh) of an icon map to one Koan icon.
const KoanIconEntry kEntries[] = {
    // back
    {&vector_icons::kBackArrowChromeRefreshOldIcon, &kKoanBackIcon},
    {&vector_icons::kBackArrowOldIcon, &kKoanBackIcon},
    {&vector_icons::kArrowBackIcon, &kKoanBackIcon},
    {&vector_icons::kArrowBackChromeRefreshOldIcon, &kKoanBackIcon},
    {&vector_icons::kArrowBackOldIcon, &kKoanBackIcon},
    {&kArrowBackOldIcon, &kKoanBackIcon},
    {&kBackArrowTouchOldIcon, &kKoanBackIcon},
    // forward
    {&vector_icons::kForwardArrowChromeRefreshOldIcon, &kKoanForwardIcon},
    {&vector_icons::kForwardArrowOldIcon, &kKoanForwardIcon},
    {&vector_icons::kArrowForwardIcon, &kKoanForwardIcon},
    {&kArrowForwardOldIcon, &kKoanForwardIcon},
    {&kForwardArrowTouchOldIcon, &kKoanForwardIcon},
    // reload
    {&vector_icons::kReloadChromeRefreshOldIcon, &kKoanReloadIcon},
    {&kReloadTouchOldIcon, &kKoanReloadIcon},
    {&vector_icons::kRefreshIcon, &kKoanReloadIcon},
    // close
    {&vector_icons::kCloseIcon, &kKoanCloseIcon},
    {&kCloseIcon, &kKoanCloseIcon},
    {&views::kCloseIcon, &kKoanCloseIcon},
    {&vector_icons::kCloseChromeRefreshOldIcon, &kKoanCloseIcon},
    {&kCloseChromeRefreshOldIcon, &kKoanCloseIcon},
    {&vector_icons::kCloseOldIcon, &kKoanCloseIcon},
    {&views::kCloseOldIcon, &kKoanCloseIcon},
    {&vector_icons::kCloseSmallIcon, &kKoanCloseIcon},
    {&kCloseSmallIcon, &kKoanCloseIcon},
    {&vector_icons::kCloseSmallOldIcon, &kKoanCloseIcon},
    {&vector_icons::kCloseRoundedOldIcon, &kKoanCloseIcon},
    {&kCloseWeight500CustomIcon, &kKoanCloseIcon},
    {&kNavigateStopChromeRefreshOldIcon, &kKoanCloseIcon},
    {&kNavigateStopTouchOldIcon, &kKoanCloseIcon},
    {&kTabCloseIcon, &kKoanCloseIcon},
    {&kTabCloseNormalOldIcon, &kKoanCloseIcon},
    {&kCloseTabChromeRefreshOldIcon, &kKoanCloseIcon},
    {&views::kIcCloseOldIcon, &kKoanCloseIcon},
    // plus
    {&vector_icons::kAdd2Icon, &kKoanPlusIcon},
    {&vector_icons::kAddOldIcon, &kKoanPlusIcon},
    {&kAddOldIcon, &kKoanPlusIcon},
    {&kAddIcon, &kKoanPlusIcon},
    {&kAddChromeRefreshOldIcon, &kKoanPlusIcon},
    {&vector_icons::kAddWeight500CustomIcon, &kKoanPlusIcon},
    {&kNewTabRefreshOldIcon, &kKoanPlusIcon},
    {&views::kNewTabOldIcon, &kKoanPlusIcon},
    // search-glass
    {&vector_icons::kSearchIcon, &kKoanSearchGlassIcon},
    {&vector_icons::kSearchChromeRefreshOldIcon, &kKoanSearchGlassIcon},
    {&vector_icons::kSearchOldIcon, &kKoanSearchGlassIcon},
    {&kSearchMenuOldIcon, &kKoanSearchGlassIcon},
    // downloads
    {&vector_icons::kDownloadIcon, &kKoanDownloadsIcon},
    {&kDownloadIcon, &kKoanDownloadsIcon},
    {&vector_icons::kFileDownloadChromeRefreshOldIcon, &kKoanDownloadsIcon},
    {&vector_icons::kFileDownloadOldIcon, &kKoanDownloadsIcon},
    {&kDownloadToolbarButtonChromeRefreshOldIcon, &kKoanDownloadsIcon},
    {&kDownloadToolbarButtonOldIcon, &kKoanDownloadsIcon},
    {&kDownloadMenuOldIcon, &kKoanDownloadsIcon},
    // history
    {&vector_icons::kHistoryIcon, &kKoanHistoryIcon},
    {&kHistoryIcon, &kKoanHistoryIcon},
    {&vector_icons::kHistoryChromeRefreshOldIcon, &kKoanHistoryIcon},
    {&vector_icons::kHistoryOldIcon, &kKoanHistoryIcon},
    {&kHistoryOldIcon, &kKoanHistoryIcon},
    // home
    {&vector_icons::kHomeIcon, &kKoanHomeIcon},
    {&kHomeIcon, &kKoanHomeIcon},
    {&vector_icons::kHomeOldIcon, &kKoanHomeIcon},
    {&kNavigateHomeChromeRefreshOldIcon, &kKoanHomeIcon},
    {&kNavigateHomeTouchOldIcon, &kKoanHomeIcon},
    // link
    {&vector_icons::kLinkIcon, &kKoanLinkIcon},
    {&vector_icons::kLinkOldIcon, &kKoanLinkIcon},
    {&kLinkChromeRefreshOldIcon, &kKoanLinkIcon},
    // edit-copy
    {&vector_icons::kContentCopyIcon, &kKoanEditCopyIcon},
    {&vector_icons::kContentCopyOldIcon, &kKoanEditCopyIcon},
    {&kCopyOldIcon, &kKoanEditCopyIcon},
    {&kCopyMenuOldIcon, &kKoanEditCopyIcon},
    // edit-cut
    {&kContentCutIcon, &kKoanEditCutIcon},
    {&kCutMenuOldIcon, &kKoanEditCutIcon},
    // edit-paste
    {&vector_icons::kContentPasteIcon, &kKoanEditPasteIcon},
    {&kContentPasteIcon, &kKoanEditPasteIcon},
    {&vector_icons::kContentPasteOldIcon, &kKoanEditPasteIcon},
    {&kPasteMenuOldIcon, &kKoanEditPasteIcon},
    // settings
    {&vector_icons::kSettingsIcon, &kKoanSettingsIcon},
    {&kSettingsIcon, &kKoanSettingsIcon},
    {&vector_icons::kSettingsChromeRefreshOldIcon, &kKoanSettingsIcon},
    {&vector_icons::kSettingsOldIcon, &kKoanSettingsIcon},
    {&kSettingsMenuOldIcon, &kKoanSettingsIcon},
    {&vector_icons::kSettingsOutlineOldIcon, &kKoanSettingsIcon},
    // info
    {&vector_icons::kInfoIcon, &kKoanInfoIcon},
    {&kInfoIcon, &kKoanInfoIcon},
    {&views::kInfoIcon, &kKoanInfoIcon},
    {&omnibox::kInfoIcon, &kKoanInfoIcon},
    {&vector_icons::kInfoRefreshOldIcon, &kKoanInfoIcon},
    {&views::kInfoChromeRefreshOldIcon, &kKoanInfoIcon},
    {&kInfoOldIcon, &kKoanInfoIcon},
    {&views::kInfoOldIcon, &kKoanInfoIcon},
    {&vector_icons::kInfoOutlineOldIcon, &kKoanInfoIcon},
    {&omnibox::kPageInfoCustomIcon, &kKoanInfoIcon},
    // security
    {&vector_icons::kLockIcon, &kKoanSecurityIcon},
    {&vector_icons::kLockOldIcon, &kKoanSecurityIcon},
    {&vector_icons::kLockFilledIcon, &kKoanSecurityIcon},
    {&vector_icons::kHttpsValidOldIcon, &kKoanSecurityIcon},
    // security-broken
    {&vector_icons::kNoEncryptionIcon, &kKoanSecurityBrokenIcon},
    {&vector_icons::kNoEncryptionOldIcon, &kKoanSecurityBrokenIcon},
    {&omnibox::kHttpOldIcon, &kKoanSecurityBrokenIcon},
    {&omnibox::kHttpChromeRefreshOldIcon, &kKoanSecurityBrokenIcon},
    // security-warning
    {&vector_icons::kNotSecureWarningChromeRefreshOldIcon,
     &kKoanSecurityWarningIcon},
    {&vector_icons::kNotSecureWarningOldIcon, &kKoanSecurityWarningIcon},
    // extension
    {&vector_icons::kExtensionChromeRefreshOldIcon, &kKoanExtensionIcon},
    {&vector_icons::kExtensionOldIcon, &kKoanExtensionIcon},
    {&vector_icons::kExtensionFilledIcon, &kKoanExtensionIcon},
    {&omnibox::kExtensionFilledIcon, &kKoanExtensionIcon},
    {&vector_icons::kChromeExtensionIcon, &kKoanExtensionIcon},
    {&omnibox::kExtensionAppOldIcon, &kKoanExtensionIcon},
    {&vector_icons::kExtensionOnOldIcon, &kKoanExtensionIcon},
    // folder
    {&vector_icons::kFolderChromeRefreshOldIcon, &kKoanFolderIcon},
    {&vector_icons::kFolderOldIcon, &kKoanFolderIcon},
    {&vector_icons::kFolderFilledIcon, &kKoanFolderIcon},
    {&vector_icons::kFolderOpenIcon, &kKoanFolderIcon},
    {&vector_icons::kFolderOpenOldIcon, &kKoanFolderIcon},
    {&vector_icons::kFolderFlippableIcon, &kKoanFolderIcon},
    // help
    {&vector_icons::kHelpIcon, &kKoanHelpIcon},
    {&vector_icons::kHelpOldIcon, &kKoanHelpIcon},
    {&vector_icons::kHelpOutlineOldIcon, &kKoanHelpIcon},
    {&vector_icons::kHelpFilledIcon, &kKoanHelpIcon},
    {&kHelpMenuOldIcon, &kKoanHelpIcon},
    // pin
    {&kKeepIcon, &kKoanPinIcon},
    {&views::kKeepIcon, &kKoanPinIcon},
    {&kKeepOldIcon, &kKoanPinIcon},
    {&views::kKeepFilledIcon, &kKoanPinIcon},
    {&views::kPinOldIcon, &kKoanPinIcon},
    // unpin
    {&kKeepOffIcon, &kKoanUnpinIcon},
    {&kKeepOffOldIcon, &kKoanUnpinIcon},
    {&views::kUnpinOldIcon, &kKoanUnpinIcon},
    // Zen's reset button on a pinned tab ("Unload and switch to tab", #150).
    {&kRemoveIcon, &kKoanUnpinIcon},
    {&kRemoveOldIcon, &kKoanUnpinIcon},
    // picture-in-picture
    {&vector_icons::kPictureInPictureAltIcon, &kKoanPictureInPictureIcon},
    {&vector_icons::kPictureInPictureAltOldIcon, &kKoanPictureInPictureIcon},
    {&vector_icons::kPictureInPictureIcon, &kKoanPictureInPictureIcon},
    {&vector_icons::kPictureInPictureOldIcon, &kKoanPictureInPictureIcon},
    {&kPictureInPictureAltIcon, &kKoanPictureInPictureIcon},
    {&kPictureInPictureAltOldIcon, &kKoanPictureInPictureIcon},
    // persistent-storage
    {&vector_icons::kDatabaseIcon, &kKoanPersistentStorageIcon},
    {&vector_icons::kDatabaseOldIcon, &kKoanPersistentStorageIcon},
    // cookies-fill
    {&vector_icons::kCookieIcon, &kKoanCookiesFillIcon},
    {&vector_icons::kCookieChromeRefreshOldIcon, &kKoanCookiesFillIcon},
    // geo
    {&vector_icons::kLocationOnIcon, &kKoanGeoIcon},
    {&vector_icons::kLocationOnOldIcon, &kKoanGeoIcon},
    {&vector_icons::kLocationOnChromeRefreshOldIcon, &kKoanGeoIcon},
    // geo-blocked
    {&vector_icons::kLocationOffIcon, &kKoanGeoBlockedIcon},
    {&vector_icons::kLocationOffChromeRefreshOldIcon, &kKoanGeoBlockedIcon},
    // microphone
    {&vector_icons::kMicIcon, &kKoanMicrophoneIcon},
    {&vector_icons::kMicOldIcon, &kKoanMicrophoneIcon},
    {&vector_icons::kMicChromeRefreshOldIcon, &kKoanMicrophoneIcon},
    {&vector_icons::kMicFilledIcon, &kKoanMicrophoneIcon},
    // microphone-blocked
    {&vector_icons::kMicOffIcon, &kKoanMicrophoneBlockedIcon},
    {&vector_icons::kMicOffChromeRefreshOldIcon, &kKoanMicrophoneBlockedIcon},
    // desktop-notification
    {&vector_icons::kNotificationsIcon, &kKoanDesktopNotificationIcon},
    {&vector_icons::kNotificationsOldIcon, &kKoanDesktopNotificationIcon},
    {&vector_icons::kNotificationsChromeRefreshOldIcon,
     &kKoanDesktopNotificationIcon},
    {&vector_icons::kNotificationsFilledIcon, &kKoanDesktopNotificationIcon},
    // desktop-notification-blocked
    {&vector_icons::kNotificationsOffIcon,
     &kKoanDesktopNotificationBlockedIcon},
    {&vector_icons::kNotificationsOffChromeRefreshOldIcon,
     &kKoanDesktopNotificationBlockedIcon},
    // midi
    {&vector_icons::kPianoIcon, &kKoanMidiIcon},
    {&vector_icons::kMidiOldIcon, &kKoanMidiIcon},
    {&vector_icons::kMidiChromeRefreshOldIcon, &kKoanMidiIcon},
    // screen
    {&vector_icons::kScreenShareIcon, &kKoanScreenIcon},
    {&vector_icons::kScreenShareOldIcon, &kKoanScreenIcon},
    {&vector_icons::kSelectWindowIcon, &kKoanScreenIcon},
    {&vector_icons::kSelectWindowOldIcon, &kKoanScreenIcon},
    {&vector_icons::kSelectWindowChromeRefreshOldIcon, &kKoanScreenIcon},
    {&vector_icons::kDesktopWindowsIcon, &kKoanScreenIcon},
    {&kDesktopWindowsIcon, &kKoanScreenIcon},
    {&vector_icons::kDesktopWindowsOldIcon, &kKoanScreenIcon},
    {&kDesktopWindowsChromeRefreshOldIcon, &kKoanScreenIcon},
    // print
    {&vector_icons::kPrintIcon, &kKoanPrintIcon},
    {&kPrintIcon, &kKoanPrintIcon},
    {&vector_icons::kPrinterOldIcon, &kKoanPrintIcon},
    {&kPrintMenuOldIcon, &kKoanPrintIcon},
    // fullscreen
    {&kFullscreenIcon, &kKoanFullscreenIcon},
    {&kFullscreenOldIcon, &kKoanFullscreenIcon},
    {&kFullscreenRefreshOldIcon, &kKoanFullscreenIcon},
    // open
    {&kOpenInNewIcon, &kKoanOpenIcon},
    {&views::kOpenInNewIcon, &kKoanOpenIcon},
    {&kOpenInNewOldIcon, &kKoanOpenIcon},
    {&kOpenInNewChromeRefreshOldIcon, &kKoanOpenIcon},
    {&vector_icons::kOpenInNewFlippableIcon, &kKoanOpenIcon},
    {&vector_icons::kLaunchOldIcon, &kKoanOpenIcon},
    {&views::kLaunchOldIcon, &kKoanOpenIcon},
    {&vector_icons::kLaunchChromeRefreshOldIcon, &kKoanOpenIcon},
    // zoom-out
    {&kZoomOutIcon, &kKoanZoomOutIcon},
    {&kZoomMinusChromeRefreshOldIcon, &kKoanZoomOutIcon},
    {&kZoomMinusMenuRefreshOldIcon, &kKoanZoomOutIcon},
    {&kZoomMinusOldIcon, &kKoanZoomOutIcon},
    // tab
    {&kTabIcon, &kKoanTabIcon},
    {&views::kTabIcon, &kKoanTabIcon},
    {&omnibox::kTabIcon, &kKoanTabIcon},
    {&kTabOldIcon, &kKoanTabIcon},
    // private-window
    {&kIncognitoIcon, &kKoanPrivateWindowIcon},
    {&views::kIncognitoIcon, &kKoanPrivateWindowIcon},
    {&omnibox::kIncognitoIcon, &kKoanPrivateWindowIcon},
    {&kIncognitoOldIcon, &kKoanPrivateWindowIcon},
    {&kIncognitoRefreshMenuOldIcon, &kKoanPrivateWindowIcon},
    {&omnibox::kIncognitoCr2023OldIcon, &kKoanPrivateWindowIcon},
    {&views::kNewIncognitoWindowOldIcon, &kKoanPrivateWindowIcon},
    // window
    {&kNewWindowIcon, &kKoanWindowIcon},
    {&views::kNewWindowIcon, &kKoanWindowIcon},
    {&kNewWindowOldIcon, &kKoanWindowIcon},
    {&views::kNewWindowOldIcon, &kKoanWindowIcon},
    // sidebar
    // The sidebar toggle in the top row: Chromium's button for collapsing the
    // vertical tab strip draws these (one per state), Zen's draws sidebar.svg.
    {&views::kMenuCloseCustomIcon, &kKoanSidebarIcon},
    {&views::kMenuOpenIcon, &kKoanSidebarIcon},
    {&views::kMenuOpenOldIcon, &kKoanSidebarIcon},
    {&kDockToLeftIcon, &kKoanSidebarIcon},
    {&kDockToLeftOldIcon, &kKoanSidebarIcon},
    {&kLeftPanelCloseFlippableIcon, &kKoanSidebarIcon},
    {&kLeftPanelCloseOldIcon, &kKoanSidebarIcon},
    // sidebar-right
    {&kDockToRightIcon, &kKoanSidebarRightIcon},
    {&kDockToRightOldIcon, &kKoanSidebarRightIcon},
    {&kRightPanelCloseFlippableIcon, &kKoanSidebarRightIcon},
    {&kRightPanelCloseOldIcon, &kKoanSidebarRightIcon},
    // split
    {&kSplitSceneIcon, &kKoanSplitIcon},
    {&kSplitSceneOldIcon, &kKoanSplitIcon},
    // menu
    {&kMoreVertIcon, &kKoanMenuIcon},
    {&views::kMoreHorizIcon, &kKoanMenuIcon},
    {&kBrowserToolsChromeRefreshOldIcon, &kKoanMenuIcon},
    {&kBrowserToolsOldIcon, &kKoanMenuIcon},
    {&kOverflowButtonOldIcon, &kKoanMenuIcon},
    {&kMoreToolsMenuOldIcon, &kKoanMenuIcon},
    // trash
    {&kDeleteIcon, &kKoanTrashIcon},
    {&views::kDeleteIcon, &kKoanTrashIcon},
    {&kDeleteOldIcon, &kKoanTrashIcon},
    {&kTrashCanRefreshOldIcon, &kKoanTrashIcon},
    {&kTrashCanOldIcon, &kKoanTrashIcon},
    // media-play
    {&vector_icons::kPlayArrowIcon, &kKoanMediaPlayIcon},
    {&vector_icons::kPlayArrowOldIcon, &kKoanMediaPlayIcon},
    {&vector_icons::kPlayArrowChromeRefreshOldIcon, &kKoanMediaPlayIcon},
    {&vector_icons::kPlayArrowFilledFlippableIcon, &kKoanMediaPlayIcon},
    // media-pause
    {&vector_icons::kPauseIcon, &kKoanMediaPauseIcon},
    {&vector_icons::kPauseFilledIcon, &kKoanMediaPauseIcon},
    {&vector_icons::kPauseOldIcon, &kKoanMediaPauseIcon},
    {&vector_icons::kPauseChromeRefreshOldIcon, &kKoanMediaPauseIcon},
    // media-next
    {&vector_icons::kSkipNextIcon, &kKoanMediaNextIcon},
    {&vector_icons::kSkipNextFilledIcon, &kKoanMediaNextIcon},
    {&vector_icons::kSkipNextOldIcon, &kKoanMediaNextIcon},
    {&vector_icons::kMediaNextTrackOldIcon, &kKoanMediaNextIcon},
    // media-previous
    {&vector_icons::kSkipPreviousIcon, &kKoanMediaPreviousIcon},
    {&vector_icons::kSkipPreviousFilledIcon, &kKoanMediaPreviousIcon},
    {&vector_icons::kSkipPreviousOldIcon, &kKoanMediaPreviousIcon},
    {&vector_icons::kMediaPreviousTrackOldIcon, &kKoanMediaPreviousIcon},
    // arrow-right
    {&kChevronRightIcon, &kKoanArrowRightIcon},
    {&kChevronRightChromeRefreshOldIcon, &kKoanArrowRightIcon},
    {&kChevronRightOldIcon, &kKoanArrowRightIcon},
    {&kKeyboardArrowRightIcon, &kKoanArrowRightIcon},
    {&vector_icons::kKeyboardArrowRightFlippableIcon, &kKoanArrowRightIcon},
    // chevron
    {&vector_icons::kKeyboardArrowDownIcon, &kKoanChevronIcon},
    {&kKeyboardArrowDownIcon, &kKoanChevronIcon},
    {&kKeyboardArrowDownChromeRefreshOldIcon, &kKoanChevronIcon},
    {&kKeyboardArrowDownOldIcon, &kKoanChevronIcon},
    {&vector_icons::kExpandMoreOldIcon, &kKoanChevronIcon},
    // dart-down
    {&views::kArrowDropDownIcon, &kKoanDartDownIcon},
    {&views::kArrowDropDownOldIcon, &kKoanDartDownIcon},
    // check
    {&kCheckIcon, &kKoanCheckIcon},
    {&views::kCheckIcon, &kKoanCheckIcon},
    {&kCheckOldIcon, &kKoanCheckIcon},
    {&kCheckSmallIcon, &kKoanCheckIcon},
    {&views::kCheckSmallIcon, &kKoanCheckIcon},
    // screenshot
    {&kScreenshotRegionIcon, &kKoanScreenshotIcon},
    {&kSharingHubScreenshotOldIcon, &kKoanScreenshotIcon},
    // save
    {&kFileSaveIcon, &kKoanSaveIcon},
    {&kFileSaveChromeRefreshOldIcon, &kKoanSaveIcon},
    {&kFileSaveOldIcon, &kKoanSaveIcon},
    {&kSavePageOldIcon, &kKoanSaveIcon},
    // search-page
    {&kFindInPageIcon, &kKoanSearchPageIcon},
    {&omnibox::kFindInPageIcon, &kKoanSearchPageIcon},
    {&omnibox::kFindInPageChromeRefreshOldIcon, &kKoanSearchPageIcon},
    // passwords
    {&kPasswordIcon, &kKoanPasswordsIcon},
    {&vector_icons::kPasswordManagerIcon, &kKoanPasswordsIcon},
    {&vector_icons::kPasswordManagerOldIcon, &kKoanPasswordsIcon},
    {&kKeyOldIcon, &kKoanPasswordsIcon},
    {&kPasswordFieldOldIcon, &kKoanPasswordsIcon},
    // translations
    {&omnibox::kTranslateIcon, &kKoanTranslationsIcon},
    {&kTranslateOldIcon, &kKoanTranslationsIcon},
    {&vector_icons::kGTranslateIcon, &kKoanTranslationsIcon},
    {&kGTranslateIcon, &kKoanTranslationsIcon},
    // share
    {&omnibox::kShareIcon, &kKoanShareIcon},
    {&omnibox::kShareChromeRefreshOldIcon, &kKoanShareIcon},
// The location bar's share icon, per platform (omnibox's BUILD.gn lists
// them under is_mac / is_linux).
#if BUILDFLAG(IS_MAC)
    {&omnibox::kIosShareIcon, &kKoanShareIcon},
    {&omnibox::kShareMacChromeRefreshOldIcon, &kKoanShareIcon},
#elif BUILDFLAG(IS_LINUX)
    {&omnibox::kSendIcon, &kKoanShareIcon},
    {&omnibox::kShareLinuxChromeRefreshOldIcon, &kKoanShareIcon},
#endif
    {&vector_icons::kSendIcon, &kKoanShareIcon},
    {&vector_icons::kSendOldIcon, &kKoanShareIcon},
    // bookmark-hollow
    {&kStarIcon, &kKoanBookmarkHollowIcon},
    {&omnibox::kStarIcon, &kKoanBookmarkHollowIcon},
    {&omnibox::kStarChromeRefreshOldIcon, &kKoanBookmarkHollowIcon},
    {&omnibox::kBookmarkChromeRefreshOldIcon, &kKoanBookmarkHollowIcon},
    {&omnibox::kBookmarkOldIcon, &kKoanBookmarkHollowIcon},
    // bookmark
    {&vector_icons::kStarFilledIcon, &kKoanBookmarkIcon},
    {&omnibox::kStarFilledIcon, &kKoanBookmarkIcon},
    {&omnibox::kStarActiveChromeRefreshOldIcon, &kKoanBookmarkIcon},
    // bookmark-star-on-tray
    {&kBookmarksSidePanelOldIcon, &kKoanBookmarkStarOnTrayIcon},
    {&kBookmarksSidePanelRefreshOldIcon, &kKoanBookmarkStarOnTrayIcon},
    {&kBookmarkManagerIcon, &kKoanBookmarkStarOnTrayIcon},
    {&kBookmarksListsMenuOldIcon, &kKoanBookmarkStarOnTrayIcon},
    // page-portrait
    {&omnibox::kPageChromeRefreshOldIcon, &kKoanPagePortraitIcon},
    {&vector_icons::kDescriptionIcon, &kKoanPagePortraitIcon},
    {&vector_icons::kDescriptionOldIcon, &kKoanPagePortraitIcon},
    {&vector_icons::kInsertDriveFileOutlineOldIcon, &kKoanPagePortraitIcon},
    // tracking-protection
    {&vector_icons::kShieldIcon, &kKoanTrackingProtectionIcon},
    {&kGuardianIcon, &kKoanTrackingProtectionIcon},
    {&kGuardianOldIcon, &kKoanTrackingProtectionIcon},
    {&kPrivacyTipIcon, &kKoanTrackingProtectionIcon},
    {&kPrivacyTipOldIcon, &kKoanTrackingProtectionIcon},
    // permissions
    {&vector_icons::kTuneIcon, &kKoanPermissionsIcon},
    {&omnibox::kSecurePageInfoChromeRefreshOldIcon, &kKoanPermissionsIcon},
    // drag-indicator
    {&views::kDragGeneralSelectionCustomIcon, &kKoanDragIndicatorIcon},
    // video
    {&vector_icons::kVideocamIcon, &kKoanVideoIcon},
    {&vector_icons::kVideocamOldIcon, &kKoanVideoIcon},
    // video-blocked
    {&vector_icons::kVideocamOffIcon, &kKoanVideoBlockedIcon},
    // tab-audio-playing-small
    {&kSpeakerIcon, &kKoanTabAudioPlayingSmallIcon},
    {&kSpeakerOldIcon, &kKoanTabAudioPlayingSmallIcon},
    // media-unmute
    {&vector_icons::kVolumeUpIcon, &kKoanMediaUnmuteIcon},
    // media-mute
    {&vector_icons::kVolumeOffIcon, &kKoanMediaMuteIcon},
    // u-turn-to-left
    {&vector_icons::kUndoIcon, &kKoanUTurnToLeftIcon},
    // default-favicon
    {&vector_icons::kGlobeIcon, &kKoanDefaultFaviconIcon},
    {&kGlobeIcon, &kKoanDefaultFaviconIcon},
    {&vector_icons::kGlobeOldIcon, &kKoanDefaultFaviconIcon},
    {&kGlobeOldIcon, &kKoanDefaultFaviconIcon},
};

using IconMap = base::flat_map<const gfx::VectorIcon*, const gfx::VectorIcon*>;

const IconMap& Table() {
  static const base::NoDestructor<IconMap> map([] {
    std::vector<std::pair<const gfx::VectorIcon*, const gfx::VectorIcon*>>
        pairs;
    for (const KoanIconEntry& entry : kEntries) {
      pairs.emplace_back(entry.chromium, entry.koan);
    }
    return IconMap(std::move(pairs));
  }());
  return *map;
}

#if DCHECK_IS_ON()
// --koan-icon-trace: the first time an icon is asked for, a line in a file.
// Debug builds only (the file is appended to from a thread pool task, off the
// paint thread).
class Trace {
 public:
  explicit Trace(const base::FilePath& path) : path_(path) {}

  void Record(const gfx::VectorIcon& icon,
              const gfx::VectorIcon& painted,
              int dip_size) {
    base::AutoLock lock(lock_);
    if (!seen_.insert({&icon, dip_size}).second) {
      return;
    }
    std::string status = "unmapped";
    if (&icon != &painted) {
      status = std::string("mapped:") + painted.name;
    } else if (std::string(icon.name).starts_with("kKoan")) {
      status = "koan";
    }
    if (!task_runner_) {
      task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN});
    }
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(
                       [](const base::FilePath& path, const std::string& line) {
                         base::AppendToFile(path, line);
                       },
                       path_,
                       std::string(icon.name) + "\t" + status + "\t" +
                           std::to_string(dip_size) + "\n"));
  }

 private:
  const base::FilePath path_;
  base::Lock lock_;
  std::set<std::pair<const gfx::VectorIcon*, int>> seen_ GUARDED_BY(lock_);
  scoped_refptr<base::SequencedTaskRunner> task_runner_ GUARDED_BY(lock_);
};

Trace* g_trace = nullptr;
#endif  // DCHECK_IS_ON()

const gfx::VectorIcon& Override(const gfx::VectorIcon& icon, int dip_size) {
  const gfx::VectorIcon& painted = KoanIconFor(icon);
#if DCHECK_IS_ON()
  if (g_trace) {
    g_trace->Record(icon, painted, dip_size);
  }
#endif
  return painted;
}

}  // namespace

base::span<const KoanIconEntry> KoanIconTable() {
  return kEntries;
}

const gfx::VectorIcon& KoanIconFor(const gfx::VectorIcon& icon) {
  const IconMap& table = Table();
  const auto it = table.find(&icon);
  return it == table.end() ? icon : *it->second;
}

void InstallVectorIconOverride() {
#if DCHECK_IS_ON()
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kIconTrace)) {
    static base::NoDestructor<Trace> trace(
        command_line->GetSwitchValuePath(switches::kIconTrace));
    g_trace = trace.get();
  }
#endif
  gfx::SetVectorIconOverrideForKoan(&Override);
}

}  // namespace koan
