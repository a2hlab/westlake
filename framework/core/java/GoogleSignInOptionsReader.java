package adapter.core;

import java.util.HashSet;
import java.util.Set;
import java.util.TreeSet;

/** Bounded reader for the pinned legacy GoogleSignInOptions SafeParcel, not caller identity. */
public final class GoogleSignInOptionsReader {
    public interface Reader {
        int position();
        int size();
        int integer();
        String string();
        String[] account();
    }
    public final Set<String> scopes = new TreeSet<>();
    public String accountName;
    private GoogleSignInOptionsReader() {}

    public static GoogleSignInOptionsReader read(Reader in) {
        if (in.size() - in.position() > 16_384) throw denied();
        if (integer(in) != 1) throw denied();
        GoogleSignInOptionsReader result = new GoogleSignInOptionsReader();
        int end = objectEnd(in, in.size());
        Set<Integer> seen = new HashSet<>();
        while (in.position() < end) {
            int header = integer(in), id = header & 65535;
            int next = end(in, length(in, header), end);
            if (!seen.add(id)) throw denied();
            switch (id) {
                case 1: if (integer(in) != 3) throw denied(); break;
                case 2: {
                    int count = integer(in);
                    if (count < 1 || count > 16) throw denied();
                    for (int i = 0; i < count; i++) {
                        if (integer(in) <= 0) throw denied();
                        int itemEnd = objectEnd(in, next);
                        Set<Integer> fields = new HashSet<>();
                        String scope = null;
                        while (in.position() < itemEnd) {
                            int h = integer(in), field = h & 65535;
                            int fieldEnd = end(in, length(in, h), itemEnd);
                            if (!fields.add(field)) throw denied();
                            if (field == 1) { if (integer(in) != 1) throw denied(); }
                            else if (field == 2) scope = in.string();
                            else throw denied();
                            if (in.position() != fieldEnd) throw denied();
                        }
                        if (scope == null || scope.length() > 512 || !scope.matches("[\\x21-\\x7e]+")
                                || !result.scopes.add(scope)) throw denied();
                    }
                    break;
                }
                case 3: {
                    if (in.position() != next) {
                        String[] account = in.account();
                        if (account == null || account.length != 2 || !"com.google".equals(account[1])
                                || account[0] == null || account[0].isEmpty() || account[0].length() > 512) throw denied();
                        result.accountName = account[0];
                    }
                    break;
                }
                case 4: case 5: case 6:
                    // ID tokens, server codes and forced code refresh need their own operation policy.
                    if (integer(in) != 0) throw denied();
                    break;
                case 7: case 8:
                    if (in.position() != next) {
                        String value = in.string();
                        if (value != null && !value.isEmpty()) throw denied();
                    }
                    break;
                case 9:
                    if (in.position() != next && integer(in) != 0) throw denied();
                    break;
                case 10:
                    if (in.position() != next) {
                        String value = in.string();
                        if (value != null && value.length() > 256) throw denied();
                    }
                    break;
                default: throw denied();
            }
            if (in.position() != next) throw denied();
        }
        if (in.position() != end || in.position() != in.size() || result.scopes.isEmpty()) throw denied();
        return result;
    }

    private static int integer(Reader in) {
        if (in.position() < 0 || in.position() > in.size() - 4) throw denied();
        return in.integer();
    }
    private static int length(Reader in, int header) {
        return header >>> 16 == 65535 ? integer(in) : header >>> 16;
    }
    private static int objectEnd(Reader in, int limit) {
        int header = integer(in);
        if ((header & 65535) != 20293) throw denied();
        return end(in, length(in, header), limit);
    }
    private static int end(Reader in, int length, int limit) {
        long end = (long) in.position() + length;
        if (length < 0 || end > limit || limit > in.size()) throw denied();
        return (int) end;
    }
    private static SecurityException denied() { return new SecurityException("silent options unsupported"); }
}
