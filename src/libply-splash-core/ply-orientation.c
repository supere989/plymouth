/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ply-orientation.h"

#include <ctype.h>
#include <glob.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UDEV
#include <libudev.h>
#endif

#include "ply-logger.h"

struct _ply_orientation
{
        char *sysfs_root;
        char *device_path;
        double matrix[9];
        ply_pixel_buffer_rotation_t candidate;
        unsigned int stable_samples;
};

static const double identity_matrix[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

bool
ply_orientation_parse_matrix (const char *text,
                              double      matrix[9])
{
        double parsed[9];
        char *end;
        int i, row, other, column;

        for (i = 0; i < 9; i++) {
                parsed[i] = strtod (text, &end);
                if (end == text || !isfinite (parsed[i]))
                        return false;
                text = end;
                while (isspace ((unsigned char) *text))
                        text++;
                if (i < 8) {
                        if (*text != (i % 3 == 2 ? ';' : ','))
                                return false;
                        text++;
                }
        }
        if (*text != '\0')
                return false;

        /* Mount matrices must preserve gravity's magnitude and axis angles. */
        for (row = 0; row < 3; row++) {
                for (other = row; other < 3; other++) {
                        double dot = 0;
                        for (column = 0; column < 3; column++)
                                dot += parsed[row * 3 + column] * parsed[other * 3 + column];
                        if (fabs (dot - (row == other ? 1.0 : 0.0)) > 0.05)
                                return false;
                }
        }
        memcpy (matrix, parsed, sizeof(parsed));
        return true;
}

bool
ply_orientation_from_acceleration (const double                acceleration[3],
                                   const double                matrix[9],
                                   ply_pixel_buffer_rotation_t *rotation)
{
        double gravity[3] = { 0 };
        double magnitude;
        int row, column;

        for (row = 0; row < 3; row++) {
                for (column = 0; column < 3; column++)
                        gravity[row] += matrix[row * 3 + column] * acceleration[column];
        }
        magnitude = hypot (hypot (gravity[0], gravity[1]), gravity[2]);
        if (!isfinite (magnitude) || magnitude < 0.000001)
                return false;

        /* Keep the previous orientation when flat or close to a diagonal. */
        if (fabs (gravity[0]) > 0.75 * magnitude) {
                *rotation = gravity[0] > 0 ? PLY_PIXEL_BUFFER_ROTATE_COUNTER_CLOCKWISE :
                                            PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE;
                return true;
        }
        if (fabs (gravity[1]) > 0.75 * magnitude) {
                *rotation = gravity[1] < 0 ? PLY_PIXEL_BUFFER_ROTATE_UPRIGHT :
                                            PLY_PIXEL_BUFFER_ROTATE_UPSIDE_DOWN;
                return true;
        }
        return false;
}

ply_pixel_buffer_rotation_t
ply_orientation_compose (ply_pixel_buffer_rotation_t panel,
                          ply_pixel_buffer_rotation_t orientation)
{
        static const int angles[] = { 0, 2, 1, 3 };
        static const ply_pixel_buffer_rotation_t rotations[] = {
                PLY_PIXEL_BUFFER_ROTATE_UPRIGHT,
                PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE,
                PLY_PIXEL_BUFFER_ROTATE_UPSIDE_DOWN,
                PLY_PIXEL_BUFFER_ROTATE_COUNTER_CLOCKWISE
        };

        return rotations[(angles[panel] + angles[orientation]) % 4];
}

static bool
read_attribute (const char *device_path,
                const char *name,
                char        text[256])
{
        char *path;
        FILE *file;
        bool success;

        if (asprintf (&path, "%s/%s", device_path, name) < 0)
                return false;
        file = fopen (path, "re");
        free (path);
        if (file == NULL)
                return false;
        success = fgets (text, 256, file) != NULL;
        fclose (file);
        if (success)
                text[strcspn (text, "\n")] = '\0';
        return success;
}

static bool
read_number (const char *device_path,
             const char *name,
             double     *number)
{
        char text[256], *end;
        double value;

        if (!read_attribute (device_path, name, text))
                return false;
        value = strtod (text, &end);
        if (end == text || !isfinite (value))
                return false;
        while (isspace ((unsigned char) *end))
                end++;
        if (*end != '\0')
                return false;
        *number = value;
        return true;
}

static bool
read_acceleration (const char *device_path,
                   double      acceleration[3])
{
        int axis;

        for (axis = 0; axis < 3; axis++) {
                char name[64];
                double raw, offset = 0, scale = 1;

                snprintf (name, sizeof(name), "in_accel_%c_input", "xyz"[axis]);
                if (read_number (device_path, name, &acceleration[axis]))
                        continue;

                snprintf (name, sizeof(name), "in_accel_%c_raw", "xyz"[axis]);
                if (!read_number (device_path, name, &raw))
                        return false;
                snprintf (name, sizeof(name), "in_accel_%c_offset", "xyz"[axis]);
                if (!read_number (device_path, name, &offset))
                        read_number (device_path, "in_accel_offset", &offset);
                snprintf (name, sizeof(name), "in_accel_%c_scale", "xyz"[axis]);
                if (!read_number (device_path, name, &scale))
                        read_number (device_path, "in_accel_scale", &scale);
                if (scale <= 0)
                        return false;
                acceleration[axis] = (raw + offset) * scale;
        }
        return true;
}

static bool
load_mount_matrix (ply_orientation_t *sensor)
{
        static const char *attributes[] = {
                "mount_matrix", "in_accel_mount_matrix", "in_mount_matrix"
        };
        char text[256];
        unsigned int i;

        memcpy (sensor->matrix, identity_matrix, sizeof(identity_matrix));
#ifdef HAVE_UDEV
        struct udev *context = udev_new ();
        struct udev_device *device = context ? udev_device_new_from_syspath (context, sensor->device_path) : NULL;
        const char *property = device ? udev_device_get_property_value (device, "ACCEL_MOUNT_MATRIX") : NULL;
        bool valid = property == NULL || ply_orientation_parse_matrix (property, sensor->matrix);
        bool found = property != NULL;

        if (device)
                udev_device_unref (device);
        if (context)
                udev_unref (context);
        if (found)
                return valid;
#endif
        for (i = 0; i < sizeof(attributes) / sizeof(attributes[0]); i++) {
                if (read_attribute (sensor->device_path, attributes[i], text))
                        return ply_orientation_parse_matrix (text, sensor->matrix);
        }
        return true;
}

static bool
find_sensor (ply_orientation_t *sensor)
{
        glob_t matches = { 0 };
        char *pattern;
        size_t i;
        double acceleration[3];

        if (asprintf (&pattern, "%s/iio:device*", sensor->sysfs_root) < 0)
                return false;
        glob (pattern, 0, NULL, &matches);
        free (pattern);
        for (i = 0; i < matches.gl_pathc; i++) {
                char label[256];

                /* Convertible systems may expose a second sensor in the base. */
                if ((read_attribute (matches.gl_pathv[i], "in_accel_label", label) ||
                     read_attribute (matches.gl_pathv[i], "label", label)) &&
                    strcasestr (label, "base") != NULL)
                        continue;
                if (!read_acceleration (matches.gl_pathv[i], acceleration))
                        continue;
                sensor->device_path = realpath (matches.gl_pathv[i], NULL);
                if (sensor->device_path != NULL && load_mount_matrix (sensor)) {
                        ply_trace ("using orientation sensor %s", sensor->device_path);
                        break;
                }
                free (sensor->device_path);
                sensor->device_path = NULL;
        }
        globfree (&matches);
        return sensor->device_path != NULL;
}

ply_orientation_t *
ply_orientation_new (const char *sysfs_root)
{
        ply_orientation_t *sensor = calloc (1, sizeof(*sensor));

        sensor->sysfs_root = strdup (sysfs_root);
        return sensor;
}

void
ply_orientation_free (ply_orientation_t *sensor)
{
        if (sensor == NULL)
                return;
        free (sensor->sysfs_root);
        free (sensor->device_path);
        free (sensor);
}

bool
ply_orientation_read (ply_orientation_t            *sensor,
                      ply_pixel_buffer_rotation_t *rotation)
{
        double acceleration[3];
        ply_pixel_buffer_rotation_t candidate;

        if (sensor->device_path == NULL && !find_sensor (sensor))
                return false;
        if (!read_acceleration (sensor->device_path, acceleration)) {
                free (sensor->device_path);
                sensor->device_path = NULL;
                sensor->stable_samples = 0;
                return false;
        }
        if (!ply_orientation_from_acceleration (acceleration, sensor->matrix, &candidate)) {
                sensor->stable_samples = 0;
                return false;
        }
        if (sensor->stable_samples == 0 || sensor->candidate != candidate) {
                sensor->candidate = candidate;
                sensor->stable_samples = 1;
                return false;
        }
        /* Three samples at 5 Hz avoid rotating on a single noisy reading. */
        if (sensor->stable_samples < 3)
                sensor->stable_samples++;
        if (sensor->stable_samples < 3)
                return false;
        *rotation = candidate;
        return true;
}
