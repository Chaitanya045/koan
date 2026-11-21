# Agent Space lifetime (#430)

## Source map and decisions

- Chromium's tab strip removes a group when its last tab leaves. A whole
  group moved to another window reports
  `TabGroupClosureReason::kDetachedToAnotherTabstrip`; that is a transfer,
  not the end of the group. Reuse these notifications without a new patch.
- Koan's normal user Spaces deliberately survive losing their last tab:
  `KoanWorkspaceController::OnTabGroupChanged()` remembers their metadata,
  then `EnsureOneSpaceShown()` recreates them with an empty placeholder.
  Keep this behavior for normal Spaces, including imports and window sync.
- `containers::OpenAgentTab()` creates an agent Space through the free
  `CreateBackgroundSpace()` helper. That helper now selects the explicit
  temporary variant; names, icons and tab URLs do not determine lifetime.
- Temporary group ids are tracked across windows. A detach keeps the
  marker; final group closure removes it and skips empty-Space recreation.
  The existing deleted-Space tracking also prevents a later tab restore
  from resurrecting the removed Space.
- `EndAgentContainer()` continues closing only its container's tabs. A user
  tab added to the agent Space keeps the nonempty Space alive. An agent tab
  moved into a normal user Space does not make that Space temporary.
- If no Space remains, the existing last-tab fallback creates a normal
  empty Space. It does not reuse the ended agent's identity or robot icon.

## Limits and verification

The marker is runtime state. Agent tabs do not restore across browser
restarts; user tabs retained in an agent Space can restore as an ordinary
user Space. Old orphan Spaces are not deleted by guessing from their names
or icons.

Five `KoanSpacesBrowserTest` cases exercise the actual agent-container
creation and cleanup APIs: task end in throwaway and user-partition modes,
closing/reopening the last agent tab, keeping a user's tab in an agent
Space, moving an agent tab into a user Space with a robot icon, and the
window's last-Space fallback. They require a rebuilt browser to run.
