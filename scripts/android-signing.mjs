// CI: signs the Android release APK. Run after `tauri android init`.
// Writes gen/android/keystore.properties from secrets and adds a release
// signing config to the generated app/build.gradle.kts.
import { readFileSync, writeFileSync } from "node:fs";

const root = new URL("../src-tauri/gen/android/", import.meta.url);
const { ANDROID_KEYSTORE_PATH, ANDROID_KEYSTORE_PASSWORD, ANDROID_KEY_ALIAS, ANDROID_KEY_PASSWORD } = process.env;
if (!ANDROID_KEYSTORE_PATH || !ANDROID_KEYSTORE_PASSWORD || !ANDROID_KEY_ALIAS) {
  console.error("Android signing secrets are missing (ANDROID_KEYSTORE_BASE64, ANDROID_KEYSTORE_PASSWORD, ANDROID_KEY_ALIAS)");
  process.exit(1);
}
writeFileSync(
  new URL("keystore.properties", root),
  [`storeFile=${ANDROID_KEYSTORE_PATH}`, `storePassword=${ANDROID_KEYSTORE_PASSWORD}`, `keyAlias=${ANDROID_KEY_ALIAS}`, `keyPassword=${ANDROID_KEY_PASSWORD || ANDROID_KEYSTORE_PASSWORD}`, ""].join("\n"),
);

const gradleUrl = new URL("app/build.gradle.kts", root);
let g = readFileSync(gradleUrl, "utf8");
if (!g.includes('create("release")')) {
  const imports = ["import java.util.Properties", "import java.io.FileInputStream"].filter((i) => !g.includes(i));
  g = imports.join("\n") + (imports.length ? "\n" : "") + g;
  const signing = `
    signingConfigs {
        create("release") {
            val props = Properties()
            props.load(FileInputStream(rootProject.file("keystore.properties")))
            storeFile = file(props["storeFile"] as String)
            storePassword = props["storePassword"] as String
            keyAlias = props["keyAlias"] as String
            keyPassword = props["keyPassword"] as String
        }
    }
`;
  const android = g.indexOf("android {");
  if (android < 0) throw new Error("build.gradle.kts: no android { block");
  g = g.slice(0, android + "android {".length) + signing + g.slice(android + "android {".length);
  const release = /getByName\("release"\)\s*\{/.exec(g);
  if (!release) throw new Error('build.gradle.kts: no getByName("release") block');
  const at = release.index + release[0].length;
  g = g.slice(0, at) + '\n            signingConfig = signingConfigs.getByName("release")' + g.slice(at);
  writeFileSync(gradleUrl, g);
}
console.log("android signing configured");
