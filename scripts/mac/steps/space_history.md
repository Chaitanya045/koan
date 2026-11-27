# Mac steps: history per Space profile (#474)

These steps are for the Mac loop and the hands-on tester. Use a DCHECK build and a profile past the welcome, with
two Spaces: **Personal** (Default profile) and **Work** (Set Profile > New Profile… > "Work"). The design note is
`.agent/notes/upstream/space-history.md`; `koan_space_history_browsertest.cc` covers the recording, the URL bar
filter and the Library's queries. These steps cover the native URL bar, menus and the Library, which the browser
test drives only through their controllers. Pick two sites you don't otherwise visit, here `example.org` and
`wikipedia.org`.

| # | Do | Expect |
|---|---|---|
| SH1 | In Personal, ⌘T, type `wikipedia.org`, Enter, then open an article. | The pages load. |
| SH2 | Switch to Work, ⌘T, type `example.org`, Enter. | The page loads in Work's profile. |
| SH3 | Still in Work, ⌘T and type `wiki`. | No Wikipedia row (no history, no inline completion of `wikipedia.org`). A search row for "wiki" is fine. |
| SH4 | In Work, ⌘T and type `exam`. | `example.org` is suggested and completes inline. |
| SH5 | Switch to Personal, ⌘T and type `exam`, then `wiki`. | No `example.org` row; Wikipedia is suggested. |
| SH6 | In Personal, ⌘T with nothing typed. | The open-tab rows are Personal's tabs only (no `example.org`), and the most visited sites have no `example.org`. |
| SH7 | In Personal, type `example.org` in full, then Enter. | The typed address still opens (as a navigation, not a search), in Personal's profile. |
| SH8 | In Work, type the title of the Wikipedia article open in Personal. | No Switch to Tab row for it. Shift+⌘A (tabs search) lists Work's tabs only. |
| SH9 | Drag the Work tab with `example.org` into Personal. In Personal, ⌘T and type `exam`. | Still no `example.org` suggestion from history (the moved tab keeps Work's profile), and no Switch to Tab for it. In Work, `exam` still suggests it. |
| SH10 | ⌘Y in Work, then Filter. | A "Which profile?" group after "What to look through": All profiles, Default, Work. Work is picked; the list has `example.org`, not Wikipedia. |
| SH11 | Pick Default, then All profiles. | Default lists Wikipedia (and everything from before), not `example.org`; All profiles lists both. Close and reopen the Library from Personal: Default is picked again. |
| SH12 | In the Library with Work picked, right-click `example.org` > Forget About This Site. | The question names "in the Work profile". After Forget, Work's list and Work's URL bar no longer have it, and a site you're signed into in Work is signed out there; Personal's login on the same site (if any) is kept. |
| SH13 | Right-click the History tab > Clear history > Last hour, with Default picked. | The question says "in the Default profile". Personal's last-hour pages go; Work's stay (pick Work to check). |
| SH14 | Same with All profiles picked. | It says "in every profile", and everything from the last hour goes. |
| SH15 | Settings > Tab Management > Workspaces: turn off "Keep history apart for each profile". | The URL bar suggests every profile's pages in every Space again, Switch to Tab offers every tab, and the Library has no profile filter. Turn it back on: separation returns at once. |
| SH16 | Incognito window: type `exam` and `wiki`. | Only Default's pages are suggested (Wikipedia), and the Library isn't there. |
| SH17 | Quit (⌘Q) and start again; repeat SH3-SH5. | Same results after the restart. No `DCHECK` or crash in the logs. |
