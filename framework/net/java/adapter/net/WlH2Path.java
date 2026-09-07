package adapter.net;

import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Minimal, stateful HPACK decoder that extracts the request pseudo-headers
 * (:path, :method, :authority) from outbound HTTP/2 HEADERS blocks.
 *
 * WHY IN-PROCESS: WlDegrade classifies by request path, but on the wire the path
 * is HPACK-encoded (Huffman + a per-connection dynamic table) inside the HEADERS
 * frame the app writes. To classify a live outbound request the adapter must
 * decode it. One decoder instance per h2 connection, fed each outbound HEADERS
 * block in stream order, so the dynamic table stays in sync (a later request's
 * :path can be an indexed reference to an entry a earlier request added).
 *
 * Scope: enough HPACK to read the pseudo-headers reliably -- integer and string
 * primitives, Huffman, the full static table, and a size-accounted dynamic table
 * with eviction. It does not need to materialise every header, only the three
 * pseudo-headers. Validated against real captured frames (WlH2PathTest) using the
 * Python hpack library as the oracle.
 *
 * Tables (Huffman + static) are generated from RFC 7541, not hand-typed.
 */
public final class WlH2Path {

    // RFC 7541 Appendix B: Huffman code and bit-length per symbol 0..255.
    private static final int[] HC = {8184,8388568,268435426,268435427,268435428,268435429,268435430,268435431,268435432,16777194,1073741820,268435433,268435434,1073741821,268435435,268435436,268435437,268435438,268435439,268435440,268435441,268435442,1073741822,268435443,268435444,268435445,268435446,268435447,268435448,268435449,268435450,268435451,20,1016,1017,4090,8185,21,248,2042,1018,1019,249,2043,250,22,23,24,0,1,2,25,26,27,28,29,30,31,92,251,32764,32,4091,1020,8186,33,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,252,115,253,8187,524272,8188,16380,34,32765,3,35,4,36,5,37,38,39,6,116,117,40,41,42,7,43,118,44,8,9,45,119,120,121,122,123,32766,2044,16381,8189,268435452,1048550,4194258,1048551,1048552,4194259,4194260,4194261,8388569,4194262,8388570,8388571,8388572,8388573,8388574,16777195,8388575,16777196,16777197,4194263,8388576,16777198,8388577,8388578,8388579,8388580,2097116,4194264,8388581,4194265,8388582,8388583,16777199,4194266,2097117,1048553,4194267,4194268,8388584,8388585,2097118,8388586,4194269,4194270,16777200,2097119,4194271,8388587,8388588,2097120,2097121,4194272,2097122,8388589,4194273,8388590,8388591,1048554,4194274,4194275,4194276,8388592,4194277,4194278,8388593,67108832,67108833,1048555,524273,4194279,8388594,4194280,33554412,67108834,67108835,67108836,134217694,134217695,67108837,16777201,33554413,524274,2097123,67108838,134217696,134217697,67108839,134217698,16777202,2097124,2097125,67108840,67108841,268435453,134217699,134217700,134217701,1048556,16777203,1048557,2097126,4194281,2097127,2097128,8388595,4194282,4194283,33554414,33554415,16777204,16777205,67108842,8388596,67108843,134217702,67108844,67108845,134217703,134217704,134217705,134217706,134217707,268435454,134217708,134217709,134217710,134217711,134217712,67108846};
    private static final int[] HL = {13,23,28,28,28,28,28,28,28,24,30,28,28,30,28,28,28,28,28,28,28,28,30,28,28,28,28,28,28,28,28,28,6,10,10,12,13,6,8,11,10,10,8,11,8,6,6,6,5,5,5,6,6,6,6,6,6,6,7,8,15,6,12,10,13,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,7,8,13,19,13,14,6,15,5,6,5,6,5,6,6,6,5,7,7,6,6,6,5,6,7,6,5,5,6,7,7,7,7,7,15,11,14,13,28,20,22,20,20,22,22,22,23,22,23,23,23,23,23,24,23,24,24,22,23,24,23,23,23,23,21,22,23,22,23,23,24,22,21,20,22,22,23,23,21,23,22,22,24,21,22,23,23,21,21,22,21,23,22,23,23,20,22,22,22,23,22,22,23,26,26,20,19,22,23,22,25,26,26,26,27,27,26,24,25,19,21,26,27,27,26,27,24,21,21,26,26,28,27,27,27,20,24,20,21,22,21,21,23,22,22,25,25,24,24,26,23,26,27,26,26,27,27,27,27,27,28,27,27,27,27,27,26};

    // RFC 7541 Appendix A: static table names/values, index 1..61.
    private static final String[] SN = {":authority",":method",":method",":path",":path",":scheme",":scheme",":status",":status",":status",":status",":status",":status",":status","accept-charset","accept-encoding","accept-language","accept-ranges","accept","access-control-allow-origin","age","allow","authorization","cache-control","content-disposition","content-encoding","content-language","content-length","content-location","content-range","content-type","cookie","date","etag","expect","expires","from","host","if-match","if-modified-since","if-none-match","if-range","if-unmodified-since","last-modified","link","location","max-forwards","proxy-authenticate","proxy-authorization","range","referer","refresh","retry-after","server","set-cookie","strict-transport-security","transfer-encoding","user-agent","vary","via","www-authenticate"};
    private static final String[] SV = {"","GET","POST","/","/index.html","http","https","200","204","206","304","400","404","500","","gzip, deflate","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","","",""};

