"""Download a checksum-pinned LLVM static SDK; never compile LLVM in host CI."""
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import zipfile


def main():
    config = json.loads(Path(__file__).with_name("sdk.json").read_text(encoding="utf-8"))
    system = {"Windows": "windows", "macOS": "macos"}[os.environ["RUNNER_OS"]]
    if system == "windows":
        arch = {"Win32": "x86", "x86": "x86", "x64": "x64", "ARM64": "arm64", "arm64": "arm64"}[os.environ["SDK_TARGET_ARCH"]]
    else:
        arch = {"arm64": "arm64", "aarch64": "arm64", "x86_64": "x64"}[platform.machine().lower()]
    asset = config["assets"][f"{system}-{arch}"]
    root = Path(os.environ["RUNNER_TEMP"]) / "iris-llvm-sdk"
    root.mkdir(parents=True, exist_ok=True)
    archive = root / asset["name"]
    url = f"https://github.com/{config['repository']}/releases/download/{config['tag']}/{asset['name']}"
    subprocess.run(["curl", "--fail", "--location", "--show-error", "--connect-timeout", "30",
                    "--max-time", "600", "--retry", "2", "--output", str(archive), url], check=True)
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != asset["sha256"]:
        raise RuntimeError("LLVM SDK SHA-256 mismatch")
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as package:
            package.extractall(root)
        name = archive.name.removesuffix(".zip")
    else:
        with tarfile.open(archive, "r:xz") as package:
            package.extractall(root, filter="data")
        name = archive.name.removesuffix(".tar.xz")
    sdk = root / name
    manifest = json.loads((sdk / "manifest.json").read_text(encoding="utf-8"))
    if manifest["platform"] != system or manifest["architecture"] != arch:
        raise RuntimeError("LLVM SDK architecture mismatch")
    if not (sdk / "lib/cmake/llvm/LLVMConfig.cmake").is_file():
        raise RuntimeError("LLVM SDK CMake configuration missing")
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as output:
        output.write(f"LLVM_DIR={sdk.as_posix()}/lib/cmake/llvm\n")
        if system == "macos":
            output.write(f"MACOSX_DEPLOYMENT_TARGET={manifest['macos_deployment_target']}\n")
    print(f"Verified static LLVM SDK: {name}", flush=True)


if __name__ == "__main__":
    main()
