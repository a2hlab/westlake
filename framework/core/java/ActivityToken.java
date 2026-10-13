package adapter.core;

import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.ResultReceiver;
import android.os.ShellCallback;

import java.io.FileDescriptor;

/**
 * An in-process activity's token. On Android an activity's token is a proxy to the system's record
 * of it, and ActivityThread takes a token that is a local Binder for an activity the system does
 * not know (one an ActivityGroup embeds): it will not relaunch it. With Binders for tokens,
 * Activity.recreate(), which apps call to apply a new theme or language, did nothing.
 *
 * This token is an IBinder of its own instead. Every service that takes it answers in process, so
 * it is never transacted or parceled, and it lives as long as the process.
 */
public final class ActivityToken implements IBinder {
    @Override
    public String getInterfaceDescriptor() {
        return null;
    }

    @Override
    public boolean pingBinder() {
        return true;
    }

    @Override
    public boolean isBinderAlive() {
        return true;
    }

    @Override
    public IInterface queryLocalInterface(String descriptor) {
        return null;
    }

    @Override
    public void dump(FileDescriptor fd, String[] args) {}

    @Override
    public void dumpAsync(FileDescriptor fd, String[] args) {}

    @Override
    public void shellCommand(FileDescriptor in, FileDescriptor out, FileDescriptor err, String[] args,
            ShellCallback shellCallback, ResultReceiver resultReceiver) {}

    @Override
    public boolean transact(int code, Parcel data, Parcel reply, int flags) {
        return code == PING_TRANSACTION;
    }

    @Override
    public void linkToDeath(DeathRecipient recipient, int flags) {}

    @Override
    public boolean unlinkToDeath(DeathRecipient recipient, int flags) {
        return true;
    }
}
