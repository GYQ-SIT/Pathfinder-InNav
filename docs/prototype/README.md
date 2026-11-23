# Design prototypes

The UX team's interactive prototypes of the two apps. They are plain web pages (HTML, CSS and JavaScript, no server behind them), so
they are kept here with the rest of the project and can be opened from a checkout.

| Prototype | Folder | Hosted copy | What it shows |
|-----------|--------|-------------|---------------|
| Phone app | `docs/prototype/mobile/` | https://innavappprototype.onrender.com/ | Explore, Directory (map, search, filters), place, route with several destinations, walking, arrival, Saved, Profile. The header's version switch has **V2 "Map search"** (the app follows it) and V1 "Before playtests" |
| Editor | `docs/prototype/editor/` | https://innav-environment-editor.onrender.com/ | The project dashboard, the editing page (2D floor tools, 3D, stack), Floors, Rooms, Destinations, Navigation graph, Validation, Runtime preview |

**Where the files come from.** From the design team's own repositories, copied here as they are (not saved from the hosted pages, which
show only the files a page loads). When the files are copied, write under each row the repository and the commit they were taken from. Open
a prototype from a checkout by opening its `index.html`, or serve the folder (`python -m http.server` inside it).

**What they are for.** The prototypes are the design reference: the apps are built to them, and
[the UI gap list](../design/ui-gap-list.md) says where the apps differ from them or do not have a screen yet. When a prototype changes,
update the files here and the gap list in the same pull request.
