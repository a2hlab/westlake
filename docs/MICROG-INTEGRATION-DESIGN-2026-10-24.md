# microG on Westlake: integration design

Status: design, written 2026-10-24 for the HK top-1000 programme. Nothing here was built or run.
Evidence: microG GmsCore at commit `32bc8954ff872d0e1a05ddfae81561b6dbecef69` (master of
2026-09-29 = release v0.3.17.252432), cited as `path:line`; Westlake at `origin/main` 1b0fdda
(`source-closure/workspace/westlake`), cited as `path:line`; AOSP 15
(`source-closure/imports/frameworks-base`, android-15.0.0_r1); Google's current client libraries,
read from their class files in memory (versions in "How apps reach GMS"); the manifests of the 30
sampled commercial apps (`source-closure/inputs/corpus-top/ext/*/`), scanned with aapt2.

## Summary

**Recommendation.** Run microG inside every app process as a guest package, the way Westlake
already runs the sideloaded WebView provider. Stage microG's two APKs read-only beside the runtime:
GmsCore as `com.google.android.gms` and microG Companion as `com.android.vending`. Westlake's
package manager reports both as installed, enabled, non-system and signed with Google's 2008
certificate (SHA-1 `38918a45...5788`); every app keeps its real certificate. The design fits
because Android already loads Play-services code into app processes (Dynamite, which microG's loader
implements), and AOSP's ActivityThread runs another package's activities, services and providers
in-process whenever that package's uid equals the process uid. Seven Westlake changes do it:
`GuestPackages` in the package manager (plus permission answers), a `GuestHost` that loads guest
components, routes in the direct-launch activity manager (bind, start, `getContentProvider`,
broadcasts), guest activities in the app's task (with `getCallingActivity` and
`startActivityIntentSender` fixed), a real `LocalAccounts` AccountManager, a shared GMS directory
with a lease and a SQLite settings store, and a small Westlake flavor of microG.

