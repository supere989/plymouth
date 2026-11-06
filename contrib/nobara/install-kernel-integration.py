#!/usr/bin/python3
"""Install the local Nobara integration and verify normal kernel image builds."""

import argparse
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile


def run(arguments, **options):
    return subprocess.run(arguments, check=True, **options)


def atomic_write(path, contents):
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    descriptor, temporary = tempfile.mkstemp(dir=path.parent, prefix=".sensor-rotation-")
    try:
        with os.fdopen(descriptor, "w") as stream:
            stream.write(contents)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-kernel", action="append", default=[])
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("Run this installer as root")

    os.environ["PATH"] = "/usr/sbin:/usr/bin:/sbin:/bin"
    os.environ.pop("LD_LIBRARY_PATH", None)
    source = Path(__file__).resolve().parent
    current = os.uname().release
    versions = list(dict.fromkeys([current] + args.verify_kernel))
    for version in versions:
        if not re.fullmatch(r"[A-Za-z0-9._+~-]+", version):
            parser.error(f"Invalid kernel version {version!r}")
        if not Path(f"/usr/lib/modules/{version}/vmlinuz").is_file():
            parser.error(f"Kernel {version} is not installed")
    if not Path("/opt/plymouth-sensor/sbin/plymouthd").is_file():
        parser.error("Install the tested /opt/plymouth-sensor build first")

    backup = Path("/var/lib/plymouth-sensor/kernel-integration-backup")
    backup.mkdir(parents=True, exist_ok=True, mode=0o700)

    def preserve(path):
        target = backup / str(path).lstrip("/").replace("/", "_")
        if path.exists() and not target.exists():
            shutil.copy2(path, target)

    files = {
        source / "91sensor-rotation/module-setup.sh": Path("/usr/lib/dracut/modules.d/91sensor-rotation/module-setup.sh"),
        source / "91sensor-rotation.conf": Path("/etc/dracut.conf.d/91sensor-rotation.conf"),
        source / "plymouth-start.conf": Path("/etc/systemd/system/plymouth-start.service.d/90-sensor-rotation.conf"),
        source / "plymouth-quit.conf": Path("/etc/systemd/system/plymouth-quit.service.d/90-sensor-rotation.conf"),
    }
    for original, installed in files.items():
        installed.parent.mkdir(parents=True, exist_ok=True)
        preserve(installed)
        atomic_write(installed, original.read_text())
        os.chmod(installed, 0o755 if installed.name == "module-setup.sh" else 0o644)

    # Retire the earlier test-only host override after installing its replacement.
    old_override = Path("/etc/systemd/system/plymouth-quit.service.d/90-sensor-test.conf")
    preserve(old_override)
    old_override.unlink(missing_ok=True)

    cmdline = Path("/etc/kernel/cmdline")
    preserve(cmdline)
    options = cmdline.read_text().strip()
    if "plymouth.autorotate" not in options.split():
        atomic_write(cmdline, options + " plymouth.autorotate\n")
    run(["systemctl", "daemon-reload"])

    machine_id = Path("/etc/machine-id").read_text().strip()
    for version in versions:
        image = Path(f"/boot/initramfs-{version}.img")
        preserve(image)
        entry = Path(f"/boot/loader/entries/{machine_id}-{version}.conf")
        preserve(entry)
        log = Path(f"/var/log/plymouth-sensor-kernel-{version}.log")
        environment = dict(os.environ)
        environment.update({
            "KERNEL_INSTALL_MACHINE_ID": machine_id,
            "KERNEL_INSTALL_LAYOUT": "other",
            "KERNEL_INSTALL_INITRD_GENERATOR": "dracut",
            "KERNEL_INSTALL_VERBOSE": "0",
        })
        print(f"Building normal boot image through 50-dracut.install for {version}", flush=True)
        with log.open("w") as output:
            result = subprocess.run([
                "/usr/lib/kernel/install.d/50-dracut.install", "add", version,
                f"/boot/{machine_id}/{version}", f"/usr/lib/modules/{version}/vmlinuz",
            ], env=environment, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            print("\n".join(log.read_text().splitlines()[-35:]), flush=True)
            raise SystemExit(f"Image build failed. See {log}")

        listing = run(["lsinitrd", str(image)], capture_output=True, text=True).stdout
        required = [
            "sensor-rotation", "opt/plymouth-sensor/sbin/plymouthd",
            "opt/plymouth-sensor/lib64/plymouth/renderers/drm.so",
            f"usr/lib/modules/{version}/kernel/drivers/hid/amd-sfh-hid/amd_sfh.ko",
            f"usr/lib/modules/{version}/kernel/drivers/iio/accel/hid-sensor-accel-3d.ko",
            "etc/systemd/system/plymouth-start.service.d/90-sensor-rotation.conf",
            "etc/systemd/system/plymouth-quit.service.d/90-sensor-rotation.conf",
            "etc/modprobe.d/amd-sfh-envy-15m.conf", "usr/share/X11/xkb/rules/evdev",
        ]
        missing = [item for item in required if item not in listing]
        if missing:
            raise SystemExit(f"Image {image} is missing {missing}")
        run(["grubby", f"--update-kernel=/boot/vmlinuz-{version}", "--args=plymouth.autorotate"])
        print(f"Verified image, sensor drivers, runtime and service configuration for {version}", flush=True)

    settings = run(["grub2-get-kernel-settings"], capture_output=True, text=True).stdout
    if "GRUB_UPDATE_DEFAULT_KERNEL=true" not in settings:
        raise SystemExit("Nobara's automatic kernel default policy is disabled. Boot default was not changed.")
    run(["grub2-set-default", f"{machine_id}-{current}"])
    print("Normal current-kernel entry selected. Future kernels use Nobara's existing update policy.", flush=True)
    run(["grub2-editenv", "list"])


if __name__ == "__main__":
    main()
