/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PLY_ORIENTATION_H
#define PLY_ORIENTATION_H

#include <stdbool.h>
#include "ply-pixel-buffer.h"

typedef struct _ply_orientation ply_orientation_t;

ply_orientation_t *ply_orientation_new (const char *sysfs_root);
void ply_orientation_free (ply_orientation_t *sensor);
bool ply_orientation_read (ply_orientation_t            *sensor,
                           ply_pixel_buffer_rotation_t *rotation);

/* A separate calculation entry point allows testing without sensor hardware. */
bool ply_orientation_from_acceleration (const double                acceleration[3],
                                        const double                matrix[9],
                                        ply_pixel_buffer_rotation_t *rotation);
bool ply_orientation_parse_matrix (const char *text,
                                   double      matrix[9]);
ply_pixel_buffer_rotation_t ply_orientation_compose (ply_pixel_buffer_rotation_t panel,
                                                     ply_pixel_buffer_rotation_t orientation);
bool ply_orientation_update_console (const char                 *path,
                                     ply_pixel_buffer_rotation_t rotation);
bool ply_orientation_sync_console (const char                 *sysfs_root,
                                   ply_pixel_buffer_rotation_t rotation);

#endif
