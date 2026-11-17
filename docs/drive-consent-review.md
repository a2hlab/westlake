# Drive test consent review

Drive's original APK now reaches the official GMS native-auth route from Android
AccountManager. Its legacy request includes permissions beyond the eight already
approved. The provider records `scope_not_granted`; those extra permissions were
initially withheld. The user subsequently approved these 19 scopes. The existing eight-scope requests still return tokens.

The approved change adds exactly the 19 scopes below to this original APK's current
signer, existing test account and current revocation epoch. The original consent
expiry would be retained. No direct microG client-token grant would be added.
These scopes include full Drive access, Gmail read-only and People read/write;
they are broader than the planned read-only file actions. This approval alone
would authorize only listing/opening for validation, with no file upload, edit
or deletion.

- `https://www.googleapis.com/auth/drive.metadata.readonly`
- `https://docs.google.com/feeds`
- `https://docs.googleusercontent.com`
- `https://spreadsheets.google.com/feeds`
- `https://www.googleapis.com/auth/activity`
- `https://www.googleapis.com/auth/cloudprint`
- `https://www.googleapis.com/auth/discussions`
- `https://www.googleapis.com/auth/docs`
- `https://www.googleapis.com/auth/drive.apps`
- `https://www.googleapis.com/auth/drive.categories.readonly`
- `https://www.googleapis.com/auth/memento`
- `https://www.googleapis.com/auth/spreadsheets`
- `https://www.googleapis.com/auth/vouchers`
- `https://www.googleapis.com/auth/gmail.readonly`
- `https://www.googleapis.com/auth/subscriptions`
- `https://www.googleapis.com/auth/peopleapi.readwrite`
- `https://www.googleapis.com/auth/drive`
- `https://www.googleapis.com/auth/drive.file`
- `https://www.googleapis.com/auth/drive.activity.readonly`

The approved consent contains 27 scopes. The original APK is unchanged.
App file listing/opening and a positive AccountManager token result remain
unvalidated until a broader request is actually approved and tested.

Google subsequently returned `drive.labels.readonly`, which is outside this set.
The strict returned-scope check withholds the token. Separate explicit approval
for that one read-only scope is pending; it has not been added. The user also
separately approved one labelled 107-byte text-file creation/opening test; its
backend creation and exact-content read-back succeeded, with no other file
changes. Original-app listing/opening remain incomplete.
