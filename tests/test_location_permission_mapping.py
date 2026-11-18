"""Fine and approximate location must round-trip separately in both permission mappers."""

from pathlib import Path
import subprocess
import tempfile
import unittest


PACKAGE_MANAGER = Path(__file__).resolve().parents[1] / "framework/package-manager"
PERMISSIONS = {
    "android.permission.ACCESS_FINE_LOCATION": "ohos.permission.LOCATION",
    "android.permission.ACCESS_COARSE_LOCATION": "ohos.permission.APPROXIMATELY_LOCATION",
    "android.permission.ACCESS_BACKGROUND_LOCATION": "ohos.permission.LOCATION_IN_BACKGROUND",
}


class LocationPermissionMappingTest(unittest.TestCase):
    def test_runtime_and_install_mappers(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            java = out / "LocationMapping.java"
            java.write_text("""
import adapter.packagemanager.PermissionMapper;
class LocationMapping {
    public static void main(String[] args) {
        System.out.println(PermissionMapper.mapToOH(args[0]));
        System.out.println(PermissionMapper.mapToAndroid(args[1]));
    }
}
""")
            subprocess.run(["javac", "-d", out, PACKAGE_MANAGER / "java/PermissionMapper.java", java], check=True)
            cpp = out / "location_mapping.cpp"
            cpp.write_text("""
#include <iostream>
#include "permission_mapper.h"
int main(int argc, char **argv) {
    std::cout << oh_adapter::PermissionMapper::MapToOH(argv[1]) << '\\n'
              << oh_adapter::PermissionMapper::MapToAndroid(argv[2]) << '\\n';
}
""")
            native = out / "location_mapping"
            jni = PACKAGE_MANAGER / "jni"
            subprocess.run(["c++", "-std=c++17", "-I", jni, cpp, jni / "permission_mapper.cpp", "-o", native], check=True)
            for command in (["java", "-cp", out, "LocationMapping"], [native]):
                for android, oh in PERMISSIONS.items():
                    with self.subTest(mapper=command[0], permission=android):
                        actual = subprocess.check_output([*command, android, oh], text=True)
                        self.assertEqual(actual.splitlines(), [oh, android])


if __name__ == "__main__":
    unittest.main()
