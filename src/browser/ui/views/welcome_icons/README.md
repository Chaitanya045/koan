# Welcome screen: the Essentials' icons

The nine apps' favicons from Zen Browser
(`browser/content/browser/zen-images/favicons/*.svg`, MPL-2.0, each file
carries the MPL-2.0 header), rendered to 32px and 64px PNGs with
`rsvg-convert`. `github-dark-*.png` is Zen's dark-mode variant (the SVG's
`prefers-color-scheme: dark` style: a white mark).

They are embedded in the binary by the `welcome_icons_data` action
(`../BUILD.gn`). Listed in `THIRD_PARTY_NOTICES.md`. The logos are their
owners' trademarks, shown to name the apps.
