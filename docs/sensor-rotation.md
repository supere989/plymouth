# Sensor-based splash rotation

Add `plymouth.autorotate` to the kernel command line to enable automatic
orientation of the built-in display in the DRM renderer. The default remains
the panel's existing orientation.

The sensor driver, its dependencies, and any udev mount-matrix rules must be
available in the initramfs for rotation during the earliest splash. The feature
reads IIO accelerometer attributes directly and does not require a system D-Bus
or a running desktop. Polled `in_accel_{x,y,z}_raw` and processed
`in_accel_{x,y,z}_input` channels are supported. Buffer-only sensors are not yet
supported.

The implementation applies channel offsets and scales, then the
`ACCEL_MOUNT_MATRIX` udev property or a standard IIO mount-matrix attribute.
When neither is provided, it assumes the standard IIO axes. Sensors labeled as
belonging to the laptop base are skipped. On machines with several unlabeled
accelerometers, the first readable accelerometer is used.

Three consecutive samples at 5 Hz must agree before rotation changes. Flat,
diagonal, unavailable, and invalid readings retain the current orientation.
Sensors that appear late are discovered on subsequent polls. External displays
keep their existing orientation. A built-in panel sharing a scan-out buffer
with an external display also retains its fixed orientation.

Rotation uses the existing software pixel-buffer transforms. The DRM scan-out
mode stays at the panel's native dimensions. Display removal and addition
notifications rebuild theme views and restore the daemon's current interaction
state, including password prompts. Sampling is suspended while the device
manager is paused or its renderers are inactive.

Build and run the regression tests with:

```sh
meson setup build -Dgtk=disabled -Ddocs=false
meson compile -C build
meson test -C build --print-errorlogs
```

Hardware validation must include both directions of portrait, landscape,
inverted landscape, a flat device, an active password prompt, and the handoff
to the login compositor. Unit tests cannot verify a machine's physical sensor
mounting or its DRM driver's behavior.
