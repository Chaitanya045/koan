# Mac steps: notifications in a Space's profile (#478, #530)

These steps are for the Mac loop and the hands-on tester. Use a DCHECK build and a profile past the welcome, with
Koan allowed to notify in System Settings > Notifications > Koan (banners). The design note is
`.agent/notes/upstream/space-containers.md`, "Notifications (#478)" and "Service-worker notifications (#530)". The
browser tests `KoanSpaceNotificationsBrowserTest.*` cover the permission, the label, the clicks, the routing and the
refusals with a stub display service; these steps cover macOS's own Notification Center, which they can't, and a
restart with a notification still on screen.

The test page is Peter Beverloo's notification generator, <https://tests.peter.sh/notification-generator/>. Its
**Persistent** box (on by default) picks a service-worker notification; off, a page notification.

| # | Do | Expect |
|---|---|---|
| SN1 | Make a Space "Work" with a new profile (Create-a-Space form > Profile > New Profile…). In it, open the test page. Click **Request permission**. | Koan's permission prompt appears at the URL bar. Allow. |
| SN2 | Untick **Persistent**, type "Hello" as Title, click **Display the notification**. | A macOS banner titled "Hello" with the subtitle "tests.peter.sh · Work". |
| SN3 | Switch to another Space (⌃1 or the bottom bar). Click the banner (or its entry in Notification Center). | Koan comes forward on the Work Space, with the test page's tab selected. |
| SN4 | In Work, display another one, switch to the other Space and minimise the window (⌘M). Click the banner's entry in Notification Center. | The window comes back and shows Work and the test page. |
| SN5 | In Work, tick **Persistent**, then **Display the notification**. | A banner (#530) with the subtitle "tests.peter.sh · Work". Push stays refused: **Subscribe** on the page fails ("push service not available"). |
| SN6 | Click the URL bar's site icon (the site panel). | Under the Share/Screenshot/Reader/Bookmark row: "Push notifications aren't available in this Space's profile. The site can notify you while its tab is open." |
| SN7 | In a Space without a profile, open the test page. | `Notification.permission` is already granted (no prompt: the answer is the site's). With Persistent on or off, Display shows a banner whose subtitle is just the site (upstream's). The site panel has no note. |
| SN8 | In that Space, with Persistent on, click the banner. | Upstream's behaviour: the page's service worker gets the click. |
| SN9 | Close the Work tab that showed SN2's notification, then click SN2's entry in Notification Center. | Koan shows the Work Space and opens tests.peter.sh in a new tab there (signed in as Work, if the site had a login). |
| SN10 | Agent control: start a task that opens the test page in an agent container and calls Request permission. | Denied without a prompt, as before (T-block-1). |
| SN11 | (#530) Make a Space "Home" with another new profile. Open the test page in Work, Home and a Space without a profile, Persistent on in each. In Work, Display a notification titled "From Work". | One banner, subtitle "tests.peter.sh · Work". |
| SN12 | Switch to Home. Click SN11's banner. | Only Work's tab of the page logs the click (the generator lists the service worker's events); Home's and the default tab's log nothing. Koan shows Work if the page focuses or opens a window. |
| SN13 | In the default Space, Display one titled "From Default" with the same **Tag** as SN11's. | Both banners stay (different profiles don't replace each other's). Clicking it reaches the default tab only. |
| SN14 | In Work, Display one more, then quit Koan (⌘Q) with the banner left in Notification Center. Start Koan, then click the entry. | Work's service worker gets the click (the page, reopened in Work, logs it if it keeps a log; a site that opens a window opens it in Work). No crash, nothing in the default Space. Repeat with every tab of Work closed before ⌘Q: the click still reaches Work. |
| SN15 | Any site whose service worker calls `clients.openWindow()` on a click (a web mail or calendar signed in in Work): get a notification, switch to another Space, click it. | The site's tab opens (or comes forward) in Work, signed in as Work. |
