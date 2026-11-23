#!/usr/bin/env python3
"""make_device_profile.py <key-info> <build.prop> -- the Android identity Westlake presents for one device, as the
build.prop an Android build would carry, made from the facts a person provides about that device.

Apps and Google Play services read a device's identity through Build and SystemProperties, and Play services'
check-in reads it per partition too (ro.<partition>.build.fingerprint, ro.<partition>.build.date.utc,
ro.product.<partition>.device). On an Android device all of these come from one build and agree. Westlake runs on
another OS, so it presents them itself: its runtime and its Bionic shim answer identity properties from
$ANDROID_ROOT/build.prop, and an identity property the file does not list stays unset (never the host OS's value,
never a built-in default).

This tool guesses nothing. The key-info file states facts (key=value, # comments): the required ones below, which
the tool refuses to run without, and optional ones, which it writes only when given (a comment may follow a value
after whitespace). Every other property comes
from a fact by the rule AOSP's build system uses for it (build/make/core/sysprop.mk, Makefile), named beside each
rule below. Prints the properties written; exits 2 naming each missing or malformed fact.
"""
from __future__ import annotations

import re
import sys
import time

#: Facts the identity cannot be made without: each is an Android property, or the input to one.
REQUIRED = {
    "brand": "ro.product.brand",
    "manufacturer": "ro.product.manufacturer",
    "model": "ro.product.model",
    "device": "ro.product.device (the device's code name)",
    "name": "ro.product.name (the product's code name)",
    "board": "ro.product.board",
    "platform": "ro.board.platform",
    "hardware": "ro.hardware and ro.boot.hardware",
    "soc_manufacturer": "ro.soc.manufacturer",
    "soc_model": "ro.soc.model",
    "android_release": "ro.build.version.release, e.g. 15",
    "android_sdk": "ro.build.version.sdk, e.g. 35",
    "build_id": "ro.build.id, e.g. AP3A.240905.015.A2",
    "build_incremental": "ro.build.version.incremental",
    "build_type": "ro.build.type: user, userdebug or eng",
    "build_tags": "ro.build.tags: release-keys, test-keys or dev-keys",
    "build_date_utc": "ro.build.date.utc: the build's time, seconds since 1970",
    "security_patch": "ro.build.version.security_patch: YYYY-MM-DD",
}
#: Facts written only when given: property name for each.
OPTIONAL = {
    "first_api_level": "ro.product.first_api_level",
    "build_user": "ro.build.user",
    "build_host": "ro.build.host",
    "characteristics": "ro.build.characteristics",
    "bootloader": "ro.bootloader",
    "baseband": "gsm.version.baseband",
    "hardware_revision": "ro.revision",
    "hardware_sku": "ro.boot.hardware.sku",
    "verified_boot_state": "ro.boot.verifiedbootstate",
    "flash_locked": "ro.boot.flash.locked",
    "locale": "ro.product.locale",
}
#: The partitions whose build properties a device lists (each has its own build.prop on Android). A key-info file may
#: name others with partitions=..., space-separated.
PARTITIONS = ("system", "system_ext", "vendor", "odm", "product", "bootimage")

_CHECKS = {
    "android_release": re.compile(r"^\d+(\.\d+)*$"),
    "android_sdk": re.compile(r"^\d+$"),
    "first_api_level": re.compile(r"^\d+$"),
    "build_type": re.compile(r"^(user|userdebug|eng)$"),
    "build_tags": re.compile(r"^[\w,.-]+-keys$"),
    "build_date_utc": re.compile(r"^\d+$"),
    "security_patch": re.compile(r"^\d{4}-\d{2}-\d{2}$"),
    "verified_boot_state": re.compile(r"^(green|yellow|orange|red)$"),
    "flash_locked": re.compile(r"^[01]$"),
    "partitions": re.compile(r"^[a-z_]+( [a-z_]+)*$"),
}
_COMMENT = re.compile(r"(^|\s+)#.*$")
#: Characters a property value cannot hold here (build.prop is line-based).
_BAD = re.compile(r"[\r\n=]")


def read_key_info(text: str) -> dict[str, str]:
    facts: dict[str, str] = {}
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        if not sep:
            raise ValueError("line %d: not key=value: %s" % (number, line))
        facts[key.strip()] = _COMMENT.sub("", value).strip()   # a comment after the value: whitespace, then #
    return facts


def problems(facts: dict[str, str]) -> list[str]:
    found = []
    for key, what in REQUIRED.items():
        if not facts.get(key):
            found.append("missing %s (%s)" % (key, what))
    known = set(REQUIRED) | set(OPTIONAL) | {"partitions"}
    for key, value in facts.items():
        if key not in known:
            found.append("unknown fact %s" % key)
        elif value and key in _CHECKS and not _CHECKS[key].match(value):
            found.append("%s=%s is not a valid value" % (key, value))
        elif _BAD.search(value):
            found.append("%s has a character a property value cannot hold" % key)
    return found


