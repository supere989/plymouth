#!/usr/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Include the tested, separately installed Plymouth build in normal boot images.

check() {
    [[ -n ${IN_KDUMP:-} ]] && return 1
    [[ -n ${mount_needs:-} ]] && return 1
    [[ -x /opt/plymouth-sensor/sbin/plymouthd ]] || return 1
    [[ -x /opt/plymouth-sensor/bin/plymouth ]] || return 1
    return 255
}

depends() {
    echo plymouth
    return 0
}

install() {
    local prefix=/opt/plymouth-sensor
    local file

    inst_multiple "$prefix/sbin/plymouthd" "$prefix/bin/plymouth" || return 1

    # inst_multiple resolves shared-library dependencies for each plugin too.
    for file in "$prefix"/lib64/libply*.so* \
                "$prefix"/lib64/plymouth/*.so \
                "$prefix"/lib64/plymouth/renderers/*.so; do
        [[ -f "$file" ]] || continue
        inst_multiple "$file" || return 1
    done

    # The theme paths are compiled into this build. Carry their data and XKB
    # layouts into the image without depending on a user's source checkout.
    while IFS= read -r -d '' file; do
        inst_simple "$file" || return 1
    done < <(find "$prefix/share/plymouth" "$prefix/etc/plymouth" \
                 /usr/share/X11/xkb \( -type f -o -type l \) -print0)

    inst_simple /etc/modprobe.d/amd-sfh-envy-15m.conf || return 1
    inst_simple /etc/systemd/system/plymouth-start.service.d/90-sensor-rotation.conf || return 1
    inst_simple /etc/systemd/system/plymouth-quit.service.d/90-sensor-rotation.conf || return 1
}
