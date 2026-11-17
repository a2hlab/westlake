# Full two-path application validation

Status: in progress. The goal is to fix and validate every application flow in
the [validation plan](../tests/google-auth/full48/validation-plan.json), including
backend use, renewal and restart. The
[previous batch](google-two-path-live44.md) remains the baseline. A connection,
account picker, or partial login is not a full application pass.

## Candidate 48: legacy Google sign-in eligibility

The restricted official GMS build rejected Spotify and Wattpad's legacy sign-in
service 91 before token delegation could run. The compatibility policy now adds
91 alongside the existing 212/213 identity exceptions. This does not alter the
stored uncertified status, enable every GMS service, grant token scopes, or
replace any server-side integrity verdict. The existing version pin, opt-in
guards and rollback journal remain in use.

The updated policy passes 18 host checks. The adapter, runtime packaging and
boot image build pass, and the board preload completes without class
initialization failures. Native artifacts and verified unchanged framework
outputs are reused. Official GMS is redeployed with carried account data; the
check-in identity and provider-grant hashes are unchanged, status remains 3,
and no certification-status override is present.

In a fresh Wattpad client, service 91 changes from callback status 9 to 0. The
app invokes the legacy sign-in service and receives its callbacks. It still
returns to the login screen without an account chooser or authenticated result.
The connection fix therefore exposes another failure, whose numeric sign-in
callback status is being traced. This is not an application acceptance pass.

Exact build and observed-result evidence is in
[candidate48.json](../tests/google-auth/full48/candidate48.json). Credentials,
account identifiers, tokens, raw logs and screenshots remain private.
