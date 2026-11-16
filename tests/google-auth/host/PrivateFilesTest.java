package adapter.core;

import java.io.FileInputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Properties;

/** PrivateFiles: owner-only, replaced whole, never through a symlink. */
public final class PrivateFilesTest {
    private static int passed;

    private static void check(boolean condition, String what) {
        if (!condition) throw new AssertionError(what);
        passed++;
    }

    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("private-files-test");
        Path dir = root.resolve("store");
        Path file = dir.resolve("accounts.properties");
        Properties p = new Properties();
        p.setProperty("name", "first");
        PrivateFiles.writePrivate(file, p);
        check("rw-------".equals(PosixFilePermissions.toString(Files.getPosixFilePermissions(file))), "file mode");
        check("rwx------".equals(PosixFilePermissions.toString(Files.getPosixFilePermissions(dir))), "directory mode");
        p.setProperty("name", "second");
        PrivateFiles.writePrivate(file, p);
        Properties read = new Properties();
        try (FileInputStream in = new FileInputStream(file.toFile())) { read.load(in); }
        check("second".equals(read.getProperty("name")), "replaced content");
        try (var left = Files.list(dir)) { check(left.count() == 1, "no temporary file left"); }
        Path link = dir.resolve("link.properties");
        Files.createSymbolicLink(link, root.resolve("elsewhere"));
        try { PrivateFiles.writePrivate(link, p); throw new AssertionError("wrote through a symlink"); }
        catch (IOException expected) { passed++; }
        check(!Files.exists(root.resolve("elsewhere")), "symlink target untouched");
        System.out.println("PRIVATE_FILES_PASS " + passed);
    }
}
