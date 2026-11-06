# Mac steps: the compact reveal's animation CHECK (`current_motion_`)

Crash seen on c165r in the crash-path step "compact reveal/hide by pin, then reversed half-way": `browser_animation_controller.cc:659
Check failed: current_motion_` in `GroupData::UpdateCurrentValues` ← `AnimationProgressed`, 375 ms after "compact mode on, sidebar
hidden". It crashes release builds too (an upstream CHECK).

Cause: a keep-expanded lock taken while the sidebar is still collapsing. Upstream then starts its expand animation from inside the
collapse's own ended notification (`VerticalTabStripRegionView::UpdateExpandOnHoverState()` runs from it), and
`BrowserAnimationController::GroupData::OnAnimationEnded()` clears its current motion *after* notifying, which wipes the motion the
new animation has just started; the next animation step fails the CHECK. The locks: the pin key (Cmd+Opt+S; Ctrl+Alt+S on the old
base), the edge hot zone with the pointer parked on it (an earlier crash-path step leaves it there, which is why 40 isolated runs
never crashed), the bubble hold, the flash. All now go through `TakeExpandedLock()`, which refuses until the collapse has settled,
and a posted `OnSidebarCollapseSettled()` takes what was refused.

| # | Do | Expect |
|---|---|---|
| 1 | `MODE=pinrev` (`scripts/mac/vtabs.sh`): 30 rounds, pointer away, then 30 with the pointer parked at the screen's left edge: compact on; pin at +0..400 ms; pin again 100-250 ms later; compact off | Koan alive after every round, no `Check failed: current_motion_` in the log, no new `.ips`. Without the fix the parked-pointer rounds crash within a few rounds. |
| 2 | Manual: Cmd+S (compact on) and at once press Cmd+Opt+S and leave it pinned; wait 1 s | The sidebar comes out when the collapse has finished (not at the key press) and stays out; Cmd+Opt+S unpins it. |
| 3 | Manual: park the pointer on the left screen edge, Cmd+S | The sidebar collapses and then comes out under the pointer, no crash; moving away hides it. |
| 4 | The crash-path's preceding steps, then the pin reversal, looped 10x | As 1. |
