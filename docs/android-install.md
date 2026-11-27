# Installing InNav on an Android phone

This is for teammates who want to **try the phone app** on their own phone. You do not need Unreal, Android Studio or any other tool, only the APK file and a phone.
(If you want to build the app yourself, see `docs/android-signing.md` instead.)

## What you need

- An **Android phone, Android 8 or newer** (API 26+), with a **64-bit ARM** processor. Nearly every phone from the last several years is. There is **no iPhone version**.
- Enough free space for the APK and the installed app's data.
- The file **`InNavi-m3-build-3-arm64.apk`** (one file; the game's data is inside it). It is not in git because it is big. Download it from the **[m3-build-3 release](https://github.com/GYQ-SIT/Pathfinder-InNav/releases/tag/m3-build-3)**
  ("Assets" -> `InNavi-m3-build-3-arm64.apk`). The repository is public, so no sign-in is needed;
  if the phone's browser does not let you download, download the file on a computer and send it to the phone (USB cable, a cloud drive or a chat app).

**`m3-build-3` (version 2.2, Android version code 12)** reads the packaged project files and uses the real campus route planner. GPS shows a rough position on the map, but there is no automatic indoor tracking: you choose the building and level, and you move through the steps of a route yourself. See [the real-data guide](mobile-real-data-guide.md) for the available data and test cases, and "Known limitations" in the repository's README.

**Updating:** `m3-build-3` is signed with the **team key** (see [Android signing](android-signing.md)). It installs over an earlier team-key build and keeps its saved places and preferences. It does **not** install over `mobile-test-2` or `mobile-test-3`, which use a separate key: uninstall the old app first. Uninstalling removes that app's saved places and preferences.

## Install

1. **Get the APK onto the phone.** Download it from the link you were given, or receive it in a chat app. If a chat app shows it as an attachment, save it to the phone first ("save to phone" / "open with other app").
2. **Tap the APK** in the file manager or the Downloads list.
3. The first time, Android says apps from this source are not allowed to install. Tap **Settings** and turn on **Allow from this source** (允许安装未知应用) for the app you opened the file with (Files, Chrome, WeChat, ...). Go back and tap **Install** again.
4. If Google Play Protect or the phone's security scan says the app is from an unknown developer or has not been scanned, choose **Install anyway** (仍然安装) or **Continue**. That is expected: this is a signed test release, not published in a store.
5. Tap **Open**. The first start takes up to about half a minute while the app sets up; later starts are quicker.

You should see **Explore** with sites from the packaged project library (the projects `SIT.json` and `E2_Filled.json`, both with the real E2/3 interior, and the footprint-only `map_extended.db`) and a tab bar at the bottom. The old prototype's mall/centre/demo-campus sites are available only when a build is launched with `-MobileMock`.

## Things to try

- **Explore -> a site card**: opens that site on the map.
- **Directory**: the map and the plan of a level. Tap a room, or search for a place ("Search buildings or places"). Drag the sheet's handle up and down, or tap it.
- **Get directions -> Start navigation**: the route, then walk it with "Next instruction" until "I have arrived".
- **The route page's From and To lists** scroll with your finger. **Level** and **Building** change where "My location" is.
- **The map**: drag to move it, pinch to zoom, and twist two fingers to turn it; the compass button shows while it is turned. The **Locate** button centres the map on where you are, and **2D** / **3D** switches the view.
- **Saved**: save a place on its page and it shows here; it is kept after you close the app.
- **Profile**: the night theme and larger text.

## Problems

| What you see | What to do |
|---|---|
| "App not installed" or "conflicts with an existing package" | The phone has an **older build signed with a different key** (an earlier test build). Uninstall InNav, then install again. Builds signed with the team key install over each other without uninstalling. |
| Tapping the APK does nothing, or the phone blocks it | Some phones (many Chinese Android skins) have an install guard or "pure mode" in the security settings. Turn it off for this install, or ask the tech lead for help. |
| "Not compatible with your device" or it will not open | The phone is probably 32-bit or older than Android 8. Tell us the model. |
| Black screen for a long time at the first start | Wait up to a minute. If it stays black, close the app, open it again, and tell us. |
| The top of the screen is cut off by the camera | Tell us the phone model, with a screenshot. The app should keep clear of the camera hole and the status bar. |
| A list will not scroll, or a button does not react | Tell us what you touched. A screenshot or a short screen recording helps most. |

## Updating and removing

- Builds signed with the same key and a higher version code can update each other and keep saved data. `mobile-test-2` and `mobile-test-3` share a separate key: build 3 updates build 2, but neither can update the team-key `mobile-test-1`. Switching between those signing-key families requires uninstalling the other build and removes its local app data.
- To **remove** the app: press and hold its icon -> App info -> Uninstall. This also deletes the saved places and settings.

## Reporting a problem

Send, in the team's chat or as an issue:

1. The phone's **model** and **Android version** (Settings -> About phone).
2. **What you did**, step by step, and **what you expected**.
3. A **screenshot or a screen recording**.
4. For a crash: roughly when it happened. If you have a computer with USB debugging turned on, `adb logcat -d > innav-log.txt` right after the crash gives us the log.
