package adapter.core;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Properties;

/**
 * Owner-only files, replaced atomically: the account store of the auth provider (microG) and its
 * account registry. A symlink in the path is refused. From Codex's BootstrapTransfer.
 */
final class PrivateFiles {
    private PrivateFiles() {}

    static void writePrivate(Path file, Properties p) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        p.store(out, null);
        writePrivateBytes(file, out.toByteArray());
    }

    static void writePrivateBytes(Path file, byte[] bytes) throws IOException {
        Path parent = file.toAbsolutePath().getParent();
        Files.createDirectories(parent);
        if (Files.isSymbolicLink(parent) || Files.isSymbolicLink(file)) {
            throw new IOException("symlink in a private file's path");
        }
        Files.setPosixFilePermissions(parent, PosixFilePermissions.fromString("rwx------"));
        Path tmp = Files.createTempFile(parent, ".private-", ".tmp",
                PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString("rw-------")));
        try {
            try (OutputStream out = Files.newOutputStream(tmp)) { out.write(bytes); }
            // Require atomic replacement. Never leave a half-written account credential behind.
            Files.move(tmp, file, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(tmp);
        }
    }
}
