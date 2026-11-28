# Mac steps: managing profiles (#477)

These steps are for the Mac loop and the hands-on tester. Use a DCHECK build and a profile past the welcome. They
follow `space_containers.md` (#318), which makes the profiles. The design is in
`.agent/notes/upstream/space-containers.md` ("Managing profiles"). `KoanSpaceProfilesBrowserTest` covers the rules;
these steps cover the settings page, the native menus and the dialogs, which the tests don't click.

Before you start: make two profiles with Set Profile > New Profile…, "Work" and "Home". Give the Space "Work" the
Work profile and sign in to a site there (any site with a login, or `document.cookie='login=1; max-age=3600'` on
`https://example.com` in the console). Give another Space "Home" the Home profile and sign in there too. Sign in to
the same site in a Space without a profile. Open a second window (⌘N).

| # | Do | Expect |
|---|---|---|
| SP1 | ⌘, then Tab Management. | After Workspaces there is a **Profiles** group. Work and Home each show a coloured dot, the name in a field, "Used by “Work”" (or "“Home”"), the open tab count, the size on this device, eight colour dots and Clear Data… / Delete Profile…. |
| SP2 | Type "Work" in the settings search box. | The Profiles group shows, with both profiles. |
| SP3 | Rename Work to "Day job" and press Return. | Both windows: Set Profile on the Work Space lists "Day job", checked. The Create-a-Space form (+ > Create Space) lists it, and if the form was open on Work, its button now says "Day job". |
| SP4 | Click the red dot on Day job. | Both windows: a Work tab moved to another Space shows its line in red, without a reload. Set Profile shows a red dot. |
| SP5 | Rename it to an empty name, then press Return. | The name goes back, and "That name isn’t allowed" shows. |
| SP6 | Home > Clear Data…. | The question says what goes and that the tabs stay. After OK: "Cleared “Home”.", and a reload of the Home tab shows it signed out. The Work tab and the tab without a profile are still signed in. |
| SP7 | Day job > Delete Profile…. | The question names the Spaces that use it ("“Work” uses it and will use Default") and the open tabs that will close. Cancel: nothing changes. |
| SP8 | Delete Profile… again, then OK. | "Deleted “Day job”." The profile is gone from the list. In both windows the Work Space is still there with an empty tab; its Day job tabs closed; Set Profile on it has Default checked. Home and the tab without a profile are still signed in. |
| SP9 | ⌘T in the Work Space and open the site. | Signed in as in the default partition (the Space is Default now), not as Day job. |
| SP10 | History > Recently Closed (or ⇧⌘T) for a Day job tab closed before SP8. | It reopens blank and closes; nothing signs in as Day job. |
| SP11 | Quit and start again. Open `~/Library/Application Support/Koan/Default/Storage/ext/` in Finder. | No `koanuser…` folder for Day job (Home's folder is there). Home is still signed in. |
| SP12 | Settings > Privacy and security > Delete browsing data, "Cookies and other site data", All time. | The row reads "Cookies and other site data, in every profile". After Delete, the Home Space and the Space without a profile are both signed out. |
| SP13 | Sign in to two sites in Home and to the first in the Space without a profile. Library (⌥⇧L) > History, Which profile? Home, right-click the first site > Forget About This Site. | The question says "in the Home profile". After Forget, that site is signed out in Home and still signed in in the Space without a profile; Home's other site is still signed in. With All profiles, the question says "in every profile" and it goes everywhere. |
| SP14 | Incognito window: ⌘, opens Settings in the regular profile. | The same Profiles list; Incognito still has no Spaces and no profiles. |
| SP15 | Before SP8: visit two pages in the Work Space, one in Home and one without a profile; pin one Work tab as an Essential; let a page in Work show a notification. Then SP8. | The question names the Essential ("…, including 1 Essential, will close"). After OK: the notification is gone from Notification Center; Library > History (All profiles) has no Day job visits, Home's and Default's are there; ⌘T in the Work Space opens in Default at once. |
| SP16 | Delete a profile, then force-quit Koan at once (⌥⌘⎋ or `kill -9`) and start again. | Its folder is gone after the start (SP11) and its visits too (SP15); Home's and Default's visits, cookies and Spaces are all there. |

What else is kept with a profile (#477's last item). Install one extension (any, from the Chrome Web Store). Start Koan
with `--koan-container-proxy="Home=socks5://127.0.0.1:1080"`.

| # | Do | Expect |
|---|---|---|
| SP17 | ⌘, then Tab Management > Profiles. | Home's row: **Proxy** `socks5://127.0.0.1:1080`; Work's: **Direct**. Both: **Extensions off** All extensions on, and **Change…**. |
| SP18 | Change… on Work's row. | The Space menu's Extensions… dialog ("Extensions in Work") opens over the settings window. Turn the extension off, Done: Work's row lists it at once, with its icon. Home's row is unchanged. |
| SP19 | In the Work Space, right-click the header > Extensions…, turn it on again, Done. | Work's row says All extensions on, without a reload of the settings page. |
| SP20 | Give no Space the Home profile (Set Profile > Default on Home). | Home's Change… is greyed out; its tooltip says to give a Space the profile. |
| SP21 | Light and dark (⌘, > Look and Feel), shadcn and Zen look; make the window narrow. | The rows stay readable: labels in one column, values wrap under themselves, nothing scrolls sideways. |
