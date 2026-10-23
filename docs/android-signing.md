# Signing the Android build

Every Android app must be signed. The phone checks the signature when it installs the app, and when an update is installed over an older version it only accepts it if both were signed
with the **same key**. We use one key for the whole team, so a build made on any team computer installs over a build made on any other, with no uninstall in between.

The key is **a secret**. It is never in the repository, and never pasted in chat, tickets or the wiki. Whoever has it can publish an "update" of InNav that phones will accept.

## What there is

| | |
|---|---|
| keystore file | `innav-release.jks` (PKCS12, RSA 4096, valid 10000 days, created 2026-10-21) |
| alias | `innav` |
| passwords | the store password and the key password are the same (a PKCS12 keystore needs it) |
| certificate SHA-256 (public, safe to share) | `DF:01:32:43:74:CF:D6:EC:5C:4D:70:B3:88:9C:CC:B8:63:B2:57:21:FA:3E:31:06:D2:F6:58:F5:DB:17:42:9F` |

The file and a text file with the passwords are kept by the person who made them (the tech lead) in `Documents/InNavSigning/`, **outside** the repository, and are backed up in the team's
password manager. **If the key is lost, an app already given to people can never be updated again** (they would have to uninstall and install a new one), so keep the backup.

## Getting set up on another computer

1. Ask the tech lead for the two files, over a private channel (the password manager, or an encrypted file): `innav-release.jks` and the passwords.
2. Put `innav-release.jks` in `InNavi/Build/Android/` (git ignores `*.jks`, `*.keystore`, `*.p12`, `*.pfx`).
3. Make the file `InNavi/Saved/Config/Android/Engine.ini` (`Saved/` is ignored by git too) with:

   ```ini
   [/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]
   KeyStore=innav-release.jks
   KeyAlias=innav
   KeyStorePassword=<the password>
   KeyPassword=<the password>
   ```

   The passwords are not in `DefaultEngine.ini` on purpose: that file is in the repository.
4. Package as usual (see `docs/design/mobile-app-data-interface.md` and the packaging notes): the build is signed with the key.

Without steps 2 and 3 Unreal signs with its own **debug key** (one for each computer): such a build installs, but it cannot be installed over a build signed with the team key; uninstall the
old one first.

## Checking a build

```
%LOCALAPPDATA%\Android\Sdk\build-tools\36.0.0\apksigner.bat verify --verbose --print-certs InNavi-arm64.apk
```

It should say `Verifies` and show the SHA-256 above as the signer. A debug-signed build shows a certificate named "Android Debug".

## One file or two

`bPackageDataInsideApk=True` (in `DefaultEngine.ini`) puts the game's data inside the APK, so a build is one file to hand around. With `False` the data is a separate `.obb` file that must be
copied to `/sdcard/Android/obb/com.teampathfinder.innavi/` next to the install, and after every new build the OBB must be copied again.

## If the key might have leaked

Tell the tech lead at once. A new key cannot update apps signed with the old one; the apps already given out would have to be uninstalled and installed again. Make a new keystore with
`keytool -genkeypair -keystore innav-release.jks -storetype PKCS12 -alias innav -keyalg RSA -keysize 4096 -validity 10000`, and give it to the team the same way.