def build_prop(facts: dict[str, str]) -> list[tuple[str, str]]:
    """The properties, in build.prop order. Each derived value names the AOSP rule it follows."""
    f = facts
    out: list[tuple[str, str]] = []
    add = out.append
    # build/make/core/sysprop.mk: BUILD_FINGERPRINT := $(PRODUCT_BRAND)/$(TARGET_PRODUCT)/$(TARGET_DEVICE):
    #   $(PLATFORM_VERSION)/$(BUILD_ID)/$(BUILD_NUMBER_FROM_FILE):$(TARGET_BUILD_VARIANT)/$(BUILD_VERSION_TAGS)
    fingerprint = "%s/%s/%s:%s/%s/%s:%s/%s" % (f["brand"], f["name"], f["device"], f["android_release"], f["build_id"],
                                              f["build_incremental"], f["build_type"], f["build_tags"])
    # build/make/core/Makefile: BUILD_DESC := $(TARGET_PRODUCT)-$(TARGET_BUILD_VARIANT) $(PLATFORM_VERSION) $(BUILD_ID)
    #   $(BUILD_NUMBER_FROM_FILE) $(BUILD_VERSION_TAGS)
    description = "%s-%s %s %s %s %s" % (f["name"], f["build_type"], f["android_release"], f["build_id"],
                                         f["build_incremental"], f["build_tags"])
    # sysprop.mk: BUILD_DISPLAY_ID is BUILD_ID for a user build with release-keys, else BUILD_DESC.
    display_id = f["build_id"] if (f["build_type"], f["build_tags"]) == ("user", "release-keys") else description
    date = time.strftime("%a %b %e %H:%M:%S UTC %Y", time.gmtime(int(f["build_date_utc"])))
    add(("ro.build.id", f["build_id"]))
    add(("ro.build.display.id", display_id))
    add(("ro.build.version.incremental", f["build_incremental"]))
    add(("ro.build.version.sdk", f["android_sdk"]))
    add(("ro.build.version.release", f["android_release"]))
    # sysprop.mk: a released (REL) platform's codename, its release-or-codename and its display version.
    add(("ro.build.version.release_or_codename", f["android_release"]))
    add(("ro.build.version.release_or_preview_display", f["android_release"]))
    add(("ro.build.version.codename", "REL"))
    add(("ro.build.version.all_codenames", "REL"))
    add(("ro.build.version.preview_sdk", "0"))
    add(("ro.build.version.preview_sdk_fingerprint", "REL"))   # sysprop.mk: REL when not a preview
    add(("ro.build.version.security_patch", f["security_patch"]))
    add(("ro.build.date", date))
    add(("ro.build.date.utc", f["build_date_utc"]))
    add(("ro.build.type", f["build_type"]))
    add(("ro.build.tags", f["build_tags"]))
    add(("ro.build.flavor", "%s-%s" % (f["name"], f["build_type"])))
    add(("ro.build.description", description))
    add(("ro.build.fingerprint", fingerprint))
    add(("ro.build.product", f["device"]))       # sysprop.mk: ro.build.product := TARGET_DEVICE
    for key in ("build_user", "build_host"):
        if f.get(key):
            add((OPTIONAL[key], f[key]))
    add(("ro.product.brand", f["brand"]))
    add(("ro.product.manufacturer", f["manufacturer"]))
    add(("ro.product.model", f["model"]))
    add(("ro.product.device", f["device"]))
    add(("ro.product.name", f["name"]))
    add(("ro.product.board", f["board"]))
    add(("ro.board.platform", f["platform"]))
    add(("ro.hardware", f["hardware"]))
    add(("ro.boot.hardware", f["hardware"]))
    add(("ro.soc.manufacturer", f["soc_manufacturer"]))
    add(("ro.soc.model", f["soc_model"]))
    # build/make/core/Makefile: each partition's build.prop repeats the build's identity under its own name
    # (ro.<partition>.build.* and ro.product.<partition>.*), from the same build.
    partitions = f.get("partitions", "").split() or list(PARTITIONS)
    for part in partitions:
        add(("ro.%s.build.fingerprint" % part, fingerprint))
        add(("ro.%s.build.id" % part, f["build_id"]))
        add(("ro.%s.build.date.utc" % part, f["build_date_utc"]))
        add(("ro.%s.build.version.incremental" % part, f["build_incremental"]))
        add(("ro.%s.build.version.release" % part, f["android_release"]))
        add(("ro.%s.build.version.sdk" % part, f["android_sdk"]))
        add(("ro.%s.build.type" % part, f["build_type"]))
        add(("ro.%s.build.tags" % part, f["build_tags"]))
        for field in ("brand", "device", "manufacturer", "model", "name"):
            add(("ro.product.%s.%s" % (part, field), f[field]))
    # build/make/core/main.mk: ro.secure is 0 only on eng builds, ro.debuggable is 1 on eng and userdebug.
    add(("ro.secure", "0" if f["build_type"] == "eng" else "1"))
    add(("ro.debuggable", "1" if f["build_type"] in ("eng", "userdebug") else "0"))
    for key in ("first_api_level", "characteristics", "bootloader", "baseband", "hardware_revision", "hardware_sku",
                "verified_boot_state", "flash_locked", "locale"):
        if f.get(key):
            add((OPTIONAL[key], f[key]))
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 64
    try:
        facts = read_key_info(open(argv[0]).read())
    except ValueError as e:
        print("key info: %s" % e, file=sys.stderr)
        return 2
    found = problems(facts)
    if found:
        for p in found:
            print("key info: %s" % p, file=sys.stderr)
        return 2
    props = build_prop(facts)
    with open(argv[1], "w") as out:
        out.write("# Westlake device profile, made by tools/make_device_profile.py from %s.\n" % argv[0])
        out.write("# Every value is a fact from that file or follows from one by an AOSP build rule. Do not edit;\n")
        out.write("# change the key info and make it again.\n")
        for key, value in props:
            out.write("%s=%s\n" % (key, value))
    print("%d properties for %s %s (Android %s, API %s)" % (len(props), facts["manufacturer"], facts["model"],
                                                            facts["android_release"], facts["android_sdk"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
