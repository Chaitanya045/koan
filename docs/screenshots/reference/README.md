# Reference crops for the Mac visual checks

`scripts/mac/suite.sh` captures Koan's window in fixed states (`scripts/mac/visual/capture.mjs`) and compares named
regions with the PNGs here (`scripts/mac/visual/diff.py`, perceptual YIQ distance, a 0.5 % threshold). A model looks
at an image only when its region fails, and then only at that crop and its `-diff.png`.

Files are `<state>-<region>.png` (states and regions are in `scripts/mac/visual/regions.json`), at the Mac's backing
scale (2x on the MacBook Air).

**Making them** is the first run's job: `scripts/mac/suite.sh --only visual --record <Koan.app>` on a build whose look
has been checked against Zen 1.24b (the full-flow pass, `scripts/mac/full-flow.md`). Then commit the crops in their own
PR, saying which build and which display they came from. Re-record when a look change is intended, in the PR that makes
the change, and say why.
