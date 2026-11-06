# Mac steps: the compact reveal's animation CHECK (`current_motion_`)

Crash seen on c165r in the crash-path step "compact reveal/hide by pin, then reversed half-way": `browser_animation_controller.cc:659
Check failed: current_motion_` in `GroupData::UpdateCurrentValues` ← `AnimationProgressed`, 375 ms after "compact mode on, sidebar
hidden". It crashes release builds too (an upstream CHECK).

Cause: a keep-expanded lock held when the sidebar's collapse ends: taken during the collapse, or held from before it (the
Create-a-Space form's lock). Upstream then starts its expand animation from inside the
collapse's own ended notification (`VerticalTabStripRegionView::UpdateExpandOnHoverState()` runs from it), and
`BrowserAnimationController::GroupData::OnAnimationEnded()` clears its current motion *after* notifying, which wipes the motion the
new animation has just started; the next animation step fails the CHECK. The locks: the pin key (Cmd+Opt+S; Ctrl+Alt+S on the old
base), the edge hot zone with the pointer parked on it (an earlier crash-path step leaves it there, which is why 40 isolated runs
never crashed), the bubble hold, the flash. Those go through `TakeExpandedLock()`, which refuses until the collapse has settled,
and a posted `OnSidebarCollapseSettled()` takes what was refused. A lock held from before the collapse (the form, upstream's menu
locks) can't be refused, so a patch to `BrowserAnimationController` (chrome/browser/ui/animation/browser_animation_controller.cc)
keeps the motion that a `kEnded` listener starts instead of clearing it.

| # | Do | Expect |
|---|---|---|
| 1 | `MODE=pinrev` (`scripts/mac/vtabs.sh`), T27: 30 rounds with the pointer away, then 30 with it moved to the left edge (`ptr move 3 400`) and wiggled 1 px during the collapse: compact on; pin at +0..400 ms; pin again 100-250 ms later; check; compact off | Koan alive after every round, no `Check failed: current_motion_` in the log, no new `.ips`; the strip is out only while the pointer is at the edge, hidden after it leaves (a lock leak leaves it out), expanded with compact off. |
| 1b | T28: compact on, pin, **Ctrl+Opt+N** (the Create-a-Space form, which holds a lock; Ctrl on the Mac whatever the keyset), assert the form is shown, compact off, on (assert the sidebar is out, >= 163: the form's lock holds the collapsed strip), off, on (same); Esc; compact off; 5 rounds | No crash (this crashes without the BrowserAnimationController patch even with the lock gating), the form's lock keeps the sidebar out while it is open, the sidebar expanded at the end. |
| 1c | T29: native fullscreen (Cmd+Ctrl+F), pointer to x=1 as compact goes on, a wiggle to x=0 at +150 ms (the edge zone is 2 px); 3 rounds | The sidebar comes out at the end of the collapse (#171's edge reveal, with no hot zone to hover), goes when the pointer leaves, no crash. |
| 2 | Manual: Cmd+S (compact on) and at once press Cmd+Opt+S and leave it pinned; wait 1 s | The sidebar comes out when the collapse has finished (not at the key press) and stays out; Cmd+Opt+S unpins it. |
| 3 | Manual: park the pointer on the left screen edge, Cmd+S | The sidebar collapses and then comes out under the pointer, no crash; moving away hides it. |
| 4 | The crash-path's preceding steps, then the pin reversal, looped 10x | As 1. |
