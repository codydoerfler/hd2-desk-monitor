# ---------------------------------------------------------------------------
#  fw_version.py — stamps the build with a firmware version string.
#
#  Wired in from platformio.ini as `extra_scripts = pre:tools/fw_version.py`.
#  It appends one macro to the build:
#
#      -D HD2_FW_VERSION="v1.2.3"
#
#  The value comes from `git describe --tags` rather than a constant in
#  platformio.ini, and that is the whole point of this file: OTA compares the
#  running firmware's version against the tag on the latest GitHub release. If
#  a human had to remember to bump a literal before tagging, the day they
#  forget is the day every device downloads v1.2.3, boots, still calls itself
#  v1.2.2, and downloads it again — forever. Deriving from the tag makes that
#  disagreement impossible to introduce.
#
#  CI must therefore check out with tags available (`fetch-depth: 0`), or every
#  release ships stamped as FALLBACK. See .github/workflows/release.yml.
# ---------------------------------------------------------------------------
import subprocess

Import("env")  # noqa: F821 - injected by PlatformIO/SCons

# Used when there is no git metadata to read: a source zip, or a checkout with
# no tags yet. 0.0.0 sorts below every real release, so such a build will
# accept the first release it sees over OTA. That is the honest outcome — an
# unversioned build has no claim to being newer than anything.
FALLBACK = "0.0.0"


def firmware_version():
    try:
        out = subprocess.check_output(
            # --dirty so an uncommitted local build is visibly not the tag it
            # sits on; --always so a repo with no tags still yields something
            # rather than raising.
            ["git", "describe", "--tags", "--dirty", "--always"],
            cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
            stderr=subprocess.DEVNULL,
        )
        version = out.decode("utf-8", "replace").strip()
        return version or FALLBACK
    except (OSError, subprocess.CalledProcessError):
        # No git binary, or not a repository. Not worth failing the build over.
        return FALLBACK


VERSION = firmware_version()
print("Firmware version: %s" % VERSION)

env.Append(CPPDEFINES=[("HD2_FW_VERSION", env.StringifyMacro(VERSION))])  # noqa: F821
