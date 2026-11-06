/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ply-test.h"
#include "ply-orientation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const double identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

static bool
test_cardinal_orientations (void)
{
        const double readings[][3] = { {0, -10, 0}, {0, 10, 0}, {10, 0, 0}, {-10, 0, 0} };
        const ply_pixel_buffer_rotation_t expected[] = {
                PLY_PIXEL_BUFFER_ROTATE_UPRIGHT, PLY_PIXEL_BUFFER_ROTATE_UPSIDE_DOWN,
                PLY_PIXEL_BUFFER_ROTATE_COUNTER_CLOCKWISE, PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE
        };
        size_t i;

        for (i = 0; i < 4; i++) {
                ply_pixel_buffer_rotation_t rotation;
                PLY_TEST_ASSERT (ply_orientation_from_acceleration (readings[i], identity, &rotation));
                PLY_TEST_ASSERT (rotation == expected[i]);
        }
        return true;
}

static bool
test_uncertain_readings_preserve_orientation (void)
{
        const double readings[][3] = { {0, 0, 10}, {0, 0, 0}, {7, 7, 0}, {NAN, 0, 0}, {0, INFINITY, 0} };
        size_t i;

        for (i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
                ply_pixel_buffer_rotation_t rotation = PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE;
                PLY_TEST_ASSERT (!ply_orientation_from_acceleration (readings[i], identity, &rotation));
                PLY_TEST_ASSERT (rotation == PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE);
        }
        return true;
}

static bool
test_mount_matrix_and_composition (void)
{
        double matrix[9];
        const double acceleration[3] = {10, 0, 0};
        ply_pixel_buffer_rotation_t rotation;

        PLY_TEST_ASSERT (ply_orientation_parse_matrix ("0, -1, 0; -1, 0, 0; 0, 0, 1", matrix));
        PLY_TEST_ASSERT (ply_orientation_from_acceleration (acceleration, matrix, &rotation));
        PLY_TEST_ASSERT (rotation == PLY_PIXEL_BUFFER_ROTATE_UPRIGHT);
        PLY_TEST_ASSERT (!ply_orientation_parse_matrix ("1,0,0;0,0,0;0,0,1", matrix));
        PLY_TEST_ASSERT (!ply_orientation_parse_matrix ("1,0,0;0,1,0;0,0,nan", matrix));
        PLY_TEST_ASSERT (!ply_orientation_parse_matrix ("1,0,0;0,1,0;0,0,1 garbage", matrix));
        PLY_TEST_ASSERT (!ply_orientation_parse_matrix ("1,0,0;0,1,0", matrix));
        PLY_TEST_ASSERT (ply_orientation_compose (PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE,
                                                  PLY_PIXEL_BUFFER_ROTATE_COUNTER_CLOCKWISE) ==
                         PLY_PIXEL_BUFFER_ROTATE_UPRIGHT);
        PLY_TEST_ASSERT (ply_orientation_compose (PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE,
                                                  PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE) ==
                         PLY_PIXEL_BUFFER_ROTATE_UPSIDE_DOWN);
        return true;
}

static bool
write_attribute (const char *root,
                 const char *name,
                 const char *value)
{
        char path[512];
        FILE *file;

        snprintf (path, sizeof(path), "%s/iio:device0/%s", root, name);
        file = fopen (path, "w");
        if (!file)
                return false;
        bool success = fputs (value, file) >= 0;
        return fclose (file) == 0 && success;
}

static bool
test_late_sensor_debounce_calibration_and_recovery (void)
{
        char root[] = "/tmp/plymouth-orientation-XXXXXX";
        char path[512];
        const char *attributes[] = { "in_accel_x_raw", "in_accel_y_raw", "in_accel_z_raw",
                                     "in_accel_scale", "in_accel_offset", "in_accel_mount_matrix" };
        ply_pixel_buffer_rotation_t rotation;
        ply_orientation_t *sensor;
        size_t i;

        PLY_TEST_ASSERT (mkdtemp (root) != NULL);
        sensor = ply_orientation_new (root);
        PLY_TEST_ASSERT (!ply_orientation_read (sensor, &rotation));
        snprintf (path, sizeof(path), "%s/iio:device0", root);
        PLY_TEST_ASSERT (mkdir (path, 0700) == 0);
        PLY_TEST_ASSERT (write_attribute (root, "in_accel_x_raw", "11\n"));
        PLY_TEST_ASSERT (write_attribute (root, "in_accel_y_raw", "1\n"));
        PLY_TEST_ASSERT (write_attribute (root, "in_accel_z_raw", "1\n"));
        PLY_TEST_ASSERT (write_attribute (root, "in_accel_scale", "0.1\n"));
        PLY_TEST_ASSERT (write_attribute (root, "in_accel_offset", "-1\n"));
        PLY_TEST_ASSERT (write_attribute (root, "in_accel_mount_matrix", "0,-1,0;-1,0,0;0,0,1\n"));
        PLY_TEST_ASSERT (!ply_orientation_read (sensor, &rotation));
        PLY_TEST_ASSERT (!ply_orientation_read (sensor, &rotation));
        PLY_TEST_ASSERT (ply_orientation_read (sensor, &rotation));
        PLY_TEST_ASSERT (rotation == PLY_PIXEL_BUFFER_ROTATE_UPRIGHT);

        PLY_TEST_ASSERT (write_attribute (root, "in_accel_x_raw", "bad\n"));
        PLY_TEST_ASSERT (!ply_orientation_read (sensor, &rotation));
        PLY_TEST_ASSERT (write_attribute (root, "in_accel_x_raw", "-9\n"));
        PLY_TEST_ASSERT (!ply_orientation_read (sensor, &rotation));
        PLY_TEST_ASSERT (!ply_orientation_read (sensor, &rotation));
        PLY_TEST_ASSERT (ply_orientation_read (sensor, &rotation));
        PLY_TEST_ASSERT (rotation == PLY_PIXEL_BUFFER_ROTATE_UPSIDE_DOWN);

        for (i = 0; i < sizeof(attributes) / sizeof(attributes[0]); i++) {
                snprintf (path, sizeof(path), "%s/iio:device0/%s", root, attributes[i]);
                PLY_TEST_ASSERT (unlink (path) == 0);
        }
        PLY_TEST_ASSERT (!ply_orientation_read (sensor, &rotation));
        ply_orientation_free (sensor);
        snprintf (path, sizeof(path), "%s/iio:device0", root);
        PLY_TEST_ASSERT (rmdir (path) == 0);
        PLY_TEST_ASSERT (rmdir (root) == 0);
        return true;
}

static const ply_test_case_t cases[] = {
        { "four cardinal orientations", test_cardinal_orientations },
        { "flat, diagonal and invalid readings", test_uncertain_readings_preserve_orientation },
        { "mount matrices and panel rotation", test_mount_matrix_and_composition },
        { "late sensor, debounce, calibration and recovery", test_late_sensor_debounce_calibration_and_recovery }
};

PLY_TEST_MAIN (cases)
