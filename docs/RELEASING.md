# Releasing Inkleaf (updates, Android, Google Drive sync)

Releases are built by GitHub Actions when a version tag is pushed. Installed
apps on Windows and Linux (AppImage) see the new release and offer to update;
the Android app tells the user a new APK is out.

Everything below is a one-time setup. All keys go into the repository's
**Settings → Secrets and variables → Actions**. Keep a copy of every key
somewhere safe (a password manager): a lost update or Android key means
installed apps can no longer be updated.

## 1. Update signing key (Windows and Linux auto-update)

On your computer, in the project folder:

```sh
npx tauri signer generate -w ~/.tauri/inkleaf.key
```

Choose a password when asked. This makes `inkleaf.key` (private) and
`inkleaf.key.pub` (public). Then add:

| Where | Name | Value |
|---|---|---|
| Secret | `TAURI_SIGNING_PRIVATE_KEY` | contents of `~/.tauri/inkleaf.key` |
| Secret | `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` | the password |
| Variable | `TAURI_UPDATER_PUBKEY` | contents of `~/.tauri/inkleaf.key.pub` |

Apps only install updates signed with this key.

## 2. Android signing key

```sh
keytool -genkey -v -keystore inkleaf.jks -keyalg RSA -keysize 2048 -validity 10000 -alias inkleaf
base64 -w0 inkleaf.jks > inkleaf.jks.b64      # macOS: base64 -i inkleaf.jks -o inkleaf.jks.b64
```

| Where | Name | Value |
|---|---|---|
| Secret | `ANDROID_KEYSTORE_BASE64` | contents of `inkleaf.jks.b64` |
| Secret | `ANDROID_KEYSTORE_PASSWORD` | the keystore password |
| Secret | `ANDROID_KEY_ALIAS` | `inkleaf` |
| Secret | `ANDROID_KEY_PASSWORD` | the key password (often the same) |

Android only installs an update signed with the same key as the installed app.

To try an APK without making a release: **Actions → Android test build → Run
workflow**, then download it from the run's artifacts.

Installing on a tablet: open the APK from the release page (or copy it over),
and allow "Install unknown apps" for the browser or file manager when asked.

## 3. Google sign-in key (Drive sync)

1. Open <https://console.cloud.google.com> and create a project ("Inkleaf").
2. **APIs & Services → Library**: enable the **Google Drive API**.
3. **Google Auth Platform** (OAuth consent screen):
   - Branding: app name *Inkleaf*, your email as support and developer contact.
   - Audience: *External*. Then **Publish app** (in "Testing", Google ends
     sign-ins after 7 days and only allows listed test users).
   - Data access: add the scopes `.../auth/drive.appdata`, `openid` and `email`.
4. **Clients → Create client**, application type **TVs and Limited Input
   devices**, name *Inkleaf*. Copy the client ID and client secret.

| Where | Name | Value |
|---|---|---|
| Secret | `GOOGLE_CLIENT_ID` | the client ID |
| Secret | `GOOGLE_CLIENT_SECRET` | the client secret |

Inkleaf asks only for its own hidden app folder in Drive (`drive.appdata`): it
can't see the user's other files. For a local build with sync, set
`INKLEAF_GOOGLE_CLIENT_ID` and `INKLEAF_GOOGLE_CLIENT_SECRET` in the
environment before `npm run tauri build`.

## 4. Making a release

```sh
npm run set-version 0.8.1          # package.json, tauri.conf.json, Cargo.toml
git commit -am "v0.8.1"
git tag v0.8.1
git push origin main v0.8.1
```

The **Release** workflow checks the versions match, builds the Windows
installer, the Linux AppImage and the Android APK, signs them, and publishes
the release with `latest.json`. Installed apps find it within a few hours (or
at once with Settings → Updates → Check now). Edit the release notes on GitHub
if you like; they are shown in the app's update banner.
