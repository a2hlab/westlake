"""tools/make_device_profile.py: a device's build.prop from the facts a person gives, nothing guessed."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[2] / "tools" / "make_device_profile.py"
spec = importlib.util.spec_from_file_location("make_device_profile", TOOL)
mdp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mdp)

FACTS = {
    "brand": "Acme", "manufacturer": "Acme", "model": "Board One", "device": "board1", "name": "board1_x",
    "board": "b1", "platform": "plat9", "hardware": "hw9", "soc_manufacturer": "Chips", "soc_model": "C900",
    "android_release": "15", "android_sdk": "35", "build_id": "AP3A.240905.015.A2", "build_incremental": "42",
    "build_type": "user", "build_tags": "release-keys", "build_date_utc": "1725537600", "security_patch": "2024-09-05",
}


class MakeDeviceProfileTest(unittest.TestCase):
    def test_the_fingerprint_and_description_follow_aosp(self):
        props = dict(mdp.build_prop(FACTS))
        self.assertEqual(props["ro.build.fingerprint"], "Acme/board1_x/board1:15/AP3A.240905.015.A2/42:user/release-keys")
        self.assertEqual(props["ro.build.description"], "board1_x-user 15 AP3A.240905.015.A2 42 release-keys")
        self.assertEqual(props["ro.build.display.id"], "AP3A.240905.015.A2", "a user release-keys build shows its id")
        debug = dict(mdp.build_prop(dict(FACTS, build_type="userdebug", build_tags="test-keys")))
        self.assertEqual(debug["ro.build.display.id"], debug["ro.build.description"])
        self.assertEqual((debug["ro.debuggable"], debug["ro.secure"]), ("1", "1"))
        self.assertEqual((props["ro.debuggable"], props["ro.secure"]), ("0", "1"))

    def test_every_partition_repeats_the_identity(self):
        props = dict(mdp.build_prop(FACTS))
        for part in mdp.PARTITIONS:
            self.assertEqual(props["ro.%s.build.fingerprint" % part], props["ro.build.fingerprint"])
            self.assertEqual(props["ro.%s.build.date.utc" % part], "1725537600")
            self.assertEqual(props["ro.product.%s.device" % part], "board1")
        only = dict(mdp.build_prop(dict(FACTS, partitions="system vendor")))
        self.assertIn("ro.vendor.build.fingerprint", only)
        self.assertNotIn("ro.odm.build.fingerprint", only)

    def test_an_optional_fact_not_given_is_not_written(self):
        props = dict(mdp.build_prop(FACTS))
        for prop in mdp.OPTIONAL.values():
            self.assertNotIn(prop, props)
        given = dict(mdp.build_prop(dict(FACTS, verified_boot_state="orange", bootloader="b1-1.0")))
        self.assertEqual((given["ro.boot.verifiedbootstate"], given["ro.bootloader"]), ("orange", "b1-1.0"))

    def test_a_missing_or_malformed_fact_is_refused(self):
        facts = dict(FACTS)
        del facts["soc_model"]
        facts["build_type"] = "release"
        found = mdp.problems(facts)
        self.assertIn("missing soc_model (ro.soc.model)", found)
        self.assertIn("build_type=release is not a valid value", found)
        self.assertIn("unknown fact colour", mdp.problems(dict(FACTS, colour="red")))
        self.assertEqual(mdp.problems(FACTS), [])

    def test_a_comment_after_a_value_is_not_part_of_it(self):
        facts = mdp.read_key_info("model=Board One   # the name it is sold under\nboard=         # unknown\n")
        self.assertEqual(facts, {"model": "Board One", "board": ""})

    def test_the_tool_writes_a_build_prop_or_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            info, out = Path(d) / "dev.keyinfo", Path(d) / "build.prop"
            info.write_text("# facts\n" + "".join("%s=%s\n" % kv for kv in FACTS.items()))
            self.assertEqual(mdp.main([str(info), str(out)]), 0)
            lines = [l for l in out.read_text().splitlines() if not l.startswith("#")]
            self.assertIn("ro.product.model=Board One", lines)
            info.write_text("brand=Acme\n")
            out.unlink()
            self.assertEqual(mdp.main([str(info), str(out)]), 2)
            self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
