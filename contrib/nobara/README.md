# Carry the tested build through Nobara kernel updates

These files integrate the already installed `/opt/plymouth-sensor` build with
Nobara's normal dracut and systemd configuration. The module copies its runtime
binaries, plugin dependencies, themes, XKB data, sensor override, and service
drop-ins into each generated boot image. The sensor drivers are loaded early.

Install the module as
`/usr/lib/dracut/modules.d/91sensor-rotation/module-setup.sh` with mode 0755.
Install `91sensor-rotation.conf` in `/etc/dracut.conf.d/`. Install the start and
quit drop-ins as `90-sensor-rotation.conf` beneath the corresponding service
directories in `/etc/systemd/system/`.

Add `plymouth.autorotate` to `/etc/kernel/cmdline`, preserving existing options.
Nobara's kernel package post-transaction script invokes `kernel-install add`.
Its existing `50-dracut.install` plugin builds the new kernel's boot image and
automatically includes this module. Nobara's existing boot-entry plugins then
select the new normal kernel entry according to the distribution's policy.
No running-kernel version is embedded in this integration.

The installer automates deployment, preserves the old configuration and normal
images under `/var/lib/plymouth-sensor/kernel-integration-backup`, and validates
normal image generation through Nobara's installed `50-dracut.install` hook:

```sh
sudo python3 contrib/nobara/install-kernel-integration.py
```

Add `--verify-kernel VERSION` to exercise another installed kernel's image
generation as well. The installer selects the normal current-kernel BLS entry
only after the requested image checks succeed. Kdump image generation excludes
the module.

After installation, rebuild the normal boot image for the current kernel and
add the option to its existing boot entries. Validate the image's contents and
make its normal BLS entry the saved default. Preserve the previously tested
sensor image as a recovery entry until the new normal image has been booted.

The installed `/opt/plymouth-sensor` tree must remain available after
switch-root as well as during image generation. Kernel updates carry that
tested build forward. Updating or rebasing Plymouth itself is a separate
packaging task.

To disable automatic inclusion, remove `/etc/dracut.conf.d/91sensor-rotation.conf`
and the two service drop-ins, remove the kernel option, and regenerate the
normal boot images with the distribution's Plymouth package.