**What the code says is required** (each verified in Google's current client libraries or microG):
`com.android.vending` must exist with the same certificate as GmsCore or every availability check
returns `SERVICE_INVALID`; Firebase will not use Play services unless
`com.google.android.c2dm.permission.SEND` reads as granted to `com.google.android.gms`; legacy Google
Sign-In fails with DEVELOPER_ERROR unless microG can see the calling activity; GmsCore must not be
marked a system app (DynamiteModule then uses the V1 loader that microG implements); microG's
reported version must stay below 26.12 (261200000) until microG implements the newer cloud-messaging
service. SafetyNet attestation is dead everywhere; Play Integrity can at best give BASIC, and on
this platform probably nothing.

**Where it breaks:** nothing runs while no app runs (push while closed); several copies of microG
run at once (one MCS session needs a lease, settings need a cross-process store); there is no
credential isolation (one OH uid for all apps, no per-app file sandbox in appspawn-x yet, login UI
in the app's process); microG's "caller is me" shortcuts are always true; DroidGuard runs in the
app's process. The phase-2 answer is a headless GMS process (gmsd) spawned by appspawn-x, holding
push, check-in, accounts and token minting, reached over per-app channels that appspawn-x
authenticates, with headless app spawns for messages and an OpenHarmony notification bridge.

**Plan:** phase 0 groundwork and spikes (guest loading, DroidGuard login) 3 ew; phase 1 MVP (GMS
present, checks, broker, Dynamite, accounts, sign-in) 11.5 ew; phase 2 FCM while an app runs 6 ew;
phase 3 push while closed plus credential isolation 10.5 ew; phase 4 location, maps, integrity 11
ew, in parallel. About 42 ew, 13-14 weeks with four engineers.

**Top risks:** Google login (DroidGuard-gated, upstream issue #3195) could block all sign-in; no
passing Play Integrity verdict for 22 of 30 apps that ask; legal and policy exposure from
presenting Google's identities and running DroidGuard in a commercial product; phase 1 is unsafe
for real users; notifications are not bridged to OpenHarmony yet, so push is invisible until they
are.

## How apps reach GMS

The availability gate, the service broker, a handful of content providers, Dynamite code loading,
messaging intents and a few activities are the whole surface. Each item below says what Google's
client library does (class files of play-services-basement 18.12.0, base 18.11.1, auth 22.0.0,
auth-api-phone 18.3.1, auth-base 18.3.4, location 21.4.0, maps 20.1.0, cloud-messaging 17.4.0,
safetynet 18.1.0, firebase-messaging 26.0.1, firebase-iid 21.1.0, firebase-installations 20.0.0,
firebase-auth 25.0.0, Play integrity 1.6.0, credentials-play-services-auth 1.6.0, googleid 1.2.1;
obfuscated class names are as found in those versions) and what microG answers with.

What the corpus uses, from the 30 sampled apps' manifests: all 30 declare Firebase's
`FirebaseInstanceIdReceiver`, a `com.google.firebase.MESSAGING_EVENT` service, the c2dm RECEIVE
permission, the `com.google.android.gms.version` meta-data, `GoogleApiActivity` and `<queries>` for
both `com.google.android.gms` and `com.android.vending`; 29 merge `SignInHubActivity` (legacy Google
Sign-In); 12 merge androidx.credentials' Play-services `HiddenActivity`; 28 Play Core's
`PlayCoreDialogWrapperActivity`; 27 request the install-referrer bind permission; 25 `BILLING`; 25
`AD_ID`; 13 carry an AdMob app id; 12 a Maps API key (7 `com.google.android.geo.API_KEY`, 5 the
legacy key); 10 request `READ_GSERVICES`; 4 use the SMS retriever; 2 `CHECK_LICENSE`; none ships
Google's PairIP license check. So Play Store's package matters as much as GmsCore.

**Correction (2026-10-26).** The HK top 1000 differs: 224 of its 982 apps ship PairIP's license
check (`com.pairip.licensecheck`), and every one of the 13 in the top 100 stopped there. The check
passes locally only when the installer is `com.android.vending`; otherwise it binds Companion's
`ILicensingService` (`checkLicenseV2`) and opens an error activity when the bind fails. Hosting
Companion in process (build 123: `GuestPackages`, `GuestHost`) is the first step of this design to
ship: with no Google account Companion suppresses the negative V2 result, PairIP gets no answer and
the app runs, as on a microG phone without sign-in; with an account Companion fetches Google's
signed license.

### 1. The availability gate

Before a GoogleApi client connects, and in most SDKs' start-up checks, the library runs
`GooglePlayServicesUtilLight.isGooglePlayServicesAvailable` (basement 18.12.0). In order:

1. The app's meta-data `com.google.android.gms.version` must equal the library's 12451000
   (`GOOGLE_PLAY_SERVICES_VERSION_CODE`, set in `<clinit>`), or it throws.
2. `getPackageInfo("com.android.vending", 0x8002040)`: missing -> 9 `SERVICE_INVALID` (only
   wearables without Play Store, embedded and IoT devices are exempt).
3. `getPackageInfo("com.google.android.gms", 0x8000040)`: missing -> 1 `SERVICE_MISSING`.
4. Both must pass `GoogleSignatureVerifier` and `vending.signatures[0]` must equal
   `gms.signatures[0]`, else 9. On SDK >= 28 it reads `signingInfo.getSigningCertificateHistory()`
   and rejects multiple signers. Six Google certificates are embedded; a non-system package must
   match the narrow list: SHA-1 `38918a453d07199354f8b19af05ec6562ced5788` (2008 "Android, Google
   Inc."), `bd32424203e0fb25f36b57e5aa356f9bdd1da998` (2020) or
   `2169eddb5fbb1fdf241c262681024692c4fc1ecb` (2024).
5. `versionCode` below the requested minimum -> 2; `applicationInfo.enabled == false` -> 3; else 0.

`isPackageGoogleSigned` / `isUidGoogleSigned` go through the `com.google.android.gms.googlecertificates`
Dynamite module (microG serves it: `com.google.android.gms.common.GoogleCertificatesImpl`); if that
module cannot load, the package counts as not Google-signed. Westlake today answers
`getPackageInfo("com.google.android.gms")` from OH's bundle manager, finds nothing and remembers it
(PackageManagerAdapter.java:753-777, :660-666), so every app gets `SERVICE_MISSING`.

microG's own version: `gmsVersion = "25.24.32"` and code `252432 * 1000 + 2 * minor (+1 for a
dirty or later build)` (build.gradle:78-79, 105), i.e. 252432034 for v0.3.17; Companion
`vendingVersion = "40.2.26"` -> 84022634 (build.gradle:80-81, 107). Both clear every client gate
found: location 11717000, maps 13400000, cloud-messaging 12000000 / 233700000 / 241100000,
credentials 230815045-243100000, Play Integrity's Play Store minimum 82380000.

### 2. Bound services and the broker

- Bind intent (basement `common/internal/zzam`, "ServiceBindIntentUtils"): an explicit component
  if the client set one; else `call("serviceIntentCall", {"serviceActionBundleKey": action})` on
  `content://com.google.android.gms.chimera`, which returns the explicit Intent to use; else
  `new Intent(action).setPackage("com.google.android.gms")`. Bind flags 0x1081; on Q+
  `bindService(Intent, int, Executor, ServiceConnection)`; a bind failure is
  `ConnectionResult` 16; it refuses to bind a package in the STOPPED state.
- microG answers the chimera call by resolving the action inside its own package and falling back
  to `DummyService` (play-services-core/src/main/kotlin/org/microg/gms/chimera/ServiceProvider.kt:29-51).
- `BaseService.onBind` ignores the action and returns one `AbstractGmsServiceBroker`
  (play-services-base/core/src/main/java/org/microg/gms/BaseService.java:42-65). The client calls
  `getService(IGmsCallbacks, GetServiceRequest)` (transaction 45,
  play-services-basement/src/main/aidl/com/google/android/gms/common/internal/IGmsServiceBroker.aidl:37);
  the broker checks the service id against the component's `GmsService` set
  (play-services-base/core/src/main/java/org/microg/gms/AbstractGmsServiceBroker.java:252-261)
  and the service answers `onPostInitComplete(status, binder, bundle)`.
- The caller check, `PackageUtils.getAndCheckCallingPackage`
  (play-services-base/core/src/main/java/org/microg/gms/common/PackageUtils.java:228-263), takes
  `Binder.getCallingUid/Pid`, then the package of the calling pid from
  `ActivityManager.getRunningAppProcesses()` (single-package entries only, :267-280), else
  `getPackagesForUid`, and throws on a mismatch with the package the request claims.

| Feature | action the client binds (library) | microG component |
|---|---|---|
| sign-in (GoogleApiClient) | `com.google.android.gms.signin.service.START` (base) | `SignInService`, play-services-core/src/main/AndroidManifest.xml:622-627 |
| Google Sign-In | `com.google.android.gms.auth.api.signin.service.START` (auth) | `AuthSignInService`, :629-634 |
| Identity / One Tap, authorization | `...auth.api.identity.service.signin.START`, `...authorization.START` (auth) | `IdentitySignInService`, `AuthorizationService`, :640-650 |
| androidx.credentials >= 1.6 | `com.google.android.gms.identitycredentials.service.START` | `IdentityCredentialApiService` (play-services-identity-credentials/core) |
| OAuth tokens (GoogleAuthUtil) | explicit `com.google.android.gms/.auth.GetToken` (auth-base) | `GetToken` -> `AuthManagerServiceImpl`, :556-558 |
| SMS retriever | `...auth.api.phone.service.SmsRetrieverApiService.START` | play-services-auth-api-phone/core/src/main/AndroidManifest.xml:26-34 |
| fused location | `com.google.android.location.internal.GoogleLocationManagerService.START` | play-services-location/core/src/main/AndroidManifest.xml:61-67 |
| SafetyNet | `com.google.android.gms.safetynet.service.START` | play-services-safetynet/core/src/main/AndroidManifest.xml:10-14 |
| FCM registration | `com.google.android.c2dm.intent.REGISTER` (cloud-messaging, Messenger) | `PushRegisterService`, :334-343 |
| phenotype, common, telemetry, module install | `...phenotype.service.START`, `...common.service.START`, telemetry, `...moduleinstall.ModuleInstallService.START` | :1019-1023, :1312-1316, :1306-1310, play-services-base/core/package/src/main/AndroidManifest.xml:14-21 |

About 130 further actions land on `DummyService`, which answers `API_DISABLED`
(:1399-1542; play-services-core/src/main/java/org/microg/gms/DummyService.java:28-36). Several
actions are declared by both a real service and DummyService (for example
`auth.api.signin.service.START` at :631 and :1417), so Westlake's resolution must rank the real one
first.

### 3. Content providers

| Authority | who asks | microG |
|---|---|---|
| `com.google.android.gms.chimera` | DynamiteModule, the bind-intent lookup | `ServiceProvider`: `serviceIntentCall`; `query` returns an empty cursor (ServiceProvider.kt:29-63) |
| `com.google.android.gms.phenotype` | auth-base | always empty (`phenotype/ConfigurationProvider.java:31-43`) |
| `com.google.android.gms.clienterrorhandling` | GoogleApiAvailabilityLight (optional `getErrorInfo`) | not provided |
| `com.google.android.gsf.gservices` | no current library; apps directly (10 of 30 request READ_GSERVICES) | `GServicesProvider` over SQLite `gservices.db`, filled from check-in (play-services-core/src/main/AndroidManifest.xml:276-279) |
| `com.google.android.gms.auth.accounts` | microG and Google apps | `AccountContentProvider` (`get_accounts`) |
| `com.google.android.gms.microg.settings` | microG itself, from every process | `SettingsProvider` over SharedPreferences (play-services-base/core/src/main/kotlin/org/microg/gms/settings/SettingsProvider.kt:37-100) |

### 4. Code loaded into the app's own process (Dynamite)

This is the one part of Play services that Android itself already runs inside the app.
DynamiteModule (basement) checks `resolveContentProvider("com.google.android.gms.chimera")`, that
the provider belongs to `com.google.android.gms` and that the availability check passes; for a
GmsCore that is not a system-image app ("Non-system-image GmsCore APK, forcing V1") it uses the V1
loader: `createPackageContext("com.google.android.gms", CONTEXT_INCLUDE_CODE |
CONTEXT_IGNORE_SECURITY)` and the class `com.google.android.gms.chimera.container.DynamiteLoaderImpl`
as `IDynamiteLoader`. microG implements that loader (play-services-core/src/main/java/com/google/android/gms/chimera/container/DynamiteLoaderImpl.java:28-103,
interface version 2) and builds, per (module, app), a `PathClassLoader` over the GmsCore APK whose
parent is a filter that sends listed API classes to the app's loader
(play-services-core/src/main/java/com/google/android/gms/chimera/DynamiteContextFactory.java:37-101).
`ObjectWrapper.unwrap` only works inside one process. Modules microG serves: `googlecertificates`,
`providerinstaller.dynamite`, `maps_dynamite` (entry `com.google.android.gms.maps.internal.CreatorImpl`;
the maps client tries `maps_core_dynamite`, then `maps_dynamite`, then the remote context),
`measurement.dynamite`, `ads.dynamite`, `cronet_dynamite`, vision/ML Kit barcode and face,
`firebase_database` and cast (the last two logged as non-functional).

### 5. Messaging

- Token requests (cloud-messaging `Rpc`, shared by firebase-messaging and firebase-iid): for GMS
  >= 12000000 a Messenger over a bound `com.google.android.c2dm.intent.REGISTER` service;
  below it, `startService(REGISTER)` or a `com.google.iid.TOKEN_REQUEST` broadcast to
  `com.google.android.gms`, with the `"app"` PendingIntent and `"google.messenger"` extras. The
  library first requires `checkPermission("com.google.android.c2dm.permission.SEND",
  "com.google.android.gms")` to be granted. firebase-messaging 26.0.1 switches to a new
  `com.google.android.gms.cloudmessaging.service.START` service when GMS >= 261200000 and the app
  opts in; microG's 252432xxx keeps it on the older path, which microG implements.
- Extras firebase-messaging sends: `scope, sender, subtype, appid, Goog-Firebase-Installations-Auth,
  gmp_app_id, gmsv, osv, app_ver, cliv, Firebase-Client, delete, gcm.topic`.
- Delivery declaration merged into every app: receiver `com.google.firebase.iid.FirebaseInstanceIdReceiver`
  (exported, permission `com.google.android.c2dm.permission.SEND`, action
  `com.google.android.c2dm.intent.RECEIVE`) and service `com.google.firebase.messaging.FirebaseMessagingService`
  (action `com.google.firebase.MESSAGING_EVENT`, priority -500). On Q+ the library calls
  `NotificationManager.setNotificationDelegate("com.google.android.gms")`.

### 6. Activities in the GMS package

- Legacy Google Sign-In: the app's `SignInHubActivity` starts `Intent("com.google.android.gms.auth.GOOGLE_SIGN_IN").setPackage("com.google.android.gms")`
  for result (request code 40962). microG's `AuthSignInActivity` (play-services-core/src/main/AndroidManifest.xml:603-613)
  rejects the request with DEVELOPER_ERROR unless `getCallingActivity()` is the app
  (play-services-core/src/main/kotlin/org/microg/gms/auth/signin/AuthSignInActivity.kt:66-68).
- Account picker: `com.google.android.gms.common.account.CHOOSE_ACCOUNT` (basement) ->
  `AccountPickerActivity` (:503-515).
- Identity API and androidx.credentials: the service returns a PendingIntent to a microG activity
  (`AssistedSignInActivity`, :676-689; `IdentityCredentialChooserActivity`), and the app's
  `HiddenActivity` launches it with `startIntentSenderForResult`.
- Resolutions (`GoogleApiActivity`, in the app) launch PendingIntents microG returns.

### 7. Play Store's package (`com.android.vending`, microG Companion)

| Client | binds | Companion component (vending-app/src/main/AndroidManifest.xml) |
|---|---|---|
| Play Integrity (22/30 reach it) | `com.google.android.play.core.integrityservice.BIND_INTEGRITY_SERVICE`, `...expressintegrityservice.BIND_EXPRESS_INTEGRITY_SERVICE` | `IntegrityService` :281-284, `ExpressIntegrityService` :289-292 |
| install referrer (27/30) | `com.google.android.finsky.BIND_GET_INSTALL_REFERRER_SERVICE` | `GetInstallReferrerService` :105-108 |
| Play Billing (25/30) | `com.android.vending.billing.InAppBillingService.BIND` | `InAppBillingService` :214-218 |
| in-app update / review (28/30) | `com.google.android.play.core.install.BIND_UPDATE_SERVICE`, `com.google.android.finsky.BIND_IN_APP_REVIEW_SERVICE` | :309-313, :318-321 |
| licensing (2/30) | `com.android.vending.licensing.ILicensingService` | `LicensingService` :96-100 |
| asset delivery, split install | `...assetmoduleservice.BIND_ASSET_MODULE_SERVICE`, `...splitinstall.BIND_SPLIT_INSTALL_SERVICE` | :235-239, :188-191 |

The Integrity library checks Play Store itself (`PhoneskyVerificationUtils`): enabled, every
signature's SHA-256 equal to Google's certificate above (`8P1sW0EPJcslw7UzRsiXL64w-O50Ed-RBICtay1g24M`),
versionCode >= 82380000. Its error codes run from -1 `API_NOT_AVAILABLE`, -2 `PLAY_STORE_NOT_FOUND`
and -6 `PLAY_SERVICES_NOT_FOUND` to -9 `CANNOT_BIND_TO_SERVICE`, -12 `GOOGLE_SERVER_UNAVAILABLE`
and -100 `INTERNAL_ERROR`.

## What microG needs from the OS

On Android microG is an ordinary APK that leans on the system in five ways. For each, what Android
gives it and what Westlake gives instead.

| Need | Android with microG | Westlake design |
|---|---|---|
| Look like Google's packages | Signature spoofing by the ROM: the old patch spoofs `signatures` for apps holding `android.permission.FAKE_PACKAGE_SIGNATURE` with `fake-signature` meta-data (fake-signature/src/main/AndroidManifest.xml:9-19; microg/patches `31/android_frameworks_base.patch`:115-136); LineageOS since 2024-02 spoofs `signatures` and `signingInfo` for exactly `com.google.android.gms` and `com.android.vending` signed with microG's key, on debuggable builds per the commit message (LineageOS commit 198fa72864) | `GuestPackages` answers both packages with Google's certificate directly; there is no permission to grant |
| A Play Store package | microG Companion (`com.android.vending`, "cannot be used standalone") | the same APK as a second guest |
| Privileged permissions | system/priv-app install for the push power allowlist (`CHANGE_DEVICE_IDLE_TEMP_WHITELIST`, McsService.java:187-208, 645-660), location app-op bookkeeping (`UPDATE_APP_OPS_STATS` etc., tolerated when missing), the system location provider (`INSTALL_LOCATION_PROVIDER`), `DUMP` for DroidGuard (worked around), `INSTALL_PACKAGES` for Companion's installer; microG defines `com.google.android.c2dm.permission.SEND` as privileged and holds it | none of these exist under Westlake; the guests "hold" what they request (permission adapter) and the corresponding effects are Westlake's own (lease instead of allowlist, OH location instead of a system provider) |
| Processes that outlive apps | `:persistent` (all GCM), `:ui` (activities), `com.google.android.gms.unstable` (DroidGuard), main (providers, brokers); McsService warns when not in `:persistent`; a battery-optimisation exemption or a foreground service keeps it up | everything runs in the app's process in phase 1; gmsd in phase 3 |
| System services | AccountManagerService (accounts, tokens, authenticator binding), ActivityManager (bind, broadcasts, PendingIntent creator, start a stopped app for a push), PackageManager (signatures, permissions, queries), AlarmManager (exact idle alarms), LocationManager providers, WebView, KeyStore, MediaDrm | in-process answers exist for most; accounts (empty) and location (disabled) are the gaps this design fills (`LocalAccounts`, the location bridge), plus guest resolution in the package and activity managers; MediaDrm, which DroidGuard calls, is unverified under Westlake |

Other facts that shape the design:

- **No sharedUserId, no `android:persistent`**, minSdk 19, targetSdk 29, compileSdk 35 (root
  build.gradle:42-44). No hidden PendingIntent API is used; hidden APIs reached by reflection are the
  power allowlist (`PowerExemptionManager`, `IDeviceIdleController`), `Context.getSharedPreferences(File,int)`
  and `ApplicationInfo.isProduct/isSystemExt` (SettingsProvider.kt:42-73),
  `ActivityThread.currentProcessName` (PackageUtils.java:301-312), replacing the AndroidKeyStore
  provider for DroidGuard (`HardwareAttestationBlockingProvider.kt:36-52`), `WorkSource.add`,
  `SystemProperties.get`. The classes exist in Westlake's AOSP 15 framework; the services behind them
  are answered in process (`deviceidle` in LocalServiceBinders) or fail softly, and microG already
  tolerates the missing allowlist permission.
- **Settings defaults are switchable without code**: user values, then the `checkin` and
  `unified_nlp` files, then `/system|product|system_ext/etc/<pkg>/settings.xml` and
  `/system/etc/microg.xml`, then manifest meta-data `org.microg.gms.settings.*`
  (SettingsProvider.kt:62-100). Upstream defaults are privacy-first: check-in, cloud messaging,
  DroidGuard, SafetyNet/Play Integrity, Companion billing, licensing and asset delivery all off.
- **GsfProxy** (`com.google.android.gsf`) only forwards old C2DM REGISTER intents to GmsCore
  (microg/GsfProxy `PushRegisterProxy.java:18-23`, last release 2016); GmsCore itself serves the
  `com.google.android.gsf.gservices` provider. Not needed.
- **Companion reads GmsCore's settings** through meta-data `org.microg.gms.settings:source-package`
  (vending-app/src/main/AndroidManifest.xml:81-87), so the two guests must stay a pair.

### The Huawei build: the closest precedent

microG's `huawei` flavor (release assets `*-hw.apk`, also distributed through Huawei AppGallery
for "EMUI or HarmonyOS" devices) keeps the same package names and the same microG signing key; only
`versionNameSuffix "-hw"` changes (play-services-core/build.gradle:177-180; vending-app/build.gradle:51-53).
The OS does the spoofing by asking microG: microG exports `ISignatureService.querySignature(packageName,
suggested)` on action `com.huawei.signature.diff` in process `:signature`, refuses callers above
uid 10000, and answers the fake (Google) or real certificate from a per-package table seeded with
GmsCore, Companion and a list of apps (fake-signature/src/huawei/AndroidManifest.xml:31-40;
SignatureService.java:66-104; fake-signature/src/main/res/values/arrays.xml:7-38). The flavor turns
the privacy defaults on through manifest meta-data (check-in, GCM, DroidGuard, SafetyNet, Companion
billing, licensing and asset delivery; `auth_include_android_id=false`; play-services-core/src/huawei/AndroidManifest.xml:10-63),
ships no system location provider, and its maps use Huawei Map Kit. Which EMUI/HarmonyOS versions
implement the signature service is UNVERIFIED. The lesson for Westlake: a vendor platform answering
"which certificate does package X have" per package, plus defaults set by meta-data, is enough for
microG to work as a user app; Westlake does both in its own package manager and needs neither the
signature service nor Map Kit.

## Push

### How microG does FCM

Paths below are in `play-services-core/src/main/` unless they say otherwise; `MCS` is
`java/org/microg/gms/gcm/McsService.java`, `PRS` `kotlin/org/microg/gms/gcm/PushRegisterService.kt`,
`RR` `java/org/microg/gms/gcm/RegisterRequest.java`.

1. **Registration.** `PushRegisterService` (actions `com.google.android.c2dm.intent.REGISTER` /
   `UNREGISTER`, process `:persistent`, AndroidManifest.xml:334-343) and `PushRegisterReceiver`
   (`com.google.iid.TOKEN_REQUEST`, :345-351, forwards to the service, PRS:366-376). The caller is
   the creator of the `"app"` PendingIntent (PRS:110-114; `PackageUtils.packageFromPendingIntent`,
   play-services-base/core/.../PackageUtils.java:291-299) or, on the Messenger path, the uid
   recorded in `sendMessageAtTime` checked against the message's `"pkg"` (PRS:225-229, 310-317).
   Before any request: check-in must be enabled and under 24 h old (else run it and wait up to
   10 s), cloud messaging enabled, and the per-app consent rule passed (PRS:32-78; confirmation of
   new apps defaults to off).
2. **Request.** `POST https://android.clients.google.com/c2dm/register3`,
   `Authorization: AidLogin <androidId>:<securityToken>`, form fields `app`, `cert` (SHA-1 of the
   app's signing certificate), `sender`, `device`, `target_ver`, every other extra as `X-<key>`
   (RR:37-147). The token comes back in `registration_id`, by Messenger (an Intent
   `com.google.android.c2dm.intent.REGISTRATION` in `Message.obj`) or by ordered broadcast to the
   app (PRS:193-210).
3. **Check-in** (`java/org/microg/gms/checkin/`): `POST https://android.clients.google.com/checkin`,
   a gzipped protobuf carrying the selected device profile's Build fields, random MEID and Wi-Fi
   MAC, and each Google account's LSID (CheckinClient.java:50-166; CheckinManager.java:46-58). It
   returns the `androidId` and `securityToken` that every later GCM and auth request carries; they
   live in the `checkin` preferences behind the settings provider.
4. **Connection.** `McsService` (`:persistent`) opens TLS to `mtalk.google.com` port 5228, then 443
   (MCS:100-104, 444-477), writes version byte 41 and `[tag][varint length][protobuf]` frames, logs
   in with the androidId and securityToken and the ids of messages already received (MCS:557-573),
   pings every 5-15 minutes from an exact idle-allowed alarm (MCS:283-303;
   kotlin/org/microg/gms/gcm/GcmPrefs.kt:105-178), reconnects with a 5 s .. 10 min backoff
   (MCS:305-314), and runs as a foreground service unless microG is exempt from battery
   optimisation. Its socket, streams and handler are static fields: one connection per process
   (MCS:110-133).
5. **Delivery** (`handleAppMessage`, MCS:575-643): an Intent `com.google.android.c2dm.intent.RECEIVE`
   with `from`, `google.message_id`, `collapse_key`, `rawData` and the app data,
   `FLAG_INCLUDE_STOPPED_PACKAGES`, `setPackage(app)`, `sendOrderedBroadcast`. A privileged microG
   first adds the app to a 10 s power allowlist (MCS:645-666). The app's own
   `FirebaseInstanceIdReceiver` and `FirebaseMessagingService` do the rest, including displaying
   FCM "notification" messages: microG never posts a notification for another app's message.
   Received ids go back to Google at the next login and are then cleared (MCS:517, 571); there is
   no queue of undelivered messages.

What Android gives microG here and Westlake does not: a process that stays up, exact wake-up
alarms, a power allowlist, and above all the system starting an app that is not running so that its
RECEIVE receiver can run.

### Stage 1: push while some Westlake app runs (phase 2 of the plan)

With the guest host of "Westlake integration design", registration works in process: the Messenger
bind, the `startService` and the TOKEN_REQUEST broadcast all reach microG's components; the
`"app"` PendingIntent's creator is the app (`LocalIntentSenders.info`); microG's ordered broadcast
to `setPackage(app)` counts as the app's own broadcast in `LocalBroadcasts.broadcast`
(framework/activity/java/LocalBroadcasts.java:87-121) and reaches `FirebaseInstanceIdReceiver`
through `ManifestReceivers` (framework/activity/java/ManifestReceivers.java:50-115), which hands
the message to the app's own `FirebaseMessagingService` by starting or binding it in process
(`InProcessServiceBinder`; a bind by action and package needs change 3's resolution, since today
only explicit components bind). Three things need design:

- **One session per device identity.** Every app process would otherwise run its own McsService
  and log in with the same androidId. What Google does with concurrent sessions for one androidId
  is not documented (UNVERIFIED; microG issue #242 shows one device holding four sockets to
  mtalk:5228 at once). Every session would receive every app's messages and acknowledge them on
  receipt, so messages for other apps would be lost or duplicated, and if the server closes the
  older session, microG treats the close as an error and reconnects in 5 s: two processes would
  knock each other off in a loop. So only the holder of the lease (change 6) runs McsService.
- **Messages for another app.** The Westlake build of microG writes a message for a package that is
  not the lease holder's app into an outbox table in the shared directory before its id is
  acknowledged; the target process, if it runs, is woken by a `FileObserver` (inotify) on its
  signal file and delivers it to itself; otherwise the outbox is drained when that app next starts
  (GuestHost, at application bind). FCM's default time-to-live is four weeks, and Google keeps
  unacknowledged messages until the device reconnects.
- **Seeing the notification.** Westlake does not show notifications in OH's notification centre:
  `LocalNotifications` keeps channels and posts and plays the sound, nothing more
  (framework/core/java/LocalNotifications.java:17-28). Push is invisible until a notification
  bridge exists: native code publishing through OH's `NotificationHelper::PublishNotification`
  (inner API, base/notification/distributed_notification_service/interfaces/inner_api/notification_helper.h:216
  in the OH tree) under the host's token, with a WantAgent that brings the host back to start the
  Android app. For a message whose app is not running, the lease holder posts a plain notification
  from the FCM `gcm.n.title` / `gcm.n.body` keys; tapping it starts the app, which then drains the
  outbox.

An alternative avoids coordination: a separate androidId per app, as Chromium's own GCM client does
its own check-in. Each app's session then carries only its own messages, and Google holds the rest
until that app next connects. It multiplies device registrations from one phone (Google's tolerance
is UNVERIFIED), the identity changes again when push moves to one process in stage 2, and tokens
rotate (apps handle that through `onNewToken`). Keep it as the fallback if the lease design hits
trouble with real accounts.

### Stage 2: push while apps are closed (phase 3 of the plan)

Something must hold the MCS session while no app runs, and something must start the app when a
message arrives. On Android those are GmsCore's persistent process and the activity manager. The
Westlake equivalent:

1. **A GMS daemon process ("gmsd").** appspawn-x spawns `com.google.android.gms` as a headless
   Westlake process at boot (a direct launch that binds the guest as the process's own package and
   launches no activity). It is the only process that runs McsService, check-in, the account store
   and token minting. Its directory is visible only to it (this needs the per-app mounts that
   `ChildMain::applySandbox` does not implement yet, framework/appspawn-x/src/child_main.cpp:1692-1731).
2. **Authenticated per-app channels.** All Westlake apps share one OH uid, so a uid cannot tell
   gmsd who is asking (Android's `Binder.getCallingUid` model collapses). appspawn-x, which knows
   which package it is spawning, creates a socketpair per app process and hands one end to gmsd
   over a control socket with SCM_RIGHTS and the package name. The protocol is narrow, not a binder
   emulation: register / unregister / ack (push), deliver (gmsd -> app), getToken / getAccounts /
   startLogin (accounts), and drain (outbox). The in-app copy of microG keeps every other API in
   process and forwards these few calls (patch points: `PushRegisterManager`, `AuthManager`,
   `CheckinManager`).
3. **Starting a closed app.** For a message whose app has no process, gmsd asks appspawn-x for a
   headless spawn of that app (new spawn mode: `AppSchedulerBridge.directLaunchNoBms` with no
   activity, framework/activity/java/AppSchedulerBridge.java:180-260); the app binds, drains its
   outbox, its `FirebaseMessagingService` posts the notification through the notification bridge,
   and the process exits after an idle timeout. Tapping the notification launches the app's
   activity through the host.
4. **Staying alive on OH.** On the board image Westlake controls init, so gmsd can run as a system
   service outside OH's app freezing. On a commercial HarmonyOS device a third-party app cannot
   keep a socket open in the background; the options there are an OH continuous task (needs a
   visible notification and an allowed task type) or a server relay through Huawei Push Kit, which
   means holding users' GCM credentials server-side. Neither is designed here.

A full Android binder between Westlake processes (a userspace binder over sockets, or a binderfs
device if the OH kernel and policy allow one) would let unmodified microG serve every app from
gmsd. It is the general answer for any cross-app Android IPC, at several times the cost of the
narrow protocol; it is listed as an option, not planned.

## Accounts

Paths below are in `play-services-core/src/main/java/org/microg/gms/auth/` unless they say otherwise.

### Login: a WebView, then a master token

- `LoginActivity` (exported, `:ui`, action `com.google.android.gms.auth.login.LOGIN`,
  play-services-core/src/main/AndroidManifest.xml:517-529) is what `AccountAuthenticator.addAccount`
  returns as `KEY_INTENT` for account type `com.google` (loginservice/AccountAuthenticator.java:54-63).
- It runs check-in first when there is no androidId (login/LoginActivity.java:292-308, 469-477).
  Check-in defaults to off (SettingsProvider.kt:154): the Westlake defaults must turn it on.
- It loads `https://accounts.google.com/EmbeddedSetup` (re-auth: `.../embedded/reauth/v2/android`,
  LoginActivity.java:96-97) in a WebView whose user agent is the device profile's plus
  `" MinuteMaid"`, with a JavaScript bridge named `mm` imitating GmsCore's own login screen
  (:101, 128, 504-756). `getDroidGuardResult` runs DroidGuard flow `minute_maid` (:595-610). The
  login therefore needs Westlake's in-process WebView (working since the WebView renderer fix) and,
  increasingly, DroidGuard.
- From the `oauth_token` cookie it posts to `https://android.googleapis.com/auth` with
  `service=ac2dm`, `ACCESS_TOKEN=1`, `add_account=1`, `client_sig=38918a45...` (GmsCore's own
  certificate) and `droidguard_results="null"` (:348-356; AuthRequest.java, play-services-base/core,
  :25-141), and receives the master token (`Token`), `Email`, `SID`, `LSID` and the user's names.
- The account is created with `addAccountExplicitly(new Account(email, "com.google"), masterToken)`
  plus `SID`/`LSID` auth tokens and user data (`flags`, `services`, `GoogleUserId`, names)
  (LoginActivity.java:380-391); a broadcast `com.google.android.gms.auth.GOOGLE_ACCOUNT_CHANGE` goes
  to `com.android.vending` (:404-460).

**Where it breaks today (upstream):** microG issue #3195 (open): new-device logins fail after the
web flow with `Error=MissingDroidguard`; PR #3257 (merged 2026-02-06) sends a literal `"null"`, and
users still reported failures in March and April 2026; the maintainer's advice is to change the
device profile. Workspace (#2919) and Family Link (#1253) accounts fail. Whether Google's 2021
embedded-WebView OAuth block applies to this flow is UNVERIFIED (no microG issue reports it).
Login reliability is the largest single unknown in the plan and is gated on DroidGuard working in
Westlake (see Integrity).

### Storage: Android's AccountManager

The master token is the account's password; per-app tokens are auth tokens keyed
`<pkg>:<sha1>:<service>[...]`, with expiry in user data `EXP.<key>` and consent in `perm.<key>`
(AuthManager.java:101-144, 215-225). microG's own extra state: `pluscontacts.db`, preferences
`google_account_cache` (default account per app), `common.selected_account_prefs`, and the
`checkin` preferences. The authenticator is `GoogleLoginService` (action
`android.accounts.AccountAuthenticator`, `@xml/authenticator`, `accountType="com.google"`,
`customTokens="true"`, AndroidManifest.xml:472-485); with custom tokens AOSP's AccountManagerService
skips its own grant check and cache and fills `KEY_CALLER_UID`, `KEY_CALLER_PID` and
`KEY_ANDROID_PACKAGE_NAME` before calling the authenticator. microG calls `addAccountExplicitly`,
`removeAccount`, `get/setUserData`, `peek/set/invalidateAuthToken`, `getPassword`/`clearPassword`,
`getAccountsByType`, `getAccountsByTypeAndFeatures`, `getAccountsByTypeForPackage` (system and
authenticator only) and the API 26 visibility calls; no hidden or reflective use was found.

Westlake answers the `account` service with "signed in nowhere": empty lists and
`addAccountExplicitly == false` (framework/core/java/LocalServiceBinders.java:908-923). Nothing of
microG's account model works until `LocalAccounts` (change 5 of the integration design) exists.

### Tokens for apps

- `GoogleAuthUtil` (auth-base) binds the explicit component `com.google.android.gms/.auth.GetToken`
  (`IAuthManagerService`); microG's `AuthManagerServiceImpl.getTokenWithAccount`
  (AuthManagerServiceImpl.java:118-197) identifies the caller by Binder, accepts scopes
  `oauth2:...`, `audience:server:client_id:...` (ID token) and `oauth2:server:client_id:...:api_scope:...`
  (server auth code), and returns the token or `NeedPermission` with a consent intent.
- Who gets a token: an app is permitted if it holds `com.google.android.googleapps.permission.GOOGLE_AUTH.<service>`
  or was allowed before; with "trust Google" (default on, SettingsProvider.kt:246) microG asks
  Google and shows its consent screen (`AskPermissionActivity`) only when Google returns no token
  (AuthManager.java:129-140, 317-368).
- What Google receives: `app=<pkg>` and `client_sig=<SHA-1 of the app's signing certificate>`, the
  master token, the androidId by default (AuthRequest.java:28-98). Google matches package and
  certificate against the OAuth client the developer registered, which is why Westlake must report
  each app's real certificate and only spoof the two guests.

### Sign-in for apps

| API | path | microG |
|---|---|---|
| legacy `GoogleSignInClient` (29/30 apps) | app's `SignInHubActivity` -> `GOOGLE_SIGN_IN` activity in gms, for result | `AuthSignInActivity`: picker or confirmation, then token / ID token / server auth code (signin/extensions.kt:117-197); requires `getCallingActivity()` = the app (AuthSignInActivity.kt:66-68) |
| silent sign-in, sign-out, revoke | `auth.api.signin.service.START` | `AuthSignInService` (signin/AuthSignInService.kt:76-176) |
| Identity API / One Tap | `auth.api.identity.service.signin.START` -> PendingIntent | `IdentitySignInService` (beginSignIn needs a `serverClientId`; phone-number hint is a stub) -> `AssistedSignInActivity` |
| authorization | `auth.api.identity.service.authorization.START` | `AuthorizationService` |
| androidx.credentials (12/30 apps) | 1.6.0 switches to `identitycredentials.service.START` when GMS >= 252400000 (microG reports 252432xxx); 1.5 uses BeginSignIn | `IdentityCredentialApiService` -> `IdentityCredentialChooserActivity`; Google ID works, passwords "are not stored" |
| SMS retriever (4/30) | `SmsRetrieverApiService.START` | implemented; needs `RECEIVE_SMS` and incoming SMS broadcasts, which direct launch does not deliver today (UNVERIFIED whether OH lets the host receive SMS at all) |
| Block Store | `auth.blockstore.service.START` | local only, no cloud backup |

Westlake specifics: `getSystemService(CREDENTIAL_SERVICE)` is null under Westlake, because the
fetcher only builds a CredentialManager over a `credential` binder
(imports/frameworks-base/core/java/android/app/SystemServiceRegistry.java:1225-1236) and none
exists. androidx.credentials on API 34+ then falls back to the Play-services provider (expected
from androidx's provider factory; to be confirmed by the probe), which is the path above. The
Identity and androidx flows launch microG's PendingIntent with `startIntentSenderForResult`, which
Westlake currently mishandles (change 4b).

Firebase Auth 20+ does not need Play services except for Google sign-in (an ID token through the
paths above), SMS retrieval and Play Integrity-backed phone verification (Firebase falls back to
reCAPTCHA); microG's `firebase-auth/core` proxy only matters for older SDKs.

## Integrity

### SafetyNet, reCAPTCHA, DroidGuard

- **SafetyNet attestation is gone everywhere.** Google turned the backend down in January 2025;
  microG's `attest` now returns `API_NOT_CONNECTED` "The SafetyNet Attestation API is deprecated and
  no longer functional" (play-services-safetynet/core/src/main/kotlin/org/microg/gms/safetynet/SafetyNetClientService.kt:65-73),
  and the attestation client was deleted at the pinned commit. Other SafetyNet calls answer
  dummies (:75-99, 204-216); `verifyWithRecaptcha` is implemented (:116-202) through a WebView and
  DroidGuard.
- **reCAPTCHA Enterprise** (`com.google.android.gms.recaptcha.service.START`) is implemented with a
  WebView backend or a DroidGuard backend (play-services-recaptcha/core/.../RecaptchaService.kt:34-45).
- **DroidGuard** is the piece everything else leans on: account login (flow `minute_maid`), Play
  Integrity (`pia_attest_e1`, `pia_express`), reCAPTCHA. microG's `DroidGuardService` runs in its own
  process `com.google.android.gms.unstable` (play-services-droidguard/core/src/main/AndroidManifest.xml:9-26)
  and is off by default (SettingsProvider.kt:315). It downloads Google's VM from
  `https://www.googleapis.com/androidantiabuse/v1/x/create` with ~25 Build values of the device
  profile, verifies it, caches it under `cache_dg/<checksum>/the.apk`, and runs
  `com.google.ccc.abuse.droidguard.DroidGuard` through a `DexClassLoader` and reflection
  (play-services-droidguard/src/main/kotlin/org/microg/gms/droidguard/HandleProxyFactory.kt:71-101).
  Whether the VM brings native code, and for which ABI, is not visible in microG's source
  (UNVERIFIED). microG shims the environment the VM probes: a stable serial, a KeyStore wrapper that
  hides certificate chains from any call stack containing "droidguard", and a proxy that swallows
  `dump()` calls to SurfaceFlinger and thermalservice, which the maintainer credits with restoring
  BASIC integrity (microG #3198).

For Westlake: DroidGuard is Google's obfuscated anti-abuse code inspecting the environment it runs
in, here an Android runtime on OpenHarmony with Westlake's own services, inside the app's process
(not GmsCore's), with Westlake's software keystore. Expect it to run (it is dex) and to score badly.
That matters beyond integrity: Google's login increasingly wants DroidGuard results (microG
#3195), so DroidGuard is a phase-1 spike, not a phase-4 item.

### Play Integrity (22 of 30 sampled apps reach it)

- Implemented by microG Companion, not GmsCore: `IntegrityService` and `ExpressIntegrityService`
  (vending-app/src/main/AndroidManifest.xml:280-294), gated by the device-attestation switch
  (default off, vending-app/src/main/java/com/android/vending/VendingPreferences.kt:85-89) and a
  per-app allow list.
- Classic request: an OAuth token for `oauth2:https://www.googleapis.com/auth/googleplay` if an
  account exists (not required), DroidGuard flow `pia_attest_e1`, a POST to
  `https://play-fe.googleapis.com/fdfe/integrity` (vending-app/src/main/kotlin/com/google/android/finsky/integrityservice/IntegrityService.kt:102-221).
  Every failure reaches the app as `INTERNAL_ERROR` (-100) (:215-235); the remediation dialog is not
  implemented. Standard (express) requests go through `fdfe/sync` and `fdfe/intermediateIntegrity`.
  The Android KeyStore attestation code exists but nothing calls it.
- Verdicts users actually get with microG: at best `MEETS_BASIC_INTEGRITY` (v0.3.6 release notes;
  issue #3300); BASIC disappeared from December 2025 until 0.3.16 (#3198, the maintainer tying it
  to a missing licensing verdict); `MEETS_DEVICE_INTEGRITY` only after replacing Companion with the
  real Play Store.
- Google's rules since May 2025 (Android 13+): DEVICE needs hardware-backed proof of a locked
  bootloader and a certified image; BASIC "requires only that the attestation root of trust is
  provided by Google", with temporary fallbacks when hardware signals are missing.

**Realistic Westlake verdicts:** no DEVICE or STRONG verdict, ever: there is no Google-rooted key
attestation (Westlake's AndroidKeyStore is software keys with self-signed certificates and "no
attestation", framework/core/java/SoftwareAndroidKeyStore.java:58-63), and nothing about the
platform is a certified image. BASIC is uncertain at best (it depends on DroidGuard's opinion of
this environment and on Google's fallbacks) and should be planned as absent. What remains in our
control is failing fast and well-formed: the bind succeeds, the request ends in a specific error or
a token, never a hang.

**Expected app behaviour.** Google tells developers to tier their response ("Allow, Allow with
limits, Allow with limits after CAPTCHA completion, and Deny") and, after three failed retries, to
treat the client as failing every check. Observed under microG: ChatGPT shows "latest version of
Google Play... (-9)" (#2108), a bank hangs at login (#3738), banking apps demand DEVICE integrity
(#3300), Google Pay refuses (#2747), a game reports a "not licensed install" (#3796). For the
corpus: social and messaging apps (Instagram, Threads, Messenger, X, Discord, Reddit, TikTok)
mostly use integrity as one risk signal at sign-up and login, so expect extra challenges (CAPTCHA,
SMS, e-mail) rather than refusal, but this has to be measured per app; banking, payment and some
games will refuse outright. That is a permanent gap of any non-certified platform, microG or not.

## Location and Maps

### Location (24 of 30 sampled apps reach GMS location)

- Apps bind `com.google.android.location.internal.GoogleLocationManagerService.START`
  (play-services-location/core/src/main/AndroidManifest.xml:61-67); each caller gets a
  `LocationManagerInstance` over shared request management (play-services-location/core/src/main/kotlin/org/microg/gms/location/manager/GoogleLocationManagerService.kt:60-68).
- microG's fused location consumes Android's `gps` provider, microG's own network location service
  if built in (else the system `network` provider), and `passive` to learn Wi-Fi and cell positions
  (manager/LocationManager.kt:176-257). Network location uses Wi-Fi scans and cell info, a local
  `geocache.db`, and an Ichnaea-compatible server; the only built-in one is BeaconDB, and online
  lookup is off until a source is chosen (SettingsProvider.kt:362-372). There is no Google endpoint.
- Callers need FINE or COARSE (`packageManager.checkPermission`) and the matching AppOps; if AppOps
  throws, the call is allowed (LocationManagerInstance.kt:405-416; extensions.kt:94-153).
- Not implemented: geofencing and activity recognition (they log "Not yet implemented" and never
  call back, LocationManagerInstance.kt:54-104). Geocoding (Nominatim) is only reachable through a
  system geocode provider, which needs privileged install, as does the whole system location
  provider module.

**Westlake today** reports location "present but switched off": providers exist, none enabled, no
fixes (framework/core/java/LocationManagerAdapter.java:1-104); Wi-Fi reports "not connected" and no
scans (LocalServiceBinders.java:434-447); the host HAP requests no location permission
(apps/imehost/module.json); `ACCESS_FINE_LOCATION` maps to OH `APPROXIMATELY_LOCATION`
(framework/package-manager/java/PermissionMapper.java:42-44). So microG would run and find nothing.

**Design:** do not make microG compute locations. Bridge `LocationManagerAdapter` to OH's location
service, which already fuses GNSS and network positioning where the device has them: the Locator
inner API (`StartLocatingV9`, base/location/interfaces/inner_api/include/locator_impl.h:346 in the
OH tree) or the location NDK where the API level has it. Report `gps` and `network` as enabled per
OH's switch, deliver `Location` objects, and let microG's fused provider and its client-API
semantics (requests, callbacks, PendingIntents, last location) sit on top. The host HAP must
request `ohos.permission.APPROXIMATELY_LOCATION` and `ohos.permission.LOCATION` (and later
`LOCATION_IN_BACKGROUND`), with the runtime request relayed through the host, and `ACCESS_FINE_LOCATION`
must map to `LOCATION`. microG's BeaconDB lookup stays off (it would need Wi-Fi scans Westlake does
not have, and sends scan data to a third party). Geofencing and activity recognition stay
unimplemented, as in microG. Test hardware: the D600 board may have no GNSS (UNVERIFIED); plan on a
phone-class OH device or OH mock locations.

### Maps (10 of 30 reach Maps; 12 declare a Maps API key)

- Google's maps client loads `com.google.android.gms.maps_core_dynamite`, then
  `com.google.android.gms.maps_dynamite`, then the remote context, and instantiates
  `com.google.android.gms.maps.internal.CreatorImpl`: maps always run in the app's process, on
  Android too. microG answers version 1 for `maps_dynamite`
  (play-services-core/src/main/java/com/google/android/gms/chimera/container/DynamiteLoaderImpl.java:91-94).
- Engine: the release APK is the "mapbox" flavor, which is MapLibre Native 10.2.0 (BSD-2-Clause;
  play-services-maps/core/mapbox/build.gradle:24-28). The vtm flavor is deprecated by microG ("Use
  Mapbox build!"); the hms flavor wraps Huawei Map Kit and needs HMS Core, which OpenHarmony does not
  have.
- Tiles: keys are injected at build time (`MAPBOX_KEY`, `STADIA_KEY`); with a Stadia key the styles
  come from `https://tiles.stadiamaps.com/data/openmaptiles.json` (play-services-maps/core/mapbox/src/main/kotlin/org/microg/gms/maps/mapbox/Styles.kt:42-77).
  microG's official APK carries microG's Stadia key; a Westlake build needs its own tile contract
  (Stadia, MapTiler or self-hosted OpenMapTiles) sized for production traffic.
- Platform needs: a `GLSurfaceView` with OpenGL ES 2.0, RGB565 or better, depth 16, stencil 8;
  `MultiArchLoader` copies `libmapbox-gl.so` out of the GmsCore APK into the app's cache and
  `System.load`s it, reading the app's hidden `primaryCpuAbi` (Westlake sets `arm64-v8a`,
  SourcePackageRegistry.java:69-74). Lite mode works (snapshotter); StreetView and the legacy
  Places APIs are stubs.

For Westlake: Maps needs no certification and no privilege, only GL and native-library loading,
which Westlake already does for apps (GLES shims; SurfaceView rendering; the PPSSPP/Mindustry
SurfaceView work). Risks are rendering fidelity of MapLibre on Westlake's GL path, and that
Google-style map content (POIs, traffic, StreetView) is not available from OpenMapTiles.

## Westlake integration design

### The shape: GMS as a guest package inside every app process

Westlake already runs a second Android package's code inside every app process: the sideloaded
WebView provider. `PackageManagerAdapter.getSideloadedWebViewPackageInfo`
(framework/package-manager/java/PackageManagerAdapter.java:50-137) describes `com.android.webview`
as an installed package whose `sourceDir` is a staged APK (`ASX_WEBVIEW_APK`), whose `dataDir` is a
staged directory and whose `uid` is `Process.myUid()`. The unchanged framework then loads it the
normal way (WebViewFactory -> createPackageContext -> LoadedApk -> PathClassLoader). microG fits the
same mould, with two packages instead of one:

- `com.google.android.gms` (GmsCore, built from microG `play-services-core`), and
- `com.android.vending` (microG Companion, built from `vending-app`), needed for Play Integrity,
  licensing, billing answers, install referrer and Play Core.

Both are staged read-only beside the runtime and reported by the package manager as installed,
enabled, non-system and signed with Google's certificate. Every ActivityThread mechanism that Android uses to
run code from another package in a process (`ActivityThread.getPackageInfo`,
`LoadedApk.makeApplicationInner`, `installProvider` for multiprocess providers) works as long as
the guest's `ApplicationInfo.uid` equals the process uid: AOSP 15
`ActivityThread.getPackageInfo(ApplicationInfo, CompatibilityInfo, int)` only throws "Requesting
code from ..." when the uids differ (imports/frameworks-base/core/java/android/app/ActivityThread.java:2859-2878),
`performLaunchActivity` takes the activity's own LoadedApk and makes that package's Application
(ActivityThread.java:3882-3960), and `acquireProvider` installs a provider locally when the
activity manager returns a holder marked `mLocal` (ActivityThread.java:7824-7880, 8228-8295).

```
 one OH process per Android app (all share the host HAP's uid)
 +-------------------------------------------------------------------------+
 | app classes + bundled play-services/firebase client libs (app loader)  |
 |   bindService(action, pkg=gms) | ContentResolver(authority) |          |
 |   sendBroadcast(pkg=gms) | startActivity(gms component)                |
 |          |  AdapterIAM-stub / ActivityTaskManagerAdapter / PM adapter   |
 |          v                                                             |
 | GuestPackages: gms + vending parsed from their APKs, Google cert       |
 | GuestHost: LoadedApk(gms) -> PathClassLoader(parent = boot)            |
 |   microG Services / Providers / Receivers / Activities, one per process|
 |   app <-> microG only through Parcel (local Binder.transact) or a      |
 |   Parcel copy: no class is shared except the framework's               |
 | LocalAccounts (IAccountManager), authenticator = microG's              |
 +-------------------------------^-----------------------------------------+
                                 | files: SQLite (WAL) + a lease file
          /data/data/com.google.android.gms   (one directory for all apps)
```

### Change list, by Westlake class

**1. Package manager: two guest packages, signature spoofed (`framework/package-manager/java`)**

New `adapter.packagemanager.GuestPackages`, a multi-package sibling of `SourcePackageRegistry`.
`SourcePackageRegistry.register` (SourcePackageRegistry.java:23-40) already parses an APK with
`PackageParser.parsePackage` and generates PackageInfo/ComponentInfo through
`PackageParser.generatePackageInfo` (:290-337); the guest registry does the same for the staged
GmsCore and Companion APKs, with three differences: `uid = Process.myUid()` (as the WebView
package), `dataDir` = the shared guest directory, and certificates not collected from the APK but
replaced by Google's release certificate (built the way `platformPackageInfo` builds the platform
certificate and `SigningInfo`, PackageManagerAdapter.java:714-751). Three details decide whether Google's
libraries accept the guests: `com.android.vending` must exist and carry the same first certificate
as `com.google.android.gms`, or every availability check returns `SERVICE_INVALID` (9); the
`signingInfo` must hold a single-signer history (SDK >= 28 path); and neither guest may be marked
`FLAG_SYSTEM`/`FLAG_UPDATED_SYSTEM_APP` or `FLAG_STOPPED`: a non-system GmsCore makes
DynamiteModule use the V1 loader that microG implements, and the bind helper refuses stopped
packages. Only the guests are spoofed;
the app's own package keeps its real certificate (SourcePackageRegistry.packageInfo collects it,
:293-301), which matters because microG sends the caller's real certificate SHA-1 to Google
(`cert` on c2dm/register3, `client_sig` on auth requests).

| PackageManagerAdapter method (line) | today | change |
|---|---|---|
| `getPackageInfo` (753), `getApplicationInfo` (804) | own package, `"android"`, sideloaded WebView, else OH BMS; misses remembered in `sAbsentPackages` (665) | ask `GuestPackages` right after `SourcePackageRegistry`; never cache a guest as absent |
| `getInstalledPackages` (837) | OH bundles only | append the guests |
| `resolveService`, `queryIntentServices` (1353, 1360) | `SourcePackageRegistry.queryServices`: own package only | add `GuestPackages.queryServices` when the intent names a guest package |
| `queryIntentReceivers` (1346), `queryIntentActivities` (932), `queryIntentContentProviders` (1367, returns null) | own package (+ OH abilities for activities) | add guest matches |
| `resolveContentProvider` (1414) | searches the own package's providers only | also the guests' authorities |
| `getServiceInfo` (1212), `getReceiverInfo` (1197), `getProviderInfo` (1236), `getActivityInfo` (869) | own package, else manifest JSON / OH | guest components |
| `getPackageUid` (1004) | own -> myUid, else OH | guests -> myUid |
| `getPackagesForUid`, `getNameForUid` (1272, 1281) | `[own]` | keep the own package first (and `getNameForUid` = own); append the guests, which share the uid |
| `checkSignatures` (1014) | always `SIGNATURE_MATCH` | compare real signature sets: an app is not Google-signed |
| `hasSigningCertificate`, `hasUidSigningCertificate` (345-353) | always false | answer from the same certificates |
| `PermissionManagerAdapter.isOwn` (PermissionManagerAdapter.java:59) and `checkPermission(pkg, perm, ...)` (:128-133), which Android 15's `PackageManager.checkPermission` reaches through PermissionManager (`PackageManagerAdapter.checkPermission`, :989, asks OH by package name) | the app package only; any other package is denied everything | a guest holds the permissions its manifest requests, as a privileged microG would: Firebase refuses to use Play services unless `checkPermission("com.google.android.c2dm.permission.SEND", "com.google.android.gms")` is granted (cloud-messaging's IID selection); runtime permissions follow the process's OH grants (`checkProcessPermission`, :35) |

**2. A guest host (`adapter.activity.GuestHost`, new)**

Generalises `InProcessServiceBinder` (framework/activity/java/InProcessServiceBinder.java), which
today loads the service class from `app.getClassLoader()` and attaches it to the app
(:75-93), and `ManifestReceivers`, which instantiates receivers from the app's LoadedApk (:107-115).
For a guest package it:

- creates the guest's LoadedApk once per process through
  `createPackageContext(pkg, CONTEXT_INCLUDE_CODE | CONTEXT_IGNORE_SECURITY)`, then
  `LoadedApk.makeApplicationInner` (GmsCore's manifest names no Application class, so this is a
  plain `android.app.Application` with the GMS base context);
- creates guest services with the guest class loader and `Service.attach(guestContext, thread,
  className, token, guestApplication, ActivityManager.getService())`, keeps them per class name as
  InProcessServiceBinder does, and delivers `onBind`'s binder to the caller's `IServiceConnection`
  on the main looper;
- instantiates guest providers (`instantiateProvider` + `attachInfo(guestContext, info)`) and
  guest manifest receivers (with `getReceiverRestrictedContext` of the guest context);
- copies every Intent, Bundle and Message that crosses between app classes and guest classes
  outside a Binder transaction through a Parcel (`writeToParcel` then `createFromParcel` with the
  receiver's class loader). In a real system the process boundary does this; in process, a
  `GoogleSignInAccount` created by microG's class and read by the app's obfuscated copy of the
  same class is a ClassCastException.

AIDL calls need no copy: the client library's `IGmsServiceBroker.Stub.asInterface` finds a local
interface of another class loader, fails its `instanceof`, builds its own Proxy and goes through
`Binder.transact`, which for a local Binder marshals into a Parcel and calls `onTransact` on the
same thread.

What the guest host instantiates, and when (classes are microG's at the pinned commit):

| Trigger in the app process | microG class |
|---|---|
| first guest touch | plain `android.app.Application` on the GMS base context (GmsCore declares no Application class) |
| bind `com.google.android.gms.signin.service.START` | `org.microg.gms.signin.SignInService` (a `BaseService` returning `AbstractGmsServiceBroker`) |
| bind `...auth.api.signin.service.START`, `...identity.service.signin.START` | `org.microg.gms.auth.signin.AuthSignInService`, `org.microg.gms.auth.credentials.identity.IdentitySignInService` |
| bind explicit `com.google.android.gms/.auth.GetToken` | `com.google.android.gms.auth.GetToken` -> `org.microg.gms.auth.AuthManagerServiceImpl` |
| bind / start `com.google.android.c2dm.intent.REGISTER`; broadcast `com.google.iid.TOKEN_REQUEST` | `org.microg.gms.gcm.PushRegisterService`; `org.microg.gms.gcm.PushRegisterReceiver` |
| lease holder only (phase 2) | `org.microg.gms.gcm.McsService`, `org.microg.gms.checkin.CheckinService` |
| bind `com.google.android.location.internal.GoogleLocationManagerService.START` | `org.microg.gms.location.manager.GoogleLocationManagerService` |
| authority `com.google.android.gms.chimera`, `com.google.android.gsf.gservices`, `com.google.android.gms.phenotype`, `com.google.android.gms.microg.settings` | `org.microg.gms.chimera.ServiceProvider`, `org.microg.gms.gservices.GServicesProvider`, `org.microg.gms.phenotype.ConfigurationProvider`, `org.microg.gms.settings.SettingsProvider` |
| Dynamite (Google's client code loads it from the guest APK itself) | `com.google.android.gms.chimera.container.DynamiteLoaderImpl`, then module classes such as `com.google.android.gms.maps.internal.CreatorImpl` and `com.google.android.gms.common.GoogleCertificatesImpl` |
| `LocalAccounts` needs the `com.google` authenticator | `org.microg.gms.auth.loginservice.GoogleLoginService` -> `AccountAuthenticator` |
| activity starts (change 4) | `org.microg.gms.auth.signin.AuthSignInActivity`, `org.microg.gms.auth.login.LoginActivity`, `org.microg.gms.auth.AskPermissionActivity`, `org.microg.gms.auth.signin.AssistedSignInActivity`, `org.microg.tools.AccountPickerActivity` |
| Companion binds | `com.google.android.finsky.integrityservice.IntegrityService`, `com.google.android.finsky.externalreferrer.GetInstallReferrerService`, `com.android.vending.billing.InAppBillingService`, `com.android.vending.licensing.LicensingService` |

**3. The direct-launch activity manager (`AppSpawnXInit.makeStubHandler`, AppSpawnXInit.java:1232-1371)**

| IActivityManager call | today | change |
|---|---|---|
| `bindService` / `bindServiceInstance` (1266-1278) | only an explicit component of the own package; everything else answers 0 ("bind failed") | resolve the intent (component, or action + package) through the package manager; own -> `InProcessServiceBinder`, guest -> `GuestHost.bind`; client libraries bind with action + `setPackage("com.google.android.gms")`, so today every GmsClient bind fails |
| `unbindService` (1280) | `true` | guest: `onUnbind` when the last connection goes |
| `startService` / `stopService` (1287-1299) | own package only (`InProcessServiceBinder.resolveInApp` requires the own package, :124-142) | guest -> `GuestHost.start` / `stop` (the C2DM REGISTER intent is a startService) |
| `getContentProvider` | not answered: the type default `null`, so `ContentResolver` gets no provider for any foreign authority | guest authority -> `ContentProviderHolder` with the provider `GuestHost` created (or `mLocal = true` and let `ActivityThread.installProvider` create it) |
| `broadcastIntentWithFeature` / `broadcastIntent` (1318-1326) -> `LocalBroadcasts.broadcast` | delivered only when the intent names no package or the own package (LocalBroadcasts.java:87-121) | an intent for a guest package goes to the guest's manifest receivers (`com.google.iid.TOKEN_REQUEST`, check-in and GCM triggers); a broadcast microG sends to the app (`setPackage(app)`) already counts as "for us" |
| `getIntentSender` (1332-1339) -> `LocalIntentSenders` | records the creator package the caller passes | unchanged: `PendingIntent.getCreatorPackage()` already answers the app (LocalIntentSenders.info, :147-152), which is how microG identifies the app behind the `"app"` extra; a guest-created activity sender starts the guest activity through change 4 |

**4. Guest activities in the app's task (`ActivityTaskManagerAdapter`, `AppSchedulerBridge`, `ActivityResults`, `ActivityClientControllerAdapter`)**

Sign-in, consent, account pickers, the login WebView and resolution screens are activities of
the GMS package. Today a start of one goes to `startOutside` (ActivityTaskManagerAdapter.java:195-216),
nothing on OH handles it, and the caller gets ActivityNotFoundException or nothing.

- `startActivity` (:142-179): `resolveOwnActivity` (:308-320) also resolves guest activities;
  `startsOwnActivity` (:322-332) is true for a guest component, so it is posted to the main looper
  like an own activity.
- `bridgeStartActivityWithStack` (:1181-1290) starts it as one of the app's own abilities (the
  bundle is the app's, the ability name the guest class), and `PendingLaunchIntents.offer` marks it
  as a guest launch.
- `AppSchedulerBridge.nativeOnScheduleLaunchAbility` (AppSchedulerBridge.java:1822-1935) today
  builds the ActivityInfo from the app's ApplicationInfo (`buildActivityInfoFromAbility`, :1123);
  for a guest launch it takes the guest's ActivityInfo (guest ApplicationInfo, theme, launch mode),
  so `performLaunchActivity` uses the GMS LoadedApk, resources and Application.
- `ActivityResults.deliver` / `sendNow` (ActivityResults.java:73-100) copy the result Intent
  through a Parcel when it crosses the guest boundary.
- `ActivityClientControllerAdapter.getCallingActivity` / `getCallingPackage` (ActivityClientControllerAdapter.java:488-491)
  answer null today; they must answer from the ActivityResults request (the caller's token ->
  `ActivityThread.getActivity(resultTo).getComponentName()`), because microG's sign-in and consent
  screens identify the requesting app by its calling activity. Without this, legacy Google Sign-In
  ends in DEVELOPER_ERROR (status 10) at AuthSignInActivity.kt:66-68: a precise early test.
- (4b) `ActivityTaskManagerAdapter.startActivityIntentSender` (:370-378) ignores the
  `IIntentSender` and hands the fill-in Intent to OH. The Identity API, androidx.credentials'
  `HiddenActivity` and every resolution PendingIntent go through `startIntentSenderForResult`, so
  it must look the sender up in `LocalIntentSenders`, fill it in, and start it like
  `startActivity` with `resultTo` / `requestCode` so the result comes back.

**5. Accounts (`LocalServiceBinders.account`, LocalServiceBinders.java:74, 908-923)**

Today `account` answers "signed in nowhere": empty lists, `addAccountExplicitly` false. microG
keeps Google accounts, the master token and cached tokens in Android's AccountManager, so Westlake
needs a real one: `adapter.core.LocalAccounts`, an `IAccountManager` over a SQLite store in the
guest directory (the `accounts`, `extras`, `authtokens`, `visibility` tables of AOSP's
AccountsDb), with authenticators discovered as AccountManagerService discovers them (services
with action `android.accounts.AccountAuthenticator` and their `android.accounts.AccountAuthenticator`
meta-data XML, here from `GuestPackages`) and bound in process through `GuestHost`. `addAccount`
and `getAuthToken` call the authenticator's `IAccountAuthenticator` and start the `KEY_INTENT`
activity it returns through change 4. Visibility follows Android 8+ rules (an app sees a Google
account after it was picked for it or made visible by microG).

**6. Shared directory and cross-process state**

- `/data/data/com.google.android.gms` and `/data/data/com.android.vending`, staged by the launcher
  like an app's data directory (AppSchedulerBridge.java:196-215 expects the launcher to stage
  `/data/data/<pkg>`). appspawn-x does not isolate app file systems yet
  (`ChildMain::applySandbox`, framework/appspawn-x/src/child_main.cpp:1692-1731, "filesystem
  isolation not yet implemented"), so every Westlake process already sees these directories under
  the shared uid; `appspawn_x_sandbox.json` (:124-125) shows the per-package mount that would make
  them invisible to apps once the sandbox is implemented.
- SQLite in WAL mode is safe across processes; SharedPreferences are not. microG funnels its
  settings through one ContentProvider precisely because "normal SharedPreferences" are not safe
  across processes (play-services-base/core/src/main/kotlin/org/microg/gms/settings/SettingsProvider.kt:37-41),
  but in Westlake each app process instantiates its own copy of that provider over the same
  files. The Westlake build of microG replaces the provider's SharedPreferences with a SQLite
  table (small, contained patch); `GuestHost` additionally wraps the guest context's
  `getSharedPreferences` with a reload-on-change, lock-on-write implementation for the few direct
  users.
- Singletons (check-in, the MCS connection) run under a lease: a lease file in the guest directory
  holding the owner pid and a heartbeat timestamp, taken over when stale (a frozen owner must lose
  it, so not a plain `flock`).
- Defaults: the Westlake build ships microG defaults (check-in on, cloud messaging on, no
  per-app push confirmation) through the mechanisms microG already reads: `/system/etc/microg.xml`,
  manifest meta-data `org.microg.gms.settings.*` (SettingsProvider.kt:35, 62-99).

**7. microG build for Westlake (a product flavor, kept small to track upstream)**

Package names and the client-facing AIDL stay unchanged. The flavor: sets the defaults above
(check-in `checkin_enable_service` and `gcm_enable_mcs_service` default to off upstream,
SettingsProvider.kt:154 and play-services-base/core/.../SettingsContract.kt:97-154); moves
SettingsProvider storage to SQLite; gates `McsService` and check-in on the Westlake lease and adds
the push outbox; does not post microG's own foreground-service and status notifications inside
apps; drops UI that assumes a standalone app (launcher icon, self-check). It also pins the reported
versionCode: firebase-messaging 26 switches to a `com.google.android.gms.cloudmessaging.service.START`
API at GMS >= 261200000, and androidx.credentials 1.6 to `identitycredentials` at >= 252400000, so
raising microG's version must wait until microG implements what the higher number promises.
Everything else is upstream code; microG releases every one to three months (v0.3.14 on
2026-04-10 to v0.3.17 on 2026-09-29), so the flavor is rebased each release.

## Where it breaks

In-process hosting gives up whatever needs a process boundary or a process that outlives the app.

1. **Nothing runs while no app runs.** No MCS session, no check-in, no token refresh. Messages wait
   at Google (or in the outbox) and notifications for closed apps never show. Answer: gmsd (Push,
   stage 2; phase 3 of the plan).
2. **Several copies of microG at once.** Each app process has its own microG: static singletons such
   as McsService's socket (McsService.java:110-133) and the Dynamite caches
   (DynamiteContextFactory.java:33-35) are per process; check-in is synchronised only inside one
   process (CheckinManager.java:40); microG's settings provider, which exists to make settings safe
   across microG's own processes, becomes one instance per app over the same files. The lease,
   the SQLite settings store and the outbox (changes 6 and 7) handle this for phase 1; the account store
   must re-read the database rather than cache it.
3. **No isolation of Google credentials.** All Westlake apps share one OH uid and appspawn-x does not
   yet give apps separate file systems (child_main.cpp:1726), so any app can read the master token
   in the shared directory, as it can already read any other app's data. The login WebView and the
   consent screens run inside the requesting app's process, so that app's code could read what the
   user types. Acceptable for an internal MVP with test accounts, not for users: phase 3 moves the
   account store, login and token minting into gmsd and mounts its directory only there.
4. **Caller identity is always "self".** In process every call carries the app's own uid and pid.
   microG's main check resolves the pid through `getRunningAppProcesses()`, which Westlake answers
   with the app's package alone (framework/activity/java/CallerProcess.java:24-37), so callers are
   still named correctly. But the shortcuts that treat `Binder.getCallingUid() == Process.myUid()`
   as "the caller is microG" are always true: any app can read `android_id` from the gservices
   provider (GServicesProvider.java:119, 151, 184), and location treats every client as itself
   (play-services-location/core/.../manager/extensions.kt:71-72); also InternalIntentMessenger.kt:13,
   SmsRetrieverCore.kt:146, GcmInGmsService.kt:143. If tests show functional differences, wrap
   guest binders so that their calls run under a synthetic per-app calling uid
   (`Binder.restoreCallingIdentity` with that uid before delegating to the guest `onTransact`) and
   teach the package and permission adapters that uid.
5. **microG's process split collapses.** `:persistent`, `:ui` and DroidGuard's
   `com.google.android.gms.unstable` all run in the app process. A crash in DroidGuard's native VM,
   which Google ships and microG downloads, takes the app down; microG's own notifications, dialogs
   and foreground-service notice appear as the app's (the Westlake flavor suppresses them).
6. **Memory and start-up.** Every app process that touches GMS loads GmsCore's dex and resources,
   and every sampled app does so at start-up (FCM token fetch; Firebase's init provider in 17 of
   30), plus one Dynamite class loader per module and app. Measure in phase 0; AOT-compile the
   guest APKs into the runtime image (compiling Westlake's boot image with the speed filter cut X's
   first frame from 29 s to 4.7 s on build 80, so compiled code matters at this scale) and start
   GuestHost lazily, at the first guest touch.
7. **No package events.** microG unregisters an app's push and clears its sign-in state on
   `PACKAGE_REMOVED` / `PACKAGE_DATA_CLEARED` (PersistentTrustedReceiver, AndroidManifest.xml:410-419).
   Westlake sends no install events, so registrations of removed apps linger until the launcher
   reports removals (phase 3).
8. **Backgrounding.** When OH suspends the lease holder, its socket and alarms stop; the lease goes
   stale and the next running Westlake process takes it. Alarms of microG live only as long as the
   process (LocalAlarms), as for apps.
9. **Device identity.** Westlake's Build properties say `OpenHarmony/rk3568/rk3568:14/oh-adapter:user/release-keys`
   (framework/android-runtime/src/android_os_SystemProperties.cpp:93-102). Check-in, the login user
   agent, DroidGuard and Play Integrity use microG's device profile instead; one consistent profile
   must be chosen and kept, since the androidId, tokens and DroidGuard results are bound to it.

## Phased plan with estimates

Estimates are engineer-weeks (ew) for engineers who know Westlake's adapters, including probe and
harness work, excluding legal review and upstreaming to microG. Verification follows the harness's
existing pattern: a probe APK prints one `[WL-CONTRACT] <row> PASS|ABSENT|FAIL` line per row
(as `westlake-harness/probes/framework-contracts` does), plus scored flows on corpus apps. The
GMS probe must be built against Google's real client libraries (Gradle, play-services-base/-auth/
-location/-maps, firebase-messaging, Play integrity, androidx.credentials), unlike the existing
probes, which use only android.jar.

### Phase 0: groundwork and the two spikes (3 ew)

- Build GmsCore and Companion v0.3.17 with the Westlake flavor (defaults by meta-data first, patches
  later); stage both APKs, extracted arm64 libraries and AOT output in the runtime image. 0.5 ew.
- Probe `gms-contracts` and harness rows `gms:*`; gap-map rows `dep:google-play-services` and
  `dep:gms-client-api-calls` (westlake-harness `harness/westlake_gap/gapmap.py:3370-3385`) start
  reading the probe instead of "absent". 1 ew.
- Spike: one guest LoadedApk + Application in a Westlake process and one broker bind; measure the
  memory and start-up cost per app. 0.5 ew.
- Spike: DroidGuard's VM and one Google login with a test account in a Westlake process (login
  WebView, check-in, `minute_maid`), to learn early whether risk 1 below is real. 0.5 ew.
- Operations (start now; they gate every later phase): a board network with unfiltered HK egress
  (the board's DNS answers Google domains wrongly), Google test accounts, a Firebase project with a
  server key for sending test messages, a Cloud project to decode Play Integrity tokens. 0.5 ew
  plus purchasing.

### Phase 1: MVP: GMS present, checks pass, broker, Dynamite, accounts, sign-in (11.5 ew)

| Work | ew |
|---|---|
| `GuestPackages`, package-manager and permission changes, signature spoofing (change 1) | 1.5 |
| `GuestHost` and the activity-manager routes: bind, start, providers, broadcasts, Parcel copies (changes 2, 3) | 2 |
| guest activities, calling activity, `startActivityIntentSender`, results (change 4) | 1.5 |
| `LocalAccounts` (change 5) | 2 |
| shared directory, SQLite settings, lease, defaults (changes 6, 7) | 1.5 |
| check-in, login WebView and DroidGuard working in Westlake, after the phase 0 spike (the riskiest item) | 2 |
| sign-in flows across corpus apps (legacy, Identity, androidx.credentials) | 1 |

Verify: rows `gms:package` (both guests found, Google certificate, single signer),
`gms:availability` (`isGooglePlayServicesAvailable == 0`), `gms:broker` (a broker call returns,
e.g. App Set ID), `gms:provider` (chimera `serviceIntentCall`), `gms:dynamite` (googlecertificates
and maps_dynamite versions > 0), `gms:signin` (the GOOGLE_SIGN_IN intent opens microG's screen and
returns CANCELED on back, not DEVELOPER_ERROR), `gms:accounts` (login adds a `com.google`
account), `gms:token` (GoogleAuthUtil token for a test OAuth client), `gms:credman` (androidx
getCredential reaches microG). Corpus: no "Google Play services" error dialog
(`GoogleApiActivity`) in any of the 30; Google sign-in completes with a test account in at least 5
of Duolingo, Spotify, Reddit, Zoom, Discord, Uber, NYTimes, Flipboard.

### Phase 2: FCM while an app runs (6 ew)

| Work | ew |
|---|---|
| registration paths and the c2dm permission answers | 0.5 |
| lease-held MCS session, outbox, FileObserver wake, drain at start (microG flavor + GuestHost) | 2 |
| OH notification bridge (publish, WantAgent back to the host, start the app); also needed by every app that notifies (30/30) | 2.5 |
| network changes, heartbeat, reconnect; 24 h soak | 1 |

Verify: `gms:fcm-token` (token within 30 s), `gms:fcm-receive` (a data and a notification message
sent from the test server arrive within 10 s while the probe runs), `gms:fcm-handoff` (a message
for app B while app A holds the lease reaches B when B starts), the notification visible in OH's
notification centre. Corpus: Telegram, Discord, Reddit, BBC News, Guardian, CNN, NYTimes, X and
Instagram obtain a token at start (log) and show a real push with test accounts.

### Phase 3: push while apps are closed, credentials out of apps (10.5 ew)

| Work | ew |
|---|---|
| gmsd: headless guest process started by appspawn-x, kept alive | 2 |
| appspawn-x-brokered per-app channels and the narrow RPC | 2 |
| microG split: push, check-in, accounts, tokens and login UI behind the RPC | 3 |
| headless app spawn for a message, idle exit | 1.5 |
| appspawn-x per-app file sandbox (`ChildMain::applySandbox`), so gmsd's directory is private | 1.5 |
| package removal and data-clear events from the launcher to gmsd | 0.5 |

Verify: `gms:fcm-closed` (probe process killed; a message starts it headless, its service runs, a
notification shows within 30 s), `gms:secrets` (an app process cannot open gmsd's directory), a 24 h
soak with heartbeats and reconnects. Corpus: the phase 2 messaging and news apps, closed.

### Phase 4: location, maps, integrity (11 ew; can run beside phases 2-3)

| Work | ew |
|---|---|
| Location: `LocationManagerAdapter` over OH's Locator, provider state from OH's switch, host location permissions and the runtime request relay, microG fused location validated | 3.5 |
| Maps: MapLibre engine on Westlake's GL and SurfaceView path, native-library loading from the guest APK, a tile contract and key, Dynamite validation | 5 |
| Integrity: Companion's integrity services and DroidGuard in Westlake, fail-fast errors, verdicts recorded per app | 2.5 |

Verify: `gms:location` (getCurrentLocation within 30 s on a device with a fix source or OH mock
locations), `gms:maps` (MapView draws tiles: pixel variance over the map), `gms:integrity` (a token
or a specific error code; the token decoded by the test backend). Corpus: location in Citymapper,
Moovit, Transit, Uber, Waze, SmartNews; maps in Citymapper, Moovit, Transit, Uber, Telegram and
WhatsApp location sharing, SmartNews, TikTok; integrity outcome (allowed, challenged, refused) in
Instagram, Threads, Messenger, X, Reddit, Discord, TikTok, CapCut, Uber, Duolingo, Spotify, Zoom.

### Total and calendar

About 42 ew (range 36-50). The critical path is phase 0 -> 1 -> 2 -> 3; phase 4 runs alongside.
With four engineers that is about 13-14 calendar weeks; the team plan's 12 weeks (core weeks 1-4,
push running 5-6, push closed 6-8, location/maps/attestation 9-12) needs five, and push-while-closed
is the part most likely to land last. Two items can move the whole schedule: login (DroidGuard) in
phase 1, and the notification bridge, without which push is invisible.

## Risks

1. **Google account login may not work reliably.** New-device logins fail upstream with
   `MissingDroidguard` (microG #3195, open) and DroidGuard has never run on this platform. Without
   login there are no tokens and no Google sign-in in any app. Mitigation: make it the first spike
   of phase 1, pick and freeze one device profile, track microG releases. There is no fallback for
   Google Sign-In.
2. **Play Integrity cannot be passed.** No DEVICE or STRONG verdict on a platform without Google-rooted
   attestation (Westlake's keystore is software); BASIC is doubtful. 22 of 30 apps reach the API;
   banking, payment and some games will refuse, social apps may add challenges. Permanent for any
   non-certified platform; measure and report per app, decide product scope.
3. **Legal and policy (needs counsel before anything ships to users).**
   - microG is Apache-2.0 (LICENSE; REUSE layout with `LICENSES/Apache-2.0`, `CC-BY-4.0` for about
     40 `package-info.java` files "reproduced from work created and shared by Google", `CC0-1.0`);
     bundled MapLibre (BSD-2), Conscrypt (Apache-2.0), Cronet (BSD-style), cryptauth protos
     (Chromium BSD); OpenCV's licence is not stated in the repo (UNVERIFIED). Avoid the VTM flavor
     (LGPLv3) and the hms flavor (proprietary Huawei Map Kit). Attribution and NOTICE files are
     routine.
   - The sensitive part is not the licence: Westlake would present Google's package names and
     certificates to apps, a microG device profile to Google's servers, and run Google's
     proprietary DroidGuard code fetched at runtime, in a commercial product. Google's terms for its
     APIs and Play services, GMS licensing for devices and the Android/Google trademarks all bear on
     that. microG's README gives no trademark guidance and the wiki no ToS statement.
   - Users' Google accounts could be flagged by Google's abuse systems (no ban reports found;
     UNVERIFIED). Check-in sends device data and each account's LSID to Google; personal-data rules
     in Hong Kong (PDPO) apply to that flow and to any server relay for push.
4. **Phase 1 has no credential isolation.** Any app can read the master token and observe the login
   screen (Where it breaks, 3). Phase 1 is for test accounts only; users wait for phase 3.
5. **Network.** The board network resolves Google domains wrongly; every GMS test needs an
   unfiltered HK uplink. In the field, HK networks reach Google normally.
6. **Cost per app.** GmsCore's dex and resources in every app process; measure in phase 0. If it is
   too high, move more of microG into gmsd earlier.
7. **Upstream churn.** microG releases every one to three months and the pinned release fixed a
   batch of security advisories (GHSA ids in Sources); Google changes server behaviour (BASIC integrity vanished for months in
   2025-26) and client-library gates (cloud messaging at GMS 26.12, androidx identity credentials at
   25.24). Keep the Westlake flavor small and rebase each release; pin the reported version.
8. **OpenHarmony limits.** No notification bridge yet; location needs host permissions and a fix
   source; on commercial HarmonyOS, background push would need a continuous task or a Push Kit
   relay. The board design assumes Westlake controls the system image.
9. **Test logistics at 1000 apps.** Google accounts with phone verification, app accounts (HK phone
   numbers), a sender for FCM tests, and a way to tell "app refused because of integrity" from
   "Westlake bug" in the harness.
10. **What microG does not implement.** Per microG's implementation-status page (last edited
    2025-05-15) and the code: mobile ads are "not intended" (13 of 30 apps carry an AdMob id;
    rewarded ads in games will not load), in-app updates none, Games partial (294 of the HK top
    1000 are games), Cast partial, geofencing and activity recognition none, Fitness and most of
    Wearable none. Companion's billing, licensing and asset delivery exist but are off by default
    (SettingsProvider.kt:406-411); turning billing on means real Google purchases through an
    unofficial client, which is a payments and policy decision, not only an engineering one.
11. **Google's own apps** (19 of the HK top 1000: Gmail, Maps, YouTube, Wallet, Gemini, Chrome...)
    lean on private Play-services APIs; microG gives known Google packages extra privileges
    (KnownGooglePackages.kt) but expect several, Wallet first, not to work.

## Sources

**microG** (github.com/microg/GmsCore at `32bc8954ff872d0e1a05ddfae81561b6dbecef69`, = tag
v0.3.17.252432; paths as cited inline). Main files: `play-services-core/src/main/AndroidManifest.xml`;
`play-services-base/core/src/main/java/org/microg/gms/{BaseService,AbstractGmsServiceBroker}.java`;
`play-services-base/core/src/main/java/org/microg/gms/common/PackageUtils.java`;
`play-services-basement/src/main/java/org/microg/gms/common/{GmsService,Constants}.java`;
`play-services-base/core/src/main/kotlin/org/microg/gms/settings/{SettingsProvider,SettingsContract}.kt`;
`play-services-core/src/main/kotlin/org/microg/gms/chimera/ServiceProvider.kt`;
`play-services-core/src/main/java/com/google/android/gms/chimera/{DynamiteContextFactory,container/DynamiteLoaderImpl}.java`;
`play-services-core/src/main/java/org/microg/gms/gcm/{McsService,PushRegisterManager,RegisterRequest,GcmDatabase,TriggerReceiver}.java`;
`play-services-core/src/main/kotlin/org/microg/gms/gcm/{PushRegisterService,GcmPrefs}.kt`;
`play-services-core/src/main/java/org/microg/gms/checkin/{CheckinService,CheckinManager,CheckinClient}.java`;
`play-services-core/src/main/java/org/microg/gms/auth/{AuthManager,AuthManagerServiceImpl,AskPermissionActivity,AccountContentProvider}.java`,
`.../auth/login/LoginActivity.java`, `.../auth/loginservice/AccountAuthenticator.java`;
`play-services-core/src/main/kotlin/org/microg/gms/auth/signin/*`,
`.../auth/credentials/identity/*`; `play-services-identity-credentials/core`;
`play-services-safetynet/core`, `play-services-recaptcha/core`, `play-services-droidguard/{core,src}`;
`vending-app/src/main/AndroidManifest.xml`, `vending-app/src/main/kotlin/com/google/android/finsky/{integrityservice,expressintegrityservice}`;
`play-services-location/core/{src,provider}`; `play-services-maps/core/{mapbox,hms,vtm}`;
`fake-signature/src/{main,huawei}`; root and module `build.gradle`; `LICENSE`, `LICENSES/`.
Wiki: Home, Installation, Signature-Spoofing, Helpful-Information, Google-Network-Connections,
Implementation-Status (last edited 2025-05-15). Issues and PRs: #242, #1253, #2108, #2747,
#2919, #3195, #3198, #3257, #3300, #3738, #3796, #3853; advisories GHSA-rp4p-8x89-jhv5,
GHSA-hgxr-rxcr-m6vf, GHSA-qqqw-h762-w2hw, GHSA-gxx5-cwvx-xch5, GHSA-wvhf-q3j8-32jw,
GHSA-phcc-cx4h-vmq3, GHSA-frxh-v6rf-xfg4, GHSA-29m7-f2vx-8cqq. Related repos:
microg/GsfProxy, microg/patches, LineageOS android_frameworks_base (signature spoofing, commit
198fa72864), lineageos4microg android_vendor_partner_gms.

**Google client libraries** (Google Maven, read in memory): play-services-basement 18.12.0, base
18.11.1, auth 22.0.0, auth-api-phone 18.3.1, auth-base 18.3.4, location 21.4.0, maps 20.1.0,
cloud-messaging 17.4.0, safetynet 18.1.0, tasks 18.4.1; firebase-messaging 26.0.1, firebase-iid
21.1.0, firebase-installations 20.0.0, firebase-auth 25.0.0; com.google.android.play:integrity
1.6.0; androidx.credentials:credentials-play-services-auth 1.6.0; googleid 1.2.1.

**Google documentation:** Play Integrity verdicts, improvements (Android 13+), error codes and
IntegrityErrorCode reference (developer.android.com/google/play/integrity/...); SafetyNet
deprecation timeline; Firebase "Android Play services dependencies" and the 2020 Firebase Auth
non-GMS post; GoogleAuthUtil reference; Google's 2021 embedded-WebView OAuth policy post.

**Other:** Chromium `google_apis/gcm/engine/{mcs_client,gservices_settings,checkin_request}.cc`
(read at main, 2026-10-23); liamcottle/rustplus.js #44; MapLibre Native android-v10.2.0.

**Westlake** (`source-closure/workspace/westlake`, origin/main 1b0fdda):
`framework/appspawn-x/java/com/android/internal/os/AppSpawnXInit.java` (makeStubHandler 1232-1371,
IActivityManager install 1410-1488); `framework/activity/java/{InProcessServiceBinder,LocalBroadcasts,ManifestReceivers,LocalIntentSenders,ActivityTaskManagerAdapter,AppSchedulerBridge,ActivityResults,ActivityClientControllerAdapter,CallerProcess}.java`;
`framework/package-manager/java/{PackageManagerAdapter,SourcePackageRegistry,PermissionManagerAdapter,PermissionMapper,ServiceInfoResolver}.java`;
`framework/core/java/{LocalServiceBinders,LocationManagerAdapter,LocalNotifications,LocalAlarms,SoftwareAndroidKeyStore,OHServiceManager}.java`;
`framework/contentprovider/java/ContentProviderRegistry.java`;
`framework/appspawn-x/src/{child_main.cpp,main.cpp,spawn_msg.h}`,
`framework/appspawn-x/config/{appspawn_x.cfg,appspawn_x_sandbox.json}`;
`framework/android-runtime/src/android_os_SystemProperties.cpp`; `apps/imehost/module.json`;
`native/host_spawn.c`.

**AOSP 15** (`source-closure/imports/frameworks-base`, android-15.0.0_r1):
`core/java/android/app/ActivityThread.java` (getPackageInfo 2855-2878, performLaunchActivity
3882-3960, acquireProvider 7824-7880, installProvider 8228-8295),
`core/java/android/app/SystemServiceRegistry.java:1225-1236`.

**OpenHarmony source tree**: `base/location/interfaces/inner_api/include/locator_impl.h`,
`base/notification/distributed_notification_service/interfaces/inner_api/notification_helper.h`.

**Corpus and harness:** manifests of the 30 sampled apps (`source-closure/inputs/corpus-top/ext/*/`,
aapt2 `dump xmltree`); reach counts from the corpus scan (30 commercial / 371 apps);
`source-closure/inputs/corpus-hk/hk-top1000.csv` (1000 apps: 706 apps, 294 games, 19 by Google);
`westlake-harness` (`probes/framework-contracts`, `harness/westlake_gap/{gapmap,contracts}.py`).
