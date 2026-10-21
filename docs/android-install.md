# Installing InNav on an Android phone

This is for teammates who want to **try the phone app** on their own phone. You do not need Unreal, Android Studio or any other tool, only the APK file and a phone.
(If you want to build the app yourself, see `docs/android-signing.md` instead.)

## What you need

- An **Android phone, Android 8 or newer** (API 26+), with a **64-bit ARM** processor. Nearly every phone from the last several years is. There is **no iPhone version**.
- About **160 MB** of free space.
- The file **`InNavi-arm64.apk`** (about 159 MB, one file; the game's data is inside it). Ask the tech lead where the latest one is: it is not in git because it is big.

What you are installing is a **development build** with **made-up (mock) data**: three sites, their floor plans and places. It is for looking at and trying the screens, not for real navigation yet.

## Install

1. **Get the APK onto the phone.** Download it from the link you were given, or receive it in a chat app. If a chat app shows it as an attachment, save it to the phone first ("save to phone" / "open with other app").
2. **Tap the APK** in the file manager or the Downloads list.
3. The first time, Android says apps from this source are not allowed to install. Tap **Settings** and turn on **Allow from this source** (允许安装未知应用) for the app you opened the file with (Files, Chrome, WeChat, ...). Go back and tap **Install** again.
4. If Google Play Protect or the phone's security scan says the app is from an unknown developer or has not been scanned, choose **Install anyway** (仍然安装) or **Continue**. That is expected: the app is signed with the team's own key, not published in a store.
5. Tap **Open**. The first start takes up to about half a minute while the app sets up; later starts are quicker.

You should see the **Explore** page with three site cards (Paya Lebar Mall, Pathfinder Centre, SIT Punggol Campus) and a tab bar at the bottom.

## Things to try

- **Explore -> a site card**: opens that site on the map.
- **Directory**: the plan of a level. Tap a room, or pick a place from the list under "Where to?". Drag the sheet's handle up and down, or tap it.
- **Get directions -> Start navigation**: the route, then walk it with "Next instruction" until "I have arrived".
- **The route page's From and To lists** scroll with your finger. **Level** and **Building** change where "My location" is.
- **Map tools** (the three dots at the bottom right of the map): turn **Move** on, then drag to move the map and pinch with two fingers to zoom; the **-** and **+** buttons zoom a step.
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

- A **new build** installs over the old one: tap the new APK and choose **Update** / **Install**. Your saved places and settings stay.
- To **remove** the app: press and hold its icon -> App info -> Uninstall. This also deletes the saved places and settings.

## Reporting a problem

Send, in the team's chat or as an issue:

1. The phone's **model** and **Android version** (Settings -> About phone).
2. **What you did**, step by step, and **what you expected**.
3. A **screenshot or a screen recording**.
4. For a crash: roughly when it happened. If you have a computer with USB debugging turned on, `adb logcat -d > innav-log.txt` right after the crash gives us the log.
