# Releasing Inkleaf (Android signing, Google Drive sync)

Releases are built by GitHub Actions when a version tag is pushed: the Linux
AppImage, the Windows installer (and a zip of the same files) and the
Android APK. Installed apps see the new
release (Settings → Updates) and offer its download.

Everything below is a one-time setup. All keys go into the repository's
**Settings → Secrets and variables → Actions**. Keep a copy of every key
somewhere safe (a password manager): a lost Android key means installed
tablets can no longer be updated.

## 1. Android signing key

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

To try a build without making a release: every push to `main` runs **Actions
→ Qt app**; download the AppImage, Windows zip or APK from the run's
artifacts (or run it by hand: Run workflow).

Installing on a tablet: open the APK from the release page (or copy it over),
and allow "Install unknown apps" for the browser or file manager when asked.

## 2. Google sign-in key (Drive sync)

1. Open <https://console.cloud.google.com> and create a project ("Inkleaf").
2. **APIs & Services → Library**: enable the **Google Drive API**.
3. **Google Auth Platform** (OAuth consent screen):
   - First publish the app's web pages (home, privacy policy, terms, in
     `docs/`): on GitHub, **Settings → Pages → Build and deployment**, Source
     *Deploy from a branch*, branch `main`, folder `/docs`, Save. After a
     minute they are at `https://sadikbr.github.io/goodnotes-clone/`.
   - Branding: app name *Inkleaf*, your email as support and developer contact,
     and under App domain:
     - Application home page: `https://sadikbr.github.io/goodnotes-clone/`
     - Privacy policy: `https://sadikbr.github.io/goodnotes-clone/privacy.html`
     - Terms of service: `https://sadikbr.github.io/goodnotes-clone/terms.html`
     - Authorized domain: `sadikbr.github.io`
   - Audience: *External*. Then **Publish app** (in "Testing", Google ends
     sign-ins after 7 days and only allows listed test users).
   - Data access: add the scopes `.../auth/drive.appdata`, `openid` and `email`.
4. **Clients → Create client**, application type **Desktop app**, name
   *Inkleaf*. Copy the client ID and client secret. (Not *TVs and Limited
   Input devices*: Google doesn't allow `drive.appdata` with that kind of
   sign-in, and Inkleaf signs in through the browser.)

| Where | Name | Value |
|---|---|---|
| Secret | `GOOGLE_CLIENT_ID` | the client ID |
| Secret | `GOOGLE_CLIENT_SECRET` | the client secret |

Inkleaf asks only for its own hidden app folder in Drive (`drive.appdata`): it
can't see the user's other files. For a local build with sync, set
`INKLEAF_GOOGLE_CLIENT_ID` and `INKLEAF_GOOGLE_CLIENT_SECRET` in the
environment before building (the CMake build builds the core).

## 3. Making a release

```sh
npm run set-version 0.8.1          # package.json and the Rust crates
git commit -am "v0.8.1"
git tag v0.8.1
git push origin main v0.8.1
```

Or, without a tag: **Actions → Release → Run workflow** releases the version in
`package.json`.

The **Release** workflow checks the versions match, builds and checks the
downloads (the same build as every push, with the APK signed), attaches
them to the release and publishes it. Installed apps find it within a few
hours (or at once with Settings → Updates → Check now). Edit the release notes
on GitHub if you like; they are shown in the app's update banner.
