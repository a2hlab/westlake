package android.os;

public class Binder implements IBinder {
    private IInterface owner;
    private String descriptor;

    public void attachInterface(IInterface owner, String descriptor) {
        this.owner = owner;
        this.descriptor = descriptor;
    }

    @Override public IInterface queryLocalInterface(String requested) {
        return descriptor != null && descriptor.equals(requested) ? owner : null;
    }

    @Override public String getInterfaceDescriptor() { return descriptor; }
}
