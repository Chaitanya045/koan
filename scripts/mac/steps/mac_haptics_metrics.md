# Mac steps: haptics, Mac metrics, inactive windows (#80)

DCHECK build, fresh profile, a Mac with a Force Touch trackpad. `.mm` files changed: `koan_haptics_mac.mm` (only checked
by eye on Linux).

| # | Do | Expect |
|---|---|---|
| 1 | Drag a tab down the sidebar past three others, slowly | A tick at the second and third slot (not the first), as in Zen. |
| 2 | Drag a Space button past another in the bottom bar | A tick per slot. |
| 3 | Theme picker: drag the opacity slider end to end; step the texture dial | A tick per tenth / per step. |
| 4 | Settings > Look and Feel > Haptic feedback off; repeat 1-3 | No ticks. Back on: ticks again, no restart. Not shown off macOS. |
| 5 | Content card and Glance corners at 400% zoom (screenshot) | 7 pt radius on macOS 26 (6 pt on 15), the same as Zen 1.24b beside it; shadcn look: its xl radius, unchanged. |
| 6 | Two windows, translucent theme: click the other window | The first goes flat grey (no blur, no gradient) like Zen; back: the theme returns without a flash. Opaque theme: no change, like Zen. |
