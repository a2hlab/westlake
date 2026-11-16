package adapter.activity;

import android.app.Activity;
import android.content.Intent;
import android.os.Parcel;
import android.os.Parcelable;

/** Opt-in, value-free diagnostics for the Google sign-in activity boundary. */
final class GoogleSignInDiagnostics {
    private static final boolean ENABLED =
            "1".equals(System.getenv("WESTLAKE_GOOGLE_SIGNIN_DIAGNOSTICS"))
            && "1".equals(System.getenv("WESTLAKE_GOOGLE_PRIVATE_LOGS"));

    private GoogleSignInDiagnostics() {}

    static void externalResolution(Intent intent, boolean ohHandler, int requestCode) {
        if (!ENABLED || !googleIntent(intent)) return;
        event("external_start action=GOOGLE_SIGN_IN target=com.google.android.gms"
                + " request_code=" + requestCode + " oh_handler=" + ohHandler);
    }

    static void unresolved(Intent intent, int startResult) {
        if (ENABLED && googleIntent(intent)) event("external_start unresolved=true start_result=" + startResult);
    }

    private static boolean googleIntent(Intent intent) {
        if (intent == null || !"com.google.android.gms.auth.GOOGLE_SIGN_IN".equals(intent.getAction())) return false;
        String target = intent.getComponent() == null ? intent.getPackage() : intent.getComponent().getPackageName();
        return "com.google.android.gms".equals(target);
    }

    static void result(Activity caller, Intent data, int requestCode, int resultCode) {
        if (!ENABLED || data == null) return;
        try {
            // Read a copy with the caller's loader. Never change the result delivered to the app.
            Intent copy = new Intent(data);
            copy.setExtrasClassLoader(caller.getClassLoader());
            if (!copy.hasExtra("googleSignInStatus")) return;
            Parcelable status = copy.getParcelableExtra("googleSignInStatus");
            if (status == null || !"com.google.android.gms.common.api.Status".equals(status.getClass().getName())) {
                event("activity_result status_type_expected=false");
                return;
            }
            // R8 removed getStatusCode() in the tested Spotify SDK. Read only SafeParcel's
            // integer field 1; never decode/log status messages, account data or PendingIntents.
            Parcel parcel = Parcel.obtain();
            try {
                status.writeToParcel(parcel, 0);
                parcel.setDataPosition(0);
                int code = statusCode(parcel);
                event("activity_result request_code=" + requestCode + " result_code=" + resultCode
                        + " google_status=" + code);
            } finally {
                parcel.recycle();
            }
        } catch (Throwable ignored) {
            event("activity_result status_decode_failed=true");
        }
    }

    private static int statusCode(Parcel parcel) {
        if (parcel.dataAvail() < 4) throw new IllegalArgumentException();
        int header = parcel.readInt();
        if ((header & 0xffff) != 20293) throw new IllegalArgumentException();
        int size = fieldSize(parcel, header, parcel.dataSize());
        int end = parcel.dataPosition() + size;
        while (parcel.dataPosition() < end) {
            if (end - parcel.dataPosition() < 4) throw new IllegalArgumentException();
            header = parcel.readInt();
            size = fieldSize(parcel, header, end);
            if ((header & 0xffff) == 1 && size == 4) return parcel.readInt();
            parcel.setDataPosition(parcel.dataPosition() + size);
        }
        throw new IllegalArgumentException();
    }

    private static int fieldSize(Parcel parcel, int header, int end) {
        int size = header >>> 16;
        if (size == 0xffff) {
            if (end - parcel.dataPosition() < 4) throw new IllegalArgumentException();
            size = parcel.readInt();
        }
        if (size < 0 || size > end - parcel.dataPosition()) throw new IllegalArgumentException();
        return size;
    }

    private static void event(String value) {
        System.err.println("[WL-GOOGLE-SIGNIN] time=" + System.currentTimeMillis() + " " + value);
    }
}
