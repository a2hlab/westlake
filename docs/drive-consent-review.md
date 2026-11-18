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

The first two approvals covered 27 scopes. Google also returned
`https://www.googleapis.com/auth/drive.labels.readonly`. Westlake initially
withheld this broader response. The operator then explicitly approved the
additional read-only scope for the existing Drive test, bringing the total
to 28. Both of Drive's local scope lists were updated. Its package and signer
binding, test account, revocation epoch and original expiry were retained.

The authorized file operation is to list and open the existing
`Westlake-Drive-validation.txt`. Its creation was a separate one-time
approval. The original Drive APK subsequently opened the existing file and
displayed all three expected lines. The latest [board results](../tests/google-auth/full48/candidate89.json)
distinguish that app result from the earlier 107-byte Drive API read-back.
