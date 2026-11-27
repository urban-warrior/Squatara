import subprocess

Import("env")


def get_git_version():
    try:
        # Gets the closest tag, or falls back to short commit hash if no tags exist
        # Adds '-dirty' if there are uncommitted changes
        version = (
            subprocess.check_output(  # for only tag, not hash if no tag, remove --always
                ["git", "describe", "--tags", "--always", "--dirty"],
                stderr=subprocess.DEVNULL,
            )
            .decode("utf-8")
            .strip()
        )
        return version
    except Exception:
        return "unknown"


git_tag = get_git_version()
print(f"Building with GIT_TAG: {git_tag}")

# Inject as a string macro: -DGIT_TAG="\"v1.0.0\""
env.Append(CPPDEFINES=[("GIT_TAG", env.StringifyMacro(git_tag))])