    private static final int DYN_MAX_DEFAULT = 4096;

    private static final class Entry { final String n, v; Entry(String n,String v){this.n=n;this.v=v;} }
    private final Deque<Entry> dyn = new ArrayDeque<Entry>();
    private int dynSize = 0;
    private int dynMax = DYN_MAX_DEFAULT;

    public String path, method, authority;

    /** Decode one HEADERS block; updates dynamic table; fills the pseudo-header fields. */
    public void decode(byte[] b, int off, int len) {
        path = method = authority = null;
        if (b == null || off < 0 || len <= 0 || off + len > b.length) return;
        int[] p = { off };
        final int end = off + len;
        // Exception-safe by construction: a HEADERS block truncated by a
        // mid-transfer disconnect (or any malformed input) must never throw out of
        // here -- a partial decode leaves `path` null, and the classifier treats an
        // unknown path as fail-open (behave normally, do not degrade). This is the
        // "network disconnect must not terminate the app" guarantee at the decode
        // layer. The dynamic table can be left inconsistent after a bad block, so
        // it is reset to avoid poisoning subsequent well-formed blocks.
        try {
            while (p[0] < end) {
                int x = b[p[0]] & 0xff;
                if ((x & 0x80) != 0) {                    // indexed header field
                    int idx = readInt(b, p, 7);
                    Entry e = at(idx);
                    if (e != null) take(e.n, e.v);
                } else if ((x & 0x40) != 0) {             // literal, incremental indexing
                    String n = nameFor(b, p, 6);
                    String v = readStr(b, p);
                    add(n, v); take(n, v);
                } else if ((x & 0x20) != 0) {             // dynamic table size update
                    int sz = readInt(b, p, 5);
                    dynMax = sz; evict();
                } else {                                   // literal, no/never indexing (0x00/0x10)
                    String n = nameFor(b, p, 4);
                    String v = readStr(b, p);
                    take(n, v);
                }
            }
        } catch (Throwable t) {
            // Truncated / malformed block: keep whatever pseudo-headers we read,
            // drop the possibly-corrupt dynamic-table state, and return quietly.
            dyn.clear();
            dynSize = 0;
        }
    }

    private void take(String n, String v) {
        if (n == null) return;
        if (n.equals(":path")) path = v;
        else if (n.equals(":method")) method = v;
        else if (n.equals(":authority")) authority = v;
    }

    private String nameFor(byte[] b, int[] p, int prefix) {
        int idx = readInt(b, p, prefix);
        if (idx == 0) return readStr(b, p);           // new name follows
        Entry e = at(idx);
        return e == null ? null : e.n;
    }

    private Entry at(int idx) {
        if (idx <= 0) return null;
        if (idx <= SN.length) return new Entry(SN[idx-1], SV[idx-1]);
        int d = idx - SN.length - 1;                  // 0 = most-recent dynamic
        if (d < 0 || d >= dyn.size()) return null;
        int i = 0;
        for (Entry e : dyn) { if (i++ == d) return e; }
        return null;
    }

    private void add(String n, String v) {
        if (n == null) n = "";
        if (v == null) v = "";
        Entry e = new Entry(n, v);
        dyn.addFirst(e);
        dynSize += n.length() + v.length() + 32;
        evict();
    }

    private void evict() {
        while (dynSize > dynMax && !dyn.isEmpty()) {
            Entry e = dyn.removeLast();
            dynSize -= e.n.length() + e.v.length() + 32;
        }
    }

    /** HPACK integer with an N-bit prefix. */
    private static int readInt(byte[] b, int[] p, int n) {
        int mask = (1 << n) - 1;
        int i = b[p[0]++] & mask;
        if (i < mask) return i;
        int m = 0, x;
        do { x = b[p[0]++] & 0xff; i += (x & 0x7f) << m; m += 7; } while ((x & 0x80) != 0);
        return i;
    }

    /** HPACK string: 1-bit Huffman flag + length + octets. */
    private String readStr(byte[] b, int[] p) {
        boolean huff = (b[p[0]] & 0x80) != 0;
        int len = readInt(b, p, 7);
        int s = p[0]; p[0] += len;
        if (!huff) return new String(b, s, len, java.nio.charset.Charset.forName("ISO-8859-1"));
        return huffDecode(b, s, len);
    }

    private static String huffDecode(byte[] b, int off, int len) {
        StringBuilder sb = new StringBuilder(len * 2);
        long cur = 0; int bits = 0;
        for (int i = 0; i < len; i++) {
            cur = (cur << 8) | (b[off + i] & 0xff); bits += 8;
            // greedily match a symbol whenever enough bits are buffered
            boolean progress = true;
            while (progress && bits >= 5) {
                progress = false;
                for (int sym = 0; sym < 256; sym++) {
                    int L = HL[sym];
                    if (L > bits) continue;
                    int code = (int) ((cur >>> (bits - L)) & ((1 << L) - 1));
                    if (code == HC[sym]) {
                        sb.append((char) sym);
                        bits -= L; progress = true; break;
                    }
                }
            }
        }
        return sb.toString();
    }
}
