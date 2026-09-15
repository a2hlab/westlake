package android.os;

public interface IBinder {
    IInterface queryLocalInterface(String descriptor);
    String getInterfaceDescriptor();
}
