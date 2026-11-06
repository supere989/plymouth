/* plugin.c - drm backend renderer plugin
 *
 * Copyright (C) 2006-2019 Red Hat, Inc.
 *               2008 Charlie Brej <cbrej@cs.man.ac.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by: Charlie Brej <cbrej@cs.man.ac.uk>
 *             Kristian Høgsberg <krh@redhat.com>
 *             Peter Jones <pjones@redhat.com>
 *             Ray Strode <rstrode@redhat.com>
 *             Hans de Goede <hdegoede@redhat.com>
 */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <values.h>
#include <unistd.h>

#include <drm.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "ply-array.h"
#include "ply-buffer.h"
#include "ply-event-loop.h"
#include "ply-input-device.h"
#include "ply-list.h"
#include "ply-logger.h"
#include "ply-hashtable.h"
#include "ply-rectangle.h"
#include "ply-region.h"
#include "ply-utils.h"
#include "ply-terminal.h"

#include "ply-renderer.h"
#include "ply-renderer-plugin.h"
#include "ply-renderer-drm-tile.h"
#include "ply-orientation.h"

#define BYTES_PER_PIXEL (4)

/* Sanity bound on the gamma ramp size reported by the GPU driver. Real
 * hardware uses small values (typically 256 or 1024); this cap keeps the
 * allocation size computation from overflowing on bogus input. */
#define PLY_MAX_GAMMA_SIZE (65536)

/* For builds with libdrm < 2.4.89 */
#ifndef DRM_MODE_ROTATE_0
#define DRM_MODE_ROTATE_0 (1 << 0)
#endif

typedef struct
{
        ply_array_t    *connector_ids;
        drmModeModeInfo mode;

        uint32_t        controller_id;
        uint32_t        console_buffer_id;
        uint32_t        x;
        uint32_t        y;
        bool            scan_out_buffer_needs_reset;
        bool            uses_hw_rotation;

        int             gamma_size;
        uint16_t       *gamma;
} ply_renderer_controller_t;

struct _ply_renderer_head
{
        ply_renderer_backend_t *backend;
        ply_pixel_buffer_t     *pixel_buffer;
        ply_rectangle_t         area;

        unsigned long           row_stride;

        ply_array_t            *controllers;
        uint32_t                scan_out_buffer_id;
};

struct _ply_renderer_input_source
{
        ply_renderer_backend_t             *backend;
        ply_fd_watch_t                     *terminal_input_watch;
        ply_list_t                         *input_devices;

        ply_buffer_t                       *key_buffer;

        ply_renderer_input_source_handler_t handler;
        void                               *user_data;
};

typedef struct
{
        uint32_t id;

        uint32_t handle;
        uint32_t width;
        uint32_t height;
        uint32_t row_stride;

        void    *map_address;
        uint32_t map_size;
        int      map_count;

        uint32_t added_fb : 1;
} ply_renderer_buffer_t;

typedef struct
{
        drmModeModeInfo             mode;
        uint32_t                    connector_id;
        uint32_t                    connector_type;
        uint32_t                    controller_id;
        uint32_t                    possible_controllers;
        int                         device_scale;
        int                         link_status;
        ply_pixel_buffer_rotation_t rotation;
        uint32_t                    tile_group_id;
        uint32_t                    tile_num_h;
        uint32_t                    tile_num_v;
        uint32_t                    tile_h_loc;
        uint32_t                    tile_v_loc;
        uint32_t                    tile_h_size;
        uint32_t                    tile_v_size;
        bool                        tiled;
        bool                        tile_is_single_monitor;
        bool                        connected;
        bool                        uses_hw_rotation;
        bool                        is_non_desktop;
} ply_output_t;

struct _ply_renderer_backend
{
        bool                        console_rotation_valid;
        ply_pixel_buffer_rotation_t console_rotation;
        ply_event_loop_t           *loop;
        ply_terminal_t             *terminal;
        ply_terminal_t             *local_console_terminal;

        int                         device_fd;
        bool                        simpledrm;
        char                       *device_name;
        drmModeRes                 *resources;

        ply_renderer_input_source_t input_source;
        ply_list_t                 *heads;
        ply_hashtable_t            *heads_by_controller_id;

        ply_hashtable_t            *output_buffers;

        ply_output_t               *outputs;
        int                         outputs_len;
        int                         connected_count;

        int32_t                     dither_red;
        int32_t                     dither_green;
        int32_t                     dither_blue;

        uint32_t                    is_active : 1;
        uint32_t                    requires_explicit_flushing : 1;
        uint32_t                    input_source_is_open : 1;

        int                         panel_width;
        int                         panel_height;
        ply_pixel_buffer_rotation_t panel_rotation;
        int                         panel_scale;
        bool                        panel_info_set;
};

ply_renderer_plugin_interface_t *ply_renderer_backend_get_interface (void);

static bool using_input_device (ply_renderer_input_source_t *backend);
static bool open_input_source (ply_renderer_backend_t      *backend,
                               ply_renderer_input_source_t *input_source);
static void flush_head (ply_renderer_backend_t *backend,
                        ply_renderer_head_t    *head);

static bool
ply_renderer_buffer_map (ply_renderer_backend_t *backend,
                         ply_renderer_buffer_t  *buffer)
{
        struct drm_mode_map_dumb map_dumb_buffer_request;
        void *map_address;

        if (buffer->map_address != MAP_FAILED) {
                buffer->map_count++;
                return true;
        }

        memset (&map_dumb_buffer_request, 0, sizeof(struct drm_mode_map_dumb));
        map_dumb_buffer_request.handle = buffer->handle;
        if (drmIoctl (backend->device_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb_buffer_request) < 0) {
                ply_trace ("Could not map GEM object %u: %m", buffer->handle);
                return false;
        }

        map_address = mmap (0, buffer->map_size,
                            PROT_READ | PROT_WRITE, MAP_SHARED,
                            backend->device_fd, map_dumb_buffer_request.offset);

        if (map_address == MAP_FAILED)
                return false;

        buffer->map_address = map_address;
        buffer->map_count++;

        return true;
}

static void
ply_renderer_buffer_unmap (ply_renderer_backend_t *backend,
                           ply_renderer_buffer_t  *buffer)
{
        buffer->map_count--;

        assert (buffer->map_count >= 0);
}

static ply_renderer_buffer_t *
ply_renderer_buffer_new (ply_renderer_backend_t *backend,
                         uint32_t                width,
                         uint32_t                height)
{
        ply_renderer_buffer_t *buffer;
        struct drm_mode_create_dumb create_dumb_buffer_request;

        buffer = calloc (1, sizeof(ply_renderer_buffer_t));
        buffer->width = width;
        buffer->height = height;
        buffer->map_address = MAP_FAILED;

        memset (&create_dumb_buffer_request, 0, sizeof(struct drm_mode_create_dumb));

        create_dumb_buffer_request.width = width;
        create_dumb_buffer_request.height = height;
        create_dumb_buffer_request.bpp = 32;
        create_dumb_buffer_request.flags = 0;

        if (drmIoctl (backend->device_fd,
                      DRM_IOCTL_MODE_CREATE_DUMB,
                      &create_dumb_buffer_request) < 0) {
                free (buffer);
                ply_trace ("Could not allocate GEM object for frame buffer: %m");
                return NULL;
        }

        buffer->handle = create_dumb_buffer_request.handle;
        buffer->row_stride = create_dumb_buffer_request.pitch;
        buffer->map_size = create_dumb_buffer_request.size;

        ply_trace ("returning %ux%u buffer with stride %u",
                   width, height, buffer->row_stride);

        return buffer;
}

static void
ply_renderer_buffer_free (ply_renderer_backend_t *backend,
                          ply_renderer_buffer_t  *buffer)
{
        struct drm_mode_destroy_dumb destroy_dumb_buffer_request;

        if (buffer->added_fb)
                drmModeRmFB (backend->device_fd, buffer->id);

        if (buffer->map_address != MAP_FAILED) {
                munmap (buffer->map_address, buffer->map_size);
                buffer->map_address = MAP_FAILED;
        }

        memset (&destroy_dumb_buffer_request, 0, sizeof(struct drm_mode_destroy_dumb));
        destroy_dumb_buffer_request.handle = buffer->handle;

        if (drmIoctl (backend->device_fd,
                      DRM_IOCTL_MODE_DESTROY_DUMB,
                      &destroy_dumb_buffer_request) < 0)
                ply_trace ("Could not deallocate GEM object %u: %m", buffer->handle);

        free (buffer);
}

static ply_renderer_buffer_t *
get_buffer_from_id (ply_renderer_backend_t *backend,
                    uint32_t                id)
{
        static ply_renderer_buffer_t *buffer;

        buffer = ply_hashtable_lookup (backend->output_buffers, (void *) (uintptr_t) id);

        return buffer;
}

static uint32_t
create_output_buffer (ply_renderer_backend_t *backend,
                      unsigned long           width,
                      unsigned long           height,
                      unsigned long          *row_stride)
{
        ply_renderer_buffer_t *buffer;

        buffer = ply_renderer_buffer_new (backend, width, height);

        if (buffer == NULL) {
                ply_trace ("Could not allocate GEM object for frame buffer: %m");
                return 0;
        }

        if (drmModeAddFB (backend->device_fd, width, height,
                          24, 32, buffer->row_stride, buffer->handle,
                          &buffer->id) != 0) {
                ply_trace ("Could not set up GEM object as frame buffer: %m");
                ply_renderer_buffer_free (backend, buffer);
                return 0;
        }

        *row_stride = buffer->row_stride;

        buffer->added_fb = true;
        ply_hashtable_insert (backend->output_buffers,
                              (void *) (uintptr_t) buffer->id,
                              buffer);

        return buffer->id;
}

static bool
map_buffer (ply_renderer_backend_t *backend,
            uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = get_buffer_from_id (backend, buffer_id);

        assert (buffer != NULL);

        return ply_renderer_buffer_map (backend, buffer);
}

static void
unmap_buffer (ply_renderer_backend_t *backend,
              uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = get_buffer_from_id (backend, buffer_id);

        assert (buffer != NULL);

        ply_renderer_buffer_unmap (backend, buffer);
}

static char *
begin_flush (ply_renderer_backend_t *backend,
             uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = get_buffer_from_id (backend, buffer_id);

        assert (buffer != NULL);

        return buffer->map_address;
}

static void
end_flush (ply_renderer_backend_t *backend,
           uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = get_buffer_from_id (backend, buffer_id);

        assert (buffer != NULL);

        if (backend->requires_explicit_flushing) {
                struct drm_clip_rect flush_area;
                int ret;

                flush_area.x1 = 0;
                flush_area.y1 = 0;
                flush_area.x2 = buffer->width;
                flush_area.y2 = buffer->height;

                ret = drmModeDirtyFB (backend->device_fd, buffer->id, &flush_area, 1);

                if (ret == -ENOSYS)
                        backend->requires_explicit_flushing = false;
        }
}

static void
destroy_output_buffer (ply_renderer_backend_t *backend,
                       uint32_t                buffer_id)
{
        ply_renderer_buffer_t *buffer;

        buffer = ply_hashtable_remove (backend->output_buffers,
                                       (void *) (uintptr_t) buffer_id);

        assert (buffer != NULL);

        ply_renderer_buffer_free (backend, buffer);
}

static bool
get_primary_plane_rotation (ply_renderer_backend_t *backend,
                            uint32_t                controller_id,
                            int                    *primary_id_ret,
                            int                    *rotation_prop_id_ret,
                            uint64_t               *rotation_ret)
{
        drmModeObjectPropertiesPtr plane_props;
        drmModePlaneResPtr plane_resources;
        drmModePropertyPtr prop;
        drmModePlanePtr plane;
        uint64_t rotation = 0;
        uint32_t i, j;
        int rotation_prop_id = -1;
        int primary_id = -1;
        int err;

        if (!controller_id)
                return false;

        err = drmSetClientCap (backend->device_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
        if (err)
                return false;

        plane_resources = drmModeGetPlaneResources (backend->device_fd);
        if (!plane_resources)
                return false;

        for (i = 0; i < plane_resources->count_planes; i++) {
                plane = drmModeGetPlane (backend->device_fd,
                                         plane_resources->planes[i]);
                if (!plane)
                        continue;

                if (plane->crtc_id != controller_id) {
                        drmModeFreePlane (plane);
                        continue;
                }

                plane_props = drmModeObjectGetProperties (backend->device_fd,
                                                          plane->plane_id,
                                                          DRM_MODE_OBJECT_PLANE);

                for (j = 0; plane_props && (j < plane_props->count_props); j++) {
                        prop = drmModeGetProperty (backend->device_fd,
                                                   plane_props->props[j]);
                        if (!prop)
                                continue;

                        if (strcmp (prop->name, "type") == 0 &&
                            plane_props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY) {
                                primary_id = plane->plane_id;
                        }

                        if (strcmp (prop->name, "rotation") == 0) {
                                rotation_prop_id = plane_props->props[j];
                                rotation = plane_props->prop_values[j];
                        }

                        drmModeFreeProperty (prop);
                }

                drmModeFreeObjectProperties (plane_props);
                drmModeFreePlane (plane);

                if (primary_id != -1)
                        break;

                /* Not primary -> clear any found rotation property */
                rotation_prop_id = -1;
        }

        drmModeFreePlaneResources (plane_resources);

        if (primary_id != -1 && rotation_prop_id != -1) {
                *primary_id_ret = primary_id;
                *rotation_prop_id_ret = rotation_prop_id;
                *rotation_ret = rotation;
                return true;
        }

        return false;
}

static ply_pixel_buffer_rotation_t
connector_orientation_prop_to_rotation (drmModePropertyPtr prop,
                                        int                orientation)
{
        const char *name = prop->enums[orientation].name;

        if (strcmp (name, "Upside Down") == 0)
                return PLY_PIXEL_BUFFER_ROTATE_UPSIDE_DOWN;

        if (strcmp (name, "Left Side Up") == 0) {
                /* Left side up, rotate counter clockwise to correct */
                return PLY_PIXEL_BUFFER_ROTATE_COUNTER_CLOCKWISE;
        }

        if (strcmp (name, "Right Side Up") == 0) {
                /* Left side up, rotate clockwise to correct */
                return PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE;
        }

        return PLY_PIXEL_BUFFER_ROTATE_UPRIGHT;
}

static void
ply_renderer_connector_get_tile_info (ply_renderer_backend_t *backend,
                                      uint64_t                blob_id,
                                      ply_output_t           *output)
{
        drmModePropertyBlobPtr blob;
        ply_renderer_drm_tile_info_t tile_info;

        blob = drmModeGetPropertyBlob (backend->device_fd, blob_id);
        if (blob == NULL || blob->data == NULL || blob->length == 0)
                goto out;

        if (ply_renderer_drm_tile_info_parse (blob->data, blob->length, &tile_info)) {
                output->tile_group_id = tile_info.group_id;
                output->tile_is_single_monitor = tile_info.is_single_monitor;
                output->tile_num_h = tile_info.num_h;
                output->tile_num_v = tile_info.num_v;
                output->tile_h_loc = tile_info.h_loc;
                output->tile_v_loc = tile_info.v_loc;
                output->tile_h_size = tile_info.h_size;
                output->tile_v_size = tile_info.v_size;
                output->tiled = true;
        } else {
                ply_trace ("Ignoring malformed TILE property");
        }
out:
        if (blob != NULL)
                drmModeFreePropertyBlob (blob);
}

static void
ply_renderer_connector_get_properties (ply_renderer_backend_t *backend,
                                       drmModeConnector       *connector,
                                       ply_output_t           *output)
{
        int i, primary_id, rotation_prop_id;
        drmModePropertyPtr prop;
        uint64_t rotation;

        output->rotation = PLY_PIXEL_BUFFER_ROTATE_UPRIGHT;
        output->tiled = false;

        for (i = 0; i < connector->count_props; i++) {
                prop = drmModeGetProperty (backend->device_fd, connector->props[i]);
                if (!prop)
                        continue;

                if ((prop->flags & DRM_MODE_PROP_ENUM) &&
                    strcmp (prop->name, "panel orientation") == 0)
                        output->rotation = connector_orientation_prop_to_rotation (prop, connector->prop_values[i]);

                if ((prop->flags & DRM_MODE_PROP_BLOB) &&
                    strcmp (prop->name, "TILE") == 0 &&
                    connector->prop_values[i] != 0)
                        ply_renderer_connector_get_tile_info (backend,
                                                              connector->prop_values[i],
                                                              output);

                if ((prop->flags & DRM_MODE_PROP_ENUM) &&
                    strcmp (prop->name, "link-status") == 0) {
                        output->link_status = connector->prop_values[i];
                        ply_trace ("link-status %d", output->link_status);
                }
                if (strcmp (prop->name, "non-desktop") == 0) {
                        output->is_non_desktop = connector->prop_values[i];
                }

                drmModeFreeProperty (prop);
        }

        /* If the firmware setup the plane to use hw 180° rotation, then we keep
         * the hw rotation. This avoids a flicker and avoids the splash turning
         * upside-down when mutter turns hw-rotation back on and then fades from
         * the splash to the login screen.
         */
        if (output->rotation == PLY_PIXEL_BUFFER_ROTATE_UPSIDE_DOWN &&
            get_primary_plane_rotation (backend, output->controller_id,
                                        &primary_id, &rotation_prop_id,
                                        &rotation) &&
            rotation == DRM_MODE_ROTATE_180) {
                ply_trace ("Keeping hw 180° rotation");
                output->rotation = PLY_PIXEL_BUFFER_ROTATE_UPRIGHT;
                output->uses_hw_rotation = true;
        }
}

static bool
ply_renderer_controller_add_connector (ply_renderer_controller_t *controller,
                                       ply_output_t              *output)
{
        if (output->link_status == DRM_MODE_LINK_STATUS_BAD)
                controller->scan_out_buffer_needs_reset = true;

        if (output->mode.hdisplay != controller->mode.hdisplay ||
            output->mode.vdisplay != controller->mode.vdisplay) {
                ply_trace ("Tried to add connector with resolution %dx%d to %dx%d controller",
                           (int) output->mode.hdisplay, (int) output->mode.vdisplay,
                           (int) controller->mode.hdisplay, (int) controller->mode.vdisplay);
                return false;
        }

        if (ply_array_contains_uint32_element (controller->connector_ids, output->connector_id)) {
                ply_trace ("Controller already contains connector with id %d", output->connector_id);
                return false;
        }

        ply_trace ("Adding connector with id %d to controller %d",
                   (int) output->connector_id, (int) controller->controller_id);
        ply_array_add_uint32_element (controller->connector_ids, output->connector_id);

        return true;
}

static ply_renderer_controller_t *
ply_renderer_controller_new (ply_output_t *output,
                             uint32_t      console_buffer_id,
                             int           gamma_size,
                             uint32_t      x,
                             uint32_t      y)
{
        ply_renderer_controller_t *controller;
        int i, step;

        controller = calloc (1, sizeof(ply_renderer_controller_t));
        controller->connector_ids = ply_array_new (PLY_ARRAY_ELEMENT_TYPE_UINT32);
        controller->controller_id = output->controller_id;
        controller->console_buffer_id = console_buffer_id;
        controller->mode = output->mode;
        controller->uses_hw_rotation = output->uses_hw_rotation;
        controller->x = x;
        controller->y = y;

        /* Reject gamma_size < 2 (divide by zero) or > max (overflow). */
        if (gamma_size > 1 && gamma_size <= PLY_MAX_GAMMA_SIZE) {
                controller->gamma_size = gamma_size;
                controller->gamma = malloc ((size_t) gamma_size * 3 * sizeof(uint16_t));

                step = UINT16_MAX / (gamma_size - 1);
                for (i = 0; i < gamma_size; i++) {
                        controller->gamma[0 * gamma_size + i] = i * step; /* red */
                        controller->gamma[1 * gamma_size + i] = i * step; /* green */
                        controller->gamma[2 * gamma_size + i] = i * step; /* blue */
                }
        }

        ply_renderer_controller_add_connector (controller, output);
        assert (ply_array_get_size (controller->connector_ids) > 0);
        return controller;
}

static ply_renderer_head_t *
ply_renderer_head_new (ply_renderer_backend_t *backend,
                       ply_output_t           *output,
                       uint32_t                console_buffer_id,
                       int                     gamma_size,
                       uint32_t                width,
                       uint32_t                height,
                       uint32_t                x,
                       uint32_t                y)
{
        ply_renderer_head_t *head;
        ply_renderer_controller_t *controller;

        head = calloc (1, sizeof(ply_renderer_head_t));
        head->backend = backend;
        head->controllers = ply_array_new (PLY_ARRAY_ELEMENT_TYPE_POINTER);
        head->area.width = width;
        head->area.height = height;

        controller = ply_renderer_controller_new (output, console_buffer_id,
                                                  gamma_size, x, y);
        ply_array_add_pointer_element (head->controllers, controller);

        head->pixel_buffer = ply_pixel_buffer_new_with_device_rotation (head->area.width, head->area.height, output->rotation);
        ply_pixel_buffer_set_device_scale (head->pixel_buffer, output->device_scale);

        ply_trace ("Creating %ldx%ld renderer head", head->area.width, head->area.height);
        ply_pixel_buffer_fill_with_color (head->pixel_buffer, NULL,
                                          0.0, 0.0, 0.0, 1.0);
        /* Delay flush till first actual draw */
        ply_region_clear (ply_pixel_buffer_get_updated_areas (head->pixel_buffer));

        ply_list_append_data (backend->heads, head);
        ply_hashtable_insert (backend->heads_by_controller_id,
                              (void *) (intptr_t) output->controller_id,
                              head);
        return head;
}

static void
ply_renderer_controller_free (ply_renderer_controller_t *controller)
{
        ply_array_free (controller->connector_ids);
        free (controller->gamma);
        free (controller);
}

static void
ply_renderer_head_free (ply_renderer_head_t *head)
{
        ply_renderer_controller_t **controllers;
        int i;

        ply_trace ("freeing %ldx%ld renderer head", head->area.width, head->area.height);
        ply_pixel_buffer_free (head->pixel_buffer);

        controllers = (ply_renderer_controller_t **) ply_array_get_pointer_elements (head->controllers);
        for (i = 0; i < ply_array_get_size (head->controllers); i++) {
                ply_renderer_controller_free (controllers[i]);
        }
        ply_array_free (head->controllers);
        free (head);
}

static void
ply_renderer_controller_clear_plane_rotation (ply_renderer_backend_t    *backend,
                                              ply_renderer_controller_t *controller)
{
        int primary_id, rotation_prop_id, err;
        uint64_t rotation;

        if (controller->uses_hw_rotation)
                return;

        if (get_primary_plane_rotation (backend, controller->controller_id,
                                        &primary_id, &rotation_prop_id,
                                        &rotation) &&
            rotation != DRM_MODE_ROTATE_0) {
                err = drmModeObjectSetProperty (backend->device_fd,
                                                primary_id,
                                                DRM_MODE_OBJECT_PLANE,
                                                rotation_prop_id,
                                                DRM_MODE_ROTATE_0);
                ply_trace ("Cleared rotation on primary plane %d result %d",
                           primary_id, err);
        }
}

static bool
ply_renderer_controller_set_scan_out_buffer (ply_renderer_backend_t    *backend,
                                             ply_renderer_controller_t *controller,
                                             uint32_t                   buffer_id)
{
        drmModeModeInfo *mode = &controller->mode;
        uint32_t *connector_ids;
        int number_of_connectors;

        connector_ids = (uint32_t *) ply_array_get_uint32_elements (controller->connector_ids);
        number_of_connectors = ply_array_get_size (controller->connector_ids);

        ply_trace ("Setting scan out buffer of controller %d to our buffer at %u,%u",
                   controller->controller_id, controller->x, controller->y);

        /* Set gamma table, do this only once */
        if (controller->gamma) {
                drmModeCrtcSetGamma (backend->device_fd,
                                     controller->controller_id,
                                     controller->gamma_size,
                                     controller->gamma + 0 * controller->gamma_size,
                                     controller->gamma + 1 * controller->gamma_size,
                                     controller->gamma + 2 * controller->gamma_size);
                free (controller->gamma);
                controller->gamma = NULL;
        }

        /* Tell the controller to use the allocated scan out buffer on each connectors
         */
        if (drmModeSetCrtc (backend->device_fd, controller->controller_id, buffer_id,
                            controller->x, controller->y,
                            connector_ids, number_of_connectors, mode) < 0) {
                ply_trace ("Couldn't set scan out buffer for head with controller id %d",
                           controller->controller_id);
                return false;
        }

        ply_renderer_controller_clear_plane_rotation (backend, controller);
        return true;
}

static bool
ply_renderer_head_add_controller (ply_renderer_backend_t *backend,
                                  ply_renderer_head_t    *head,
                                  ply_output_t           *output,
                                  uint32_t                console_buffer_id,
                                  int                     gamma_size,
                                  uint32_t                x,
                                  uint32_t                y)
{
        ply_renderer_controller_t *controller;

        controller = ply_renderer_controller_new (output, console_buffer_id,
                                                  gamma_size, x, y);
        ply_array_add_pointer_element (head->controllers, controller);
        ply_hashtable_insert (backend->heads_by_controller_id,
                              (void *) (intptr_t) output->controller_id,
                              head);
        return true;
}

static bool
ply_renderer_head_map (ply_renderer_backend_t *backend,
                       ply_renderer_head_t    *head)
{
        assert (backend != NULL);
        assert (backend->device_fd >= 0);
        assert (backend != NULL);

        assert (head != NULL);

        ply_trace ("Creating buffer for %ldx%ld renderer head", head->area.width, head->area.height);
        head->scan_out_buffer_id = create_output_buffer (backend,
                                                         head->area.width, head->area.height,
                                                         &head->row_stride);

        if (head->scan_out_buffer_id == 0)
                return false;

        ply_trace ("Mapping buffer for %ldx%ld renderer head", head->area.width, head->area.height);
        if (!map_buffer (backend, head->scan_out_buffer_id)) {
                destroy_output_buffer (backend, head->scan_out_buffer_id);
                head->scan_out_buffer_id = 0;
                return false;
        }

        {
                ply_renderer_controller_t **controllers;
                int i;

                controllers = (ply_renderer_controller_t **) ply_array_get_pointer_elements (head->controllers);
                for (i = 0; i < ply_array_get_size (head->controllers); i++) {
                        controllers[i]->scan_out_buffer_needs_reset = true;
                }
        }
        return true;
}

static void
ply_renderer_head_unmap (ply_renderer_backend_t *backend,
                         ply_renderer_head_t    *head)
{
        ply_trace ("unmapping %ldx%ld renderer head", head->area.width, head->area.height);
        unmap_buffer (backend, head->scan_out_buffer_id);

        destroy_output_buffer (backend, head->scan_out_buffer_id);
        head->scan_out_buffer_id = 0;
}

static void
ply_renderer_head_remove_from_lookup_table (ply_renderer_backend_t *backend,
                                            ply_renderer_head_t    *head)
{
        ply_renderer_controller_t **controllers;
        int i;

        controllers = (ply_renderer_controller_t **) ply_array_get_pointer_elements (head->controllers);
        for (i = 0; i < ply_array_get_size (head->controllers); i++) {
                ply_hashtable_remove (backend->heads_by_controller_id,
                                      (void *) (intptr_t) controllers[i]->controller_id);
        }
}

static void
ply_renderer_head_remove (ply_renderer_backend_t *backend,
                          ply_renderer_head_t    *head)
{
        if (head->scan_out_buffer_id)
                ply_renderer_head_unmap (backend, head);

        ply_renderer_head_remove_from_lookup_table (backend, head);
        ply_list_remove_data (backend->heads, head);
        ply_renderer_head_free (head);
}

static void
ply_renderer_head_remove_connector (ply_renderer_backend_t *backend,
                                    ply_renderer_head_t    *head,
                                    uint32_t                connector_id)
{
        ply_renderer_controller_t **controllers;
        ply_renderer_controller_t *controller = NULL;
        int controller_index, i, size;
        uint32_t *connector_ids;

        controllers = (ply_renderer_controller_t **) ply_array_get_pointer_elements (head->controllers);
        for (controller_index = 0; controller_index < ply_array_get_size (head->controllers); controller_index++) {
                if (ply_array_contains_uint32_element (controllers[controller_index]->connector_ids,
                                                       connector_id)) {
                        controller = controllers[controller_index];
                        break;
                }
        }

        if (controller == NULL) {
                ply_trace ("Head does not contain connector %u, cannot remove", connector_id);
                return;
        }

        size = ply_array_get_size (controller->connector_ids);
        if (size == 1) {
                ply_renderer_head_remove (backend, head);
                return;
        }

        /* Empty the array and re-add all connectors except the one being removed */
        connector_ids = ply_array_steal_uint32_elements (controller->connector_ids);
        for (i = 0; i < size; i++) {
                if (connector_ids[i] != connector_id)
                        ply_array_add_uint32_element (controller->connector_ids,
                                                      connector_ids[i]);
        }
        free (connector_ids);
}

static void
flush_area (const char      *src,
            unsigned long    src_row_stride,
            char            *dst,
            unsigned long    dst_row_stride,
            ply_rectangle_t *area_to_flush)
{
        unsigned long y1, y2, y;

        y1 = area_to_flush->y;
        y2 = y1 + area_to_flush->height;

        if (area_to_flush->width * 4 == src_row_stride &&
            area_to_flush->width * 4 == dst_row_stride) {
                memcpy (dst, src, area_to_flush->width * area_to_flush->height * 4);
                return;
        }

        for (y = y1; y < y2; y++) {
                memcpy (dst, src, area_to_flush->width * 4);
                dst += dst_row_stride;
                src += src_row_stride;
        }
}

static void
ply_renderer_head_flush_area (ply_renderer_head_t *head,
                              ply_rectangle_t     *area_to_flush,
                              char                *map_address)
{
        uint32_t *shadow_buffer;
        char *dst, *src;

        shadow_buffer = ply_pixel_buffer_get_argb32_data (head->pixel_buffer);

        dst = &map_address[area_to_flush->y * head->row_stride + area_to_flush->x * BYTES_PER_PIXEL];
        src = (char *) &shadow_buffer[area_to_flush->y * head->area.width + area_to_flush->x];

        flush_area (src, head->area.width * 4, dst, head->row_stride, area_to_flush);
}

static void
free_heads (ply_renderer_backend_t *backend)
{
        ply_list_node_t *node;

        node = ply_list_get_first_node (backend->heads);
        while (node != NULL) {
                ply_list_node_t *next_node;
                ply_renderer_head_t *head;

                head = (ply_renderer_head_t *) ply_list_node_get_data (node);
                next_node = ply_list_get_next_node (backend->heads, node);

                ply_renderer_head_remove_from_lookup_table (backend, head);
                ply_renderer_head_free (head);
                ply_list_remove_node (backend->heads, node);

                node = next_node;
        }
}

static ply_renderer_backend_t *
create_backend (const char     *device_name,
                ply_terminal_t *terminal,
                ply_terminal_t *local_console_terminal)
{
        ply_renderer_backend_t *backend;

        backend = calloc (1, sizeof(ply_renderer_backend_t));

        if (device_name != NULL)
                backend->device_name = strdup (device_name);
        else
                backend->device_name = strdup ("/dev/dri/card0");

        ply_trace ("creating renderer backend for device %s", backend->device_name);

        backend->device_fd = -1;

        backend->loop = ply_event_loop_get_default ();
        backend->heads = ply_list_new ();
        backend->input_source.key_buffer = ply_buffer_new ();
        backend->input_source.input_devices = ply_list_new ();
        backend->terminal = terminal;
        backend->local_console_terminal = local_console_terminal;
        backend->requires_explicit_flushing = true;
        backend->output_buffers = ply_hashtable_new (ply_hashtable_direct_hash,
                                                     ply_hashtable_direct_compare);
        backend->heads_by_controller_id = ply_hashtable_new (NULL, NULL);

        return backend;
}

static const char *
get_device_name (ply_renderer_backend_t *backend)
{
        return backend->device_name;
}

static void
unload_driver (ply_renderer_backend_t *backend)
{
        if (backend->device_fd < 0)
                return;

        ply_trace ("unloading driver");
        drmClose (backend->device_fd);
        backend->device_fd = -1;
}

static void
destroy_backend (ply_renderer_backend_t *backend)
{
        ply_trace ("destroying renderer backend for device %s", backend->device_name);
        free_heads (backend);

        unload_driver (backend);

        free (backend->device_name);
        ply_hashtable_free (backend->output_buffers);
        ply_hashtable_free (backend->heads_by_controller_id);
        ply_list_free (backend->input_source.input_devices);

        free (backend->outputs);
        free (backend);
}

static void
activate (ply_renderer_backend_t *backend)
{
        ply_renderer_head_t *head;
        ply_list_node_t *node;

        ply_trace ("taking master and scanning out");
        backend->is_active = true;

        drmSetMaster (backend->device_fd);
        node = ply_list_get_first_node (backend->heads);
        while (node != NULL) {
                head = (ply_renderer_head_t *) ply_list_node_get_data (node);
                /* Flush out any pending drawing to the buffer */
                flush_head (backend, head);
                node = ply_list_get_next_node (backend->heads, node);
        }
}

static void
deactivate (ply_renderer_backend_t *backend)
{
        ply_trace ("dropping master");
        drmDropMaster (backend->device_fd);
        backend->is_active = false;
}

static void
on_active_vt_changed (ply_renderer_backend_t *backend)
{
        if (ply_terminal_is_active (backend->terminal)) {
                ply_trace ("activating on vt change");
                activate (backend);
        } else {
                ply_trace ("deactivating on vt change");
                deactivate (backend);
        }
}

static bool
load_driver (ply_renderer_backend_t *backend)
{
        drmVersion *version;
        int device_fd;

        ply_trace ("Opening '%s'", backend->device_name);
        device_fd = open (backend->device_name, O_RDWR);

        if (device_fd < 0) {
                ply_trace ("open failed: %m");
                return false;
        }

        version = drmGetVersion (device_fd);
        if (version) {
                ply_trace ("drm driver: %s", version->name);
                if (strcmp (version->name, "simpledrm") == 0)
                        backend->simpledrm = true;

                drmFreeVersion (version);
        }

        backend->device_fd = device_fd;

        drmDropMaster (device_fd);

        return true;
}

static bool
open_device (ply_renderer_backend_t *backend)
{
        assert (backend != NULL);
        assert (backend->device_name != NULL);

        if (!load_driver (backend))
                return false;

        if (backend->terminal == NULL)
                return true;

        if (!ply_terminal_open (backend->terminal)) {
                ply_trace ("could not open terminal: %m");
                return false;
        }

        if (!ply_terminal_is_vt (backend->terminal)) {
                ply_trace ("terminal is not a VT");
                ply_terminal_close (backend->terminal);
                return false;
        }

        ply_terminal_watch_for_active_vt_change (backend->terminal,
                                                 (ply_terminal_active_vt_changed_handler_t)
                                                 on_active_vt_changed,
                                                 backend);

        return true;
}

static void
close_device (ply_renderer_backend_t *backend)
{
        ply_trace ("closing device");

        free_heads (backend);

        if (backend->terminal != NULL) {
                ply_terminal_stop_watching_for_active_vt_change (backend->terminal,
                                                                 (ply_terminal_active_vt_changed_handler_t)
                                                                 on_active_vt_changed,
                                                                 backend);
        }

        unload_driver (backend);
}

static void
output_get_controller_info (ply_renderer_backend_t *backend,
                            drmModeConnector       *connector,
                            ply_output_t           *output)
{
        int i;
        drmModeEncoder *encoder;

        assert (backend != NULL);

        output->possible_controllers = 0xffffffff;

        for (i = 0; i < connector->count_encoders; i++) {
                encoder = drmModeGetEncoder (backend->device_fd,
                                             connector->encoders[i]);

                if (encoder == NULL)
                        continue;

                if (encoder->encoder_id == connector->encoder_id && encoder->crtc_id) {
                        ply_trace ("Found already lit monitor on connector %u using controller %u",
                                   connector->connector_id, encoder->crtc_id);
                        output->controller_id = encoder->crtc_id;
                }

                /* Like mutter and xf86-drv-modesetting only select controllers
                 * which are supported by all the connector's encoders.
                 */
                output->possible_controllers &= encoder->possible_crtcs;
                ply_trace ("connector %u encoder %u possible controllers 0x%08x/0x%08x",
                           connector->connector_id, encoder->encoder_id,
                           encoder->possible_crtcs, output->possible_controllers);
                drmModeFreeEncoder (encoder);
        }
}

static drmModeModeInfo *
find_matching_connector_mode (ply_renderer_backend_t *backend,
                              drmModeConnector       *connector,
                              drmModeModeInfo        *mode)
{
        int i;

        for (i = 0; i < connector->count_modes; i++) {
                if (ply_renderer_drm_modes_are_equal (&connector->modes[i], mode)) {
                        ply_trace ("Found connector mode index %d for mode %dx%d",
                                   i, mode->hdisplay, mode->vdisplay);

                        return &connector->modes[i];
                }
        }

        return NULL;
}

static drmModeModeInfo *
find_tile_mode (drmModeConnector *connector,
                ply_output_t     *output)
{
        ply_renderer_drm_tile_info_t tile_info = {
                .h_size = output->tile_h_size,
                .v_size = output->tile_v_size,
        };
        drmModeModeInfo *mode;

        mode = ply_renderer_drm_find_tile_mode (connector->modes,
                                                connector->count_modes,
                                                &tile_info);
        if (mode != NULL)
                ply_trace ("Found tile mode index %td for %ux%u tile",
                           mode - connector->modes,
                           output->tile_h_size, output->tile_v_size);

        return mode;
}

static drmModeModeInfo *
get_preferred_mode (drmModeConnector *connector)
{
        int i;

        for (i = 0; i < connector->count_modes; i++) {
                if (connector->modes[i].type & DRM_MODE_TYPE_USERDEF) {
                        ply_trace ("Found user set mode %dx%d at index %d",
                                   connector->modes[i].hdisplay,
                                   connector->modes[i].vdisplay, i);
                        return &connector->modes[i];
                }
        }

        for (i = 0; i < connector->count_modes; i++) {
                if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
                        ply_trace ("Found preferred mode %dx%d at index %d",
                                   connector->modes[i].hdisplay,
                                   connector->modes[i].vdisplay, i);
                        return &connector->modes[i];
                }
        }

        return NULL;
}

static drmModeModeInfo *
get_active_mode (ply_renderer_backend_t *backend,
                 drmModeConnector       *connector,
                 ply_output_t           *output)
{
        drmModeCrtc *controller;
        drmModeModeInfo *mode;

        controller = drmModeGetCrtc (backend->device_fd, output->controller_id);
        if (!controller || !controller->mode_valid) {
                ply_trace ("No valid mode currently active on monitor");
                return NULL;
        }

        ply_trace ("Looking for connector mode index of active mode %dx%d",
                   controller->mode.hdisplay, controller->mode.vdisplay);

        mode = find_matching_connector_mode (backend, connector, &controller->mode);

        drmModeFreeCrtc (controller);

        return mode;
}

static void
get_output_info (ply_renderer_backend_t *backend,
                 uint32_t                connector_id,
                 ply_output_t           *output)
{
        drmModeModeInfo *mode = NULL;
        drmModeConnector *connector;
        bool has_90_rotation = false;

        memset (output, 0, sizeof(*output));
        output->connector_id = connector_id;

        connector = drmModeGetConnector (backend->device_fd, connector_id);
        if (connector == NULL)
                return;

        if (connector->connection != DRM_MODE_CONNECTED ||
            connector->count_modes <= 0)
                goto out;

        output_get_controller_info (backend, connector, output);
        ply_renderer_connector_get_properties (backend, connector, output);
        /* ignore non-desktop outputs */
        if (output->is_non_desktop)
                goto out;
        if (output->rotation == PLY_PIXEL_BUFFER_ROTATE_COUNTER_CLOCKWISE ||
            output->rotation == PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE)
                has_90_rotation = true;

        if (!output->tiled)
                mode = get_preferred_mode (connector);

        if (!mode && output->controller_id)
                mode = get_active_mode (backend, connector, output);

        if (!mode && output->tiled)
                mode = find_tile_mode (connector, output);

        /* If we couldn't find the current active mode, fall back to the first available. */
        if (!mode) {
                ply_trace ("falling back to first available mode");
                mode = &connector->modes[0];
        }
        output->mode = *mode;

        if (backend->simpledrm)
                output->device_scale = ply_guess_device_scale (mode->hdisplay, mode->vdisplay,
                                                               connector->mmWidth,
                                                               connector->mmHeight);
        else
                output->device_scale = ply_get_device_scale (mode->hdisplay, mode->vdisplay,
                                                             (!has_90_rotation) ? connector->mmWidth : connector->mmHeight,
                                                             (!has_90_rotation) ? connector->mmHeight : connector->mmWidth);
        output->connector_type = connector->connector_type;
        output->connected = true;
out:
        drmModeFreeConnector (connector);
}

/* Some controllers can only drive some outputs, we want to find a combination
 * where all (connected) outputs get a controller. To do this setup_outputs
 * picks which output to assign a controller for first (trying all outputs), so
 * that that one will get the first (free) controller and then recurses into
 * itself to assign the remaining outputs. This tries assigning all remainig
 * unassigned outputs first and returns the best result of all possible
 * assignment orders for the remaining unassigned outputs.
 * This repeats until we find an assignment order which results in a controller
 * for all outputs, or we've tried all possible assignment orders.
 */
static uint32_t
find_controller_for_output (ply_renderer_backend_t *backend,
                            const ply_output_t     *outputs,
                            int                     outputs_len,
                            int                     output_idx)
{
        uint32_t possible_controllers = outputs[output_idx].possible_controllers;
        int i, j;

        for (i = 0; i < backend->resources->count_crtcs; i++) {
                uint32_t controller_id = backend->resources->crtcs[i];

                if (!(possible_controllers & (1 << i)))
                        continue; /* controller not usable for this connector */
                for (j = 0; j < outputs_len; j++) {
                        if (outputs[j].controller_id == controller_id)
                                break;
                }
                if (j < outputs_len)
                        continue; /* controller already in use */
                return controller_id;
        }

        return 0;
}

static int
count_setup_controllers (const ply_output_t *outputs,
                         int                 outputs_len)
{
        int i, count = 0;

        for (i = 0; i < outputs_len; i++) {
                if (outputs[i].controller_id)
                        count++;
        }

        return count;
}

static ply_output_t *
setup_outputs (ply_renderer_backend_t *backend,
               const ply_output_t     *outputs,
               int                     outputs_len)
{
        const ply_output_t *best_outputs;
        ply_output_t *new_outputs;
        int i, count, best_count;
        uint32_t controller_id;

        best_count = count_setup_controllers (outputs, outputs_len);
        best_outputs = outputs;

        for (i = 0; i < outputs_len && best_count < backend->connected_count; i++) {
                /* Not connected or already assigned? */
                if (!outputs[i].connected || outputs[i].controller_id)
                        continue;

                /* Assign controller for connector i */
                controller_id = find_controller_for_output (backend, outputs, outputs_len, i);
                if (!controller_id)
                        continue;

                /* Add the new controller to a copy of the passed in connector
                 * template, we want to try all possible permutations of
                 * unassigned outputs without modifying the template.
                 */
                new_outputs = calloc (outputs_len, sizeof(*new_outputs));
                memcpy (new_outputs, outputs, outputs_len * sizeof(ply_output_t));
                new_outputs[i].controller_id = controller_id;

                /* Recurse into ourselves to assign remaining controllers,
                 * trying all possible assignment orders.
                 */
                new_outputs = setup_outputs (backend, new_outputs, outputs_len);

                count = count_setup_controllers (new_outputs, outputs_len);
                if (count > best_count) {
                        if (best_outputs != outputs)
                                free ((void *) best_outputs);
                        best_outputs = new_outputs;
                        best_count = count;
                } else {
                        free (new_outputs);
                }
        }

        if (best_outputs != outputs)
                free ((void *) outputs);

        /* Our caller is allowed to modify outputs, cast-away the const */
        return (ply_output_t *) best_outputs;
}

static void
remove_output (ply_renderer_backend_t *backend,
               ply_output_t           *output)
{
        ply_renderer_head_t *head;

        head = ply_hashtable_lookup (backend->heads_by_controller_id,
                                     (void *) (intptr_t) output->controller_id);
        if (head == NULL) {
                ply_trace ("Could not find head for connector %u, controller %u, cannot remove",
                           output->connector_id, output->controller_id);
                return;
        }

        ply_renderer_head_remove_connector (backend, head, output->connector_id);
}

/* Check if an output has changed since we last enumerated it; and if
 * it has changed remove it from the head it is part of.
 */
static bool
check_if_output_has_changed (ply_renderer_backend_t *backend,
                             ply_output_t           *new_output)
{
        ply_output_t *old_output = NULL;
        int i;

        for (i = 0; i < backend->outputs_len; i++) {
                if (backend->outputs[i].connector_id == new_output->connector_id) {
                        old_output = &backend->outputs[i];
                        break;
                }
        }

        if (!old_output || !old_output->controller_id)
                return false;

        if (memcmp (old_output, new_output, sizeof(ply_output_t)) == 0)
                return false;

        ply_trace ("Output for connector %u changed, removing", old_output->connector_id);
        remove_output (backend, old_output);
        return true;
}

/* Sometimes the EFI firmware sets up the framebuffer at 800x600 or 1024x768
 * instead of the native panel resolution. In this case it is better to wait
 * for the native driver to load, so we return false from query_device ().
 */
static bool
check_simpledrm_resolution (ply_renderer_backend_t *backend,
                            ply_output_t           *output)
{
        if (!backend->simpledrm)
                return true;

        if (!output->connected)
                return true;

        if ((output->mode.hdisplay == 640 && output->mode.vdisplay == 480) ||
            (output->mode.hdisplay == 800 && output->mode.vdisplay == 600) ||
            (output->mode.hdisplay == 1024 && output->mode.vdisplay == 768)) {
                ply_trace ("Skipping simpledrm device with mode %dx%d",
                           output->mode.hdisplay, output->mode.vdisplay);
                return false;
        }

        return true;
}

static bool
outputs_share_tile_group (const ply_output_t *a,
                          const ply_output_t *b)
{
        return a->tiled && b->tiled &&
               a->tile_group_id == b->tile_group_id &&
               a->tile_is_single_monitor && b->tile_is_single_monitor &&
               a->tile_num_h == b->tile_num_h &&
               a->tile_num_v == b->tile_num_v &&
               a->tile_h_size == b->tile_h_size &&
               a->tile_v_size == b->tile_v_size;
}

static ply_renderer_head_t *
find_existing_head_for_tile_group (ply_renderer_backend_t *backend,
                                   const ply_output_t     *outputs,
                                   int                     output_index)
{
        ply_renderer_head_t *head;
        int i;

        for (i = 0; i < output_index; i++) {
                if (!outputs_share_tile_group (&outputs[output_index], &outputs[i]))
                        continue;

                head = ply_hashtable_lookup (backend->heads_by_controller_id,
                                             (void *) (intptr_t) outputs[i].controller_id);
                if (head != NULL)
                        return head;
        }

        return NULL;
}

static bool
get_controller_state (ply_renderer_backend_t *backend,
                      ply_output_t           *output,
                      uint32_t               *console_buffer_id,
                      int                    *gamma_size)
{
        drmModeCrtc *controller;

        controller = drmModeGetCrtc (backend->device_fd, output->controller_id);
        if (controller == NULL)
                return false;

        *console_buffer_id = controller->buffer_id;
        *gamma_size = controller->gamma_size;
        drmModeFreeCrtc (controller);
        return true;
}

static bool
output_is_builtin (const ply_output_t *output)
{
        return output->connector_type == DRM_MODE_CONNECTOR_LVDS ||
               output->connector_type == DRM_MODE_CONNECTOR_eDP ||
               output->connector_type == DRM_MODE_CONNECTOR_DSI;
}

static void
update_panel_info (ply_renderer_backend_t                 *backend,
                   const ply_output_t                     *outputs,
                   const bool                             *complete_tile_groups,
                   const ply_renderer_drm_tile_geometry_t *tile_geometries,
                   int                                     outputs_len)
{
        int i;

        backend->panel_info_set = false;
        for (i = 0; i < outputs_len; i++) {
                if (!outputs[i].connected || outputs[i].controller_id == 0)
                        continue;

                /* A builtin panel takes precedence over the first external output. */
                if (backend->panel_info_set && !output_is_builtin (&outputs[i]))
                        continue;

                if (complete_tile_groups[i]) {
                        backend->panel_width = tile_geometries[i].width;
                        backend->panel_height = tile_geometries[i].height;
                } else {
                        backend->panel_width = outputs[i].mode.hdisplay;
                        backend->panel_height = outputs[i].mode.vdisplay;
                }
                backend->panel_rotation = outputs[i].rotation;
                backend->panel_scale = outputs[i].device_scale;
                backend->panel_info_set = true;
        }
}

/* Update our outputs array to match the hardware state and
 * create and/or remove heads as necessary.
 * Returns true if any heads were modified.
 */
static bool
create_heads_for_active_connectors (ply_renderer_backend_t *backend,
                                    bool                    force,
                                    bool                    change)
{
        int i, j, number_of_setup_outputs, outputs_len;
        ply_output_t *outputs;
        ply_renderer_drm_tile_output_t *tile_outputs;
        ply_renderer_drm_tile_geometry_t *tile_geometries;
        bool *complete_tile_groups;
        bool changed = false;

        /* Step 1:
         * Query all outputs and:
         * 1.1 Remove currently connected outputs from their heads if changed.
         * 1.2 Build a new outputs array from scratch. For any unchanged
         *     outputs for which we already have a head, we will end up in
         *     ply_renderer_controller_add_connector which will ignore the
         *     already added connector.
         */
        ply_trace ("(Re)enumerating all outputs");

        outputs = calloc (backend->resources->count_connectors, sizeof(*outputs));
        outputs_len = backend->resources->count_connectors;

        backend->connected_count = 0;
        for (i = 0; i < outputs_len; i++) {
                get_output_info (backend, backend->resources->connectors[i], &outputs[i]);

                if (!force && !check_simpledrm_resolution (backend, &outputs[i])) {
                        free (outputs);
                        return false;
                }

                if (check_if_output_has_changed (backend, &outputs[i]))
                        changed = true;

                if (outputs[i].connected)
                        backend->connected_count++;
        }

        /* Step 2:
         * Free the old outputs array, now that we have checked for changes
         * we no longer need it.
         */
        free (backend->outputs);
        backend->outputs = NULL;

        /* Step 3:
         * Drop controllers for clones for which we've picked different modes.
         */
        for (i = 0; i < outputs_len; i++) {
                if (!outputs[i].controller_id)
                        continue;

                for (j = i + 1; j < outputs_len; j++) {
                        if (!outputs[j].controller_id)
                                continue;

                        if (outputs[i].controller_id == outputs[j].controller_id &&
                            (outputs[i].mode.hdisplay != outputs[j].mode.hdisplay ||
                             outputs[i].mode.vdisplay != outputs[j].mode.vdisplay)) {
                                ply_trace ("connector %u uses same controller as %u and modes differ, unlinking controller",
                                           outputs[j].connector_id, outputs[i].connector_id);
                                outputs[j].controller_id = 0;
                        }
                }
        }

        /* Step 4:
         * Assign controllers to outputs without a controller
         */
        number_of_setup_outputs = count_setup_controllers (outputs, outputs_len);
        if (number_of_setup_outputs != backend->connected_count) {
                /* First try, try to assign controllers to outputs without one */
                ply_trace ("Some outputs don't have controllers, picking controllers");
                outputs = setup_outputs (backend, outputs, outputs_len);
                number_of_setup_outputs = count_setup_controllers (outputs, outputs_len);
        }
        /* Try again if necessary, re-assing controllers for all outputs.
         * Note this is skipped when processing change events, as we don't
         * want to mess with the controller assignment of already lit monitors
         * in that case.
         */
        if (!change && number_of_setup_outputs != backend->connected_count) {
                ply_trace ("Some outputs still don't have controllers, re-assigning controllers for all outputs");
                for (i = 0; i < outputs_len; i++) {
                        if (outputs[i].uses_hw_rotation)
                                continue; /* Do not re-assign hw-rotated outputs */
                        outputs[i].controller_id = 0;
                }
                outputs = setup_outputs (backend, outputs, outputs_len);
        }
        for (i = 0; i < outputs_len; i++) {
                ply_trace ("Using controller %u for connector %u",
                           outputs[i].controller_id, outputs[i].connector_id);
        }

        tile_outputs = calloc (outputs_len, sizeof(*tile_outputs));
        tile_geometries = calloc (outputs_len, sizeof(*tile_geometries));
        complete_tile_groups = calloc (outputs_len, sizeof(*complete_tile_groups));
        if (outputs_len > 0 &&
            (tile_outputs == NULL || tile_geometries == NULL || complete_tile_groups == NULL)) {
                free (complete_tile_groups);
                free (tile_geometries);
                free (tile_outputs);
                free (outputs);
                return false;
        }

        for (i = 0; i < outputs_len; i++) {
                tile_outputs[i].tile.group_id = outputs[i].tile_group_id;
                tile_outputs[i].tile.is_single_monitor = outputs[i].tile_is_single_monitor;
                tile_outputs[i].tile.num_h = outputs[i].tile_num_h;
                tile_outputs[i].tile.num_v = outputs[i].tile_num_v;
                tile_outputs[i].tile.h_loc = outputs[i].tile_h_loc;
                tile_outputs[i].tile.v_loc = outputs[i].tile_v_loc;
                tile_outputs[i].tile.h_size = outputs[i].tile_h_size;
                tile_outputs[i].tile.v_size = outputs[i].tile_v_size;
                tile_outputs[i].mode = outputs[i].mode;
                tile_outputs[i].controller_id = outputs[i].controller_id;
                tile_outputs[i].device_scale = outputs[i].device_scale;
                tile_outputs[i].tiled = outputs[i].tiled;
                tile_outputs[i].connected = outputs[i].connected;
                tile_outputs[i].upright_rotation = outputs[i].rotation == PLY_PIXEL_BUFFER_ROTATE_UPRIGHT;
                tile_outputs[i].uses_hw_rotation = outputs[i].uses_hw_rotation;
        }

        for (i = 0; i < outputs_len; i++) {
                complete_tile_groups[i] = ply_renderer_drm_tile_group_get_geometry (
                        tile_outputs, outputs_len, i,
                        backend->resources->max_width,
                        backend->resources->max_height,
                        &tile_geometries[i]);
        }

        update_panel_info (backend, outputs, complete_tile_groups,
                           tile_geometries, outputs_len);

        /* Step 5:
         * Create one logical head for every complete tiled monitor, and one
         * head per controller for all other outputs.
         */
        for (i = 0; i < outputs_len; i++) {
                ply_renderer_head_t *head;
                uint32_t controller_id;
                uint32_t console_buffer_id;
                uint32_t width, height, x = 0, y = 0;
                int gamma_size;
                bool complete_tile_group;

                if (!outputs[i].controller_id)
                        continue;

                if (!get_controller_state (backend, &outputs[i],
                                           &console_buffer_id, &gamma_size))
                        continue;

                controller_id = outputs[i].controller_id;
                complete_tile_group = complete_tile_groups[i];
                width = outputs[i].mode.hdisplay;
                height = outputs[i].mode.vdisplay;
                if (complete_tile_group) {
                        width = tile_geometries[i].width;
                        height = tile_geometries[i].height;
                        x = tile_geometries[i].x;
                        y = tile_geometries[i].y;
                }

                head = ply_hashtable_lookup (backend->heads_by_controller_id,
                                             (void *) (intptr_t) controller_id);

                if (head == NULL && complete_tile_group) {
                        head = find_existing_head_for_tile_group (backend, outputs, i);
                }

                if (head != NULL &&
                    (head->area.width != width || head->area.height != height)) {
                        ply_renderer_head_remove (backend, head);
                        head = NULL;
                        changed = true;
                }

                if (head == NULL && complete_tile_group) {
                        head = find_existing_head_for_tile_group (backend, outputs, i);
                }

                if (head == NULL) {
                        if (complete_tile_group) {
                                ply_trace ("Creating %ux%u logical head for TILE group %u",
                                           width, height, outputs[i].tile_group_id);
                        } else if (outputs[i].tiled) {
                                ply_trace ("TILE group %u is incomplete or incompatible; creating an independent head",
                                           outputs[i].tile_group_id);
                        }

                        head = ply_renderer_head_new (backend, &outputs[i],
                                                      console_buffer_id, gamma_size,
                                                      width, height, x, y);
                        changed = true;
                } else {
                        ply_renderer_controller_t **controllers;
                        ply_renderer_controller_t *head_controller = NULL;
                        int k;

                        controllers = (ply_renderer_controller_t **) ply_array_get_pointer_elements (head->controllers);
                        for (k = 0; k < ply_array_get_size (head->controllers); k++) {
                                if (controllers[k]->controller_id == controller_id) {
                                        head_controller = controllers[k];
                                        break;
                                }
                        }

                        if (head_controller != NULL) {
                                if (ply_renderer_controller_add_connector (head_controller, &outputs[i]))
                                        changed = true;
                        } else if (complete_tile_group) {
                                ply_renderer_head_add_controller (backend, head, &outputs[i],
                                                                  console_buffer_id, gamma_size,
                                                                  x, y);
                                changed = true;
                        }
                }
        }

        backend->outputs_len = outputs_len;
        backend->outputs = outputs;

        free (complete_tile_groups);
        free (tile_geometries);
        free (tile_outputs);

        ply_trace ("outputs %schanged\n", changed ? "" : "un");

        return changed;
}

static bool
has_32bpp_support (ply_renderer_backend_t *backend)
{
        uint32_t buffer_id;
        unsigned long row_stride;
        uint32_t min_width;
        uint32_t min_height;

        min_width = backend->resources->min_width;
        min_height = backend->resources->min_height;

        /* Some backends set min_width/min_height to 0,
         * but 0x0 sized buffers don't work.
         */
        if (min_width == 0)
                min_width = 1;

        if (min_height == 0)
                min_height = 1;

        buffer_id = create_output_buffer (backend, min_width, min_height, &row_stride);

        if (buffer_id == 0) {
                ply_trace ("Could not create minimal (%ux%u) 32bpp dummy buffer",
                           backend->resources->min_width,
                           backend->resources->min_height);
                return false;
        }

        destroy_output_buffer (backend, buffer_id);

        return true;
}

static bool
query_device (ply_renderer_backend_t *backend,
              bool                    force)
{
        bool ret = true;

        assert (backend != NULL);
        assert (backend->device_fd >= 0);

        backend->resources = drmModeGetResources (backend->device_fd);

        if (backend->resources == NULL) {
                ply_trace ("Could not get card resources");
                return false;
        }

        if (!create_heads_for_active_connectors (backend, force, false)) {
                ply_trace ("Could not initialize heads");
                ret = false;
        } else if (!has_32bpp_support (backend)) {
                ply_trace ("Device doesn't support 32bpp framebuffer");
                ret = false;
        }

        drmModeFreeResources (backend->resources);
        backend->resources = NULL;

        return ret;
}

static bool
handle_change_event (ply_renderer_backend_t *backend)
{
        bool ret = true;

        backend->resources = drmModeGetResources (backend->device_fd);
        if (backend->resources == NULL) {
                ply_trace ("Could not get card resources for change event");
                return false;
        }

        ret = create_heads_for_active_connectors (backend, true, true);

        drmModeFreeResources (backend->resources);
        backend->resources = NULL;

        return ret;
}

static bool
map_to_device (ply_renderer_backend_t *backend)
{
        ply_renderer_head_t *head;
        ply_list_node_t *node;
        bool head_mapped;

        head_mapped = false;
        node = ply_list_get_first_node (backend->heads);
        while (node != NULL) {
                head = (ply_renderer_head_t *) ply_list_node_get_data (node);

                if (ply_renderer_head_map (backend, head))
                        head_mapped = true;

                node = ply_list_get_next_node (backend->heads, node);
        }

        if (backend->terminal != NULL) {
                if (ply_terminal_is_active (backend->terminal))
                        activate (backend);
                else
                        ply_terminal_activate_vt (backend->terminal);
        } else {
                activate (backend);
        }

        return head_mapped;
}

static void
unmap_from_device (ply_renderer_backend_t *backend)
{
        ply_renderer_head_t *head;
        ply_list_node_t *node;

        node = ply_list_get_first_node (backend->heads);
        while (node != NULL) {
                head = (ply_renderer_head_t *) ply_list_node_get_data (node);
                ply_renderer_head_unmap (backend, head);
                node = ply_list_get_next_node (backend->heads, node);
        }
}

static bool
reset_scan_out_buffer_if_needed (ply_renderer_backend_t *backend,
                                 ply_renderer_head_t    *head)
{
        ply_renderer_controller_t **controllers;
        bool did_reset = false;
        int i;

        if (backend->terminal != NULL)
                if (!ply_terminal_is_active (backend->terminal))
                        return false;

        controllers = (ply_renderer_controller_t **) ply_array_get_pointer_elements (head->controllers);
        for (i = 0; i < ply_array_get_size (head->controllers); i++) {
                drmModeCrtc *crtc;

                if (controllers[i]->scan_out_buffer_needs_reset) {
                        bool success;

                        success = ply_renderer_controller_set_scan_out_buffer (backend,
                                                                               controllers[i],
                                                                               head->scan_out_buffer_id);
                        controllers[i]->scan_out_buffer_needs_reset = !success;
                        did_reset = true;
                        continue;
                }

                crtc = drmModeGetCrtc (backend->device_fd, controllers[i]->controller_id);
                if (crtc == NULL)
                        continue;

                if (crtc->buffer_id != head->scan_out_buffer_id ||
                    crtc->x != controllers[i]->x ||
                    crtc->y != controllers[i]->y) {
                        ply_renderer_controller_set_scan_out_buffer (backend, controllers[i],
                                                                     head->scan_out_buffer_id);
                        did_reset = true;
                }
                drmModeFreeCrtc (crtc);
        }

        return did_reset;
}

static void
flush_head (ply_renderer_backend_t *backend,
            ply_renderer_head_t    *head)
{
        ply_rectangle_t *area_to_flush;
        ply_region_t *updated_region;
        ply_list_t *areas_to_flush;
        ply_list_node_t *node;
        ply_pixel_buffer_t *pixel_buffer;
        char *map_address;
        bool dirty = false;
        static enum { PLY_SET_MODE_ON_REDRAWS_UNKNOWN = -1,
                      PLY_SET_MODE_ON_REDRAWS_DISABLED,
                      PLY_SET_MODE_ON_REDRAWS_ENABLED } set_mode_on_redraws = PLY_SET_MODE_ON_REDRAWS_UNKNOWN;

        assert (backend != NULL);

        if (set_mode_on_redraws == PLY_SET_MODE_ON_REDRAWS_UNKNOWN) {
                if (ply_kernel_command_line_has_argument ("plymouth.set-mode-on-redraws")) {
                        ply_trace ("Mode getting reset every redraw");
                        set_mode_on_redraws = PLY_SET_MODE_ON_REDRAWS_ENABLED;
                } else {
                        set_mode_on_redraws = PLY_SET_MODE_ON_REDRAWS_DISABLED;
                }
        }

        if (!backend->is_active)
                return;

        if (backend->terminal != NULL) {
                ply_terminal_set_mode (backend->terminal, PLY_TERMINAL_MODE_GRAPHICS);

                if (using_input_device (&backend->input_source)) {
                        ply_terminal_set_disabled_input (backend->terminal);
                } else {
                        ply_terminal_set_unbuffered_input (backend->terminal);
                }
        }
        pixel_buffer = head->pixel_buffer;
        updated_region = ply_pixel_buffer_get_updated_areas (pixel_buffer);
        areas_to_flush = ply_region_get_sorted_rectangle_list (updated_region);

        /* A hotplugged head may not be mapped yet, map it now. */
        if (!head->scan_out_buffer_id) {
                if (!ply_renderer_head_map (backend, head))
                        return;
        }

        map_address = begin_flush (backend, head->scan_out_buffer_id);

        node = ply_list_get_first_node (areas_to_flush);
        while (node != NULL) {
                area_to_flush = (ply_rectangle_t *) ply_list_node_get_data (node);

                ply_renderer_head_flush_area (head, area_to_flush, map_address);
                dirty = true;

                node = ply_list_get_next_node (areas_to_flush, node);
        }

        if (set_mode_on_redraws == PLY_SET_MODE_ON_REDRAWS_ENABLED) {
                ply_renderer_controller_t **controllers;
                int i;

                dirty = true;
                controllers = (ply_renderer_controller_t **) ply_array_get_pointer_elements (head->controllers);
                for (i = 0; i < ply_array_get_size (head->controllers); i++) {
                        controllers[i]->scan_out_buffer_needs_reset = true;
                }
        }

        if (dirty) {
                if (reset_scan_out_buffer_if_needed (backend, head))
                        ply_trace ("Needed to reset scan out buffer on %ldx%ld renderer head",
                                   head->area.width, head->area.height);

                end_flush (backend, head->scan_out_buffer_id);
        }

        ply_region_clear (updated_region);
}

static ply_list_t *
get_heads (ply_renderer_backend_t *backend)
{
        return backend->heads;
}

static ply_pixel_buffer_t *
get_buffer_for_head (ply_renderer_backend_t *backend,
                     ply_renderer_head_t    *head)
{
        if (head->backend != backend)
                return NULL;

        return head->pixel_buffer;
}

static bool
has_input_source (ply_renderer_backend_t      *backend,
                  ply_renderer_input_source_t *input_source)
{
        return input_source == &backend->input_source;
}

static ply_renderer_input_source_t *
get_input_source (ply_renderer_backend_t *backend)
{
        return &backend->input_source;
}

static void
on_terminal_key_event (ply_renderer_input_source_t *input_source)
{
        ply_renderer_backend_t *backend = input_source->backend;
        int terminal_fd;

        if (using_input_device (input_source))
                return;

        terminal_fd = ply_terminal_get_fd (backend->terminal);

        ply_buffer_append_from_fd (input_source->key_buffer,
                                   terminal_fd);

        if (input_source->handler != NULL)
                input_source->handler (input_source->user_data, input_source->key_buffer, input_source);
}

static ply_input_device_input_result_t
on_input_device_key (ply_renderer_input_source_t *input_source,
                     ply_input_device_t          *input_device,
                     const char                  *text)
{
        ply_buffer_append_bytes (input_source->key_buffer, text, strlen (text));

        if (input_source->handler == NULL)
                return PLY_INPUT_RESULT_PROPAGATED;

        input_source->handler (input_source->user_data, input_source->key_buffer, input_source);

        return PLY_INPUT_RESULT_CONSUMED;
}

static void
on_input_leds_changed (ply_renderer_input_source_t *input_source,
                       ply_input_device_t          *input_device)
{
        ply_list_node_t *node;

        ply_list_foreach (input_source->input_devices, node) {
                ply_input_device_t *update_input_device = ply_list_node_get_data (node);
                ply_input_device_update_leds (update_input_device);
        }
}

static void
on_input_source_disconnected (ply_renderer_input_source_t *input_source)
{
        ply_trace ("input source disconnected, reopening");

        open_input_source (input_source->backend, input_source);
}

static bool
using_input_device (ply_renderer_input_source_t *input_source)
{
        return ply_list_get_length (input_source->input_devices) > 0;
}

static void
watch_input_device (ply_renderer_backend_t *backend,
                    ply_input_device_t     *input_device)
{
        ply_trace ("Listening for keys from device '%s'", ply_input_device_get_name (input_device));

        ply_input_device_watch_for_input (input_device,
                                          (ply_input_device_input_handler_t) on_input_device_key,
                                          (ply_input_device_leds_changed_handler_t) on_input_leds_changed,
                                          &backend->input_source);

        if (backend->terminal != NULL)
                ply_terminal_set_disabled_input (backend->terminal);
}

static void
watch_input_devices (ply_renderer_backend_t *backend)
{
        ply_list_node_t *node;
        ply_renderer_input_source_t *input_source = &backend->input_source;

        ply_list_foreach (input_source->input_devices, node) {
                ply_input_device_t *input_device = ply_list_node_get_data (node);

                watch_input_device (backend, input_device);
        }
}

static bool
open_input_source (ply_renderer_backend_t      *backend,
                   ply_renderer_input_source_t *input_source)
{
        assert (backend != NULL);
        assert (has_input_source (backend, input_source));

        if (!backend->input_source_is_open)
                watch_input_devices (backend);

        if (backend->terminal != NULL && ply_terminal_get_fd (backend->terminal) >= 0) {
                input_source->terminal_input_watch = ply_event_loop_watch_fd (backend->loop,
                                                                              ply_terminal_get_fd (backend->terminal),
                                                                              PLY_EVENT_LOOP_FD_STATUS_HAS_DATA,
                                                                              (ply_event_handler_t) on_terminal_key_event,
                                                                              (ply_event_handler_t)
                                                                              on_input_source_disconnected, input_source);
        }

        input_source->backend = backend;
        backend->input_source_is_open = true;

        return true;
}

static void
set_handler_for_input_source (ply_renderer_backend_t             *backend,
                              ply_renderer_input_source_t        *input_source,
                              ply_renderer_input_source_handler_t handler,
                              void                               *user_data)
{
        assert (backend != NULL);
        assert (has_input_source (backend, input_source));

        input_source->handler = handler;
        input_source->user_data = user_data;
}

static void
close_input_source (ply_renderer_backend_t      *backend,
                    ply_renderer_input_source_t *input_source)
{
        assert (backend != NULL);
        assert (has_input_source (backend, input_source));

        if (!backend->input_source_is_open)
                return;

        if (using_input_device (input_source)) {
                ply_list_node_t *node;
                ply_list_foreach (input_source->input_devices, node) {
                        ply_input_device_t *input_device = ply_list_node_get_data (node);
                        ply_input_device_stop_watching_for_input (input_device,
                                                                  (ply_input_device_input_handler_t) on_input_device_key,
                                                                  (ply_input_device_leds_changed_handler_t) on_input_leds_changed,
                                                                  &backend->input_source);
                }
                if (backend->terminal != NULL)
                        ply_terminal_set_unbuffered_input (backend->terminal);
        }

        if (input_source->terminal_input_watch != NULL) {
                ply_event_loop_stop_watching_fd (backend->loop, input_source->terminal_input_watch);
                input_source->terminal_input_watch = NULL;
        }

        input_source->backend = NULL;

        backend->input_source_is_open = false;
}

static bool
get_panel_properties (ply_renderer_backend_t      *backend,
                      int                         *width,
                      int                         *height,
                      ply_pixel_buffer_rotation_t *rotation,
                      int                         *scale)
{
        if (!backend->panel_width)
                return false;

        *width = backend->panel_width;
        *height = backend->panel_height;
        *rotation = backend->panel_rotation;
        *scale = backend->panel_scale;
        return true;
}

static ply_input_device_t *
get_any_input_device (ply_renderer_backend_t *backend)
{
        ply_list_node_t *node = ply_list_get_first_node (backend->input_source.input_devices);

        if (node != NULL)
                return ply_list_node_get_data (node);

        return NULL;
}

static bool
get_capslock_state (ply_renderer_backend_t *backend)
{
        if (using_input_device (&backend->input_source)) {
                ply_input_device_t *dev = get_any_input_device (backend);
                if (!dev)
                        return false;

                return ply_input_device_get_capslock_state (dev);
        }
        if (!backend->local_console_terminal)
                return false;

        return ply_terminal_get_capslock_state (backend->local_console_terminal);
}

static const char *
get_keymap (ply_renderer_backend_t *backend)
{
        if (using_input_device (&backend->input_source)) {
                const char *keymap;
                ply_input_device_t *dev = get_any_input_device (backend);
                if (!dev)
                        return NULL;

                keymap = ply_input_device_get_keymap (dev);
                if (keymap != NULL) {
                        return keymap;
                }
        }
        if (!backend->local_console_terminal)
                return NULL;

        return ply_terminal_get_keymap (backend->local_console_terminal);
}

static const char *
get_keyboard_layout (ply_renderer_backend_t *backend)
{
        ply_input_device_t *dev = get_any_input_device (backend);

        if (!dev)
                return NULL;

        return ply_input_device_get_keyboard_layout (dev);
}

static void
add_input_device (ply_renderer_backend_t *backend,
                  ply_input_device_t     *input_device)
{
        ply_list_append_data (backend->input_source.input_devices, input_device);

        if (backend->input_source_is_open)
                watch_input_device (backend, input_device);
}

static void
remove_input_device (ply_renderer_backend_t *backend,
                     ply_input_device_t     *input_device)
{
        ply_list_remove_data (backend->input_source.input_devices, input_device);
}

/* Called only on the renderer event loop, between frames. Scan-out dimensions
 * stay native while the existing pixel-buffer code rotates software drawing. */
bool
ply_renderer_backend_set_orientation (ply_renderer_backend_t     *backend,
                                      ply_pixel_buffer_rotation_t orientation)
{
        ply_list_node_t *node;
        bool changed = false;

        if (backend->outputs == NULL || !backend->is_active)
                return false;

        for (node = ply_list_get_first_node (backend->heads); node != NULL;
             node = ply_list_get_next_node (backend->heads, node)) {
                ply_renderer_head_t *head = ply_list_node_get_data (node);
                ply_pixel_buffer_rotation_t rotation = PLY_PIXEL_BUFFER_ROTATE_UPRIGHT;
                bool builtin = false;
                bool shared_with_external = false;
                int i;

                for (i = 0; i < backend->outputs_len; i++) {
                        ply_output_t *output = &backend->outputs[i];
                        ply_renderer_head_t *output_head;

                        if (!output->connected || output->controller_id == 0)
                                continue;
                        output_head = ply_hashtable_lookup (backend->heads_by_controller_id,
                                                            (void *) (intptr_t) output->controller_id);
                        if (output_head != head)
                                continue;
                        /* Firmware-backed simpledrm often calls the sole boot
                         * display "Unknown" before the native panel appears. */
                        if (output_is_builtin (output) ||
                            (backend->simpledrm && backend->connected_count == 1)) {
                                builtin = true;
                                rotation = output->rotation;
                        } else {
                                shared_with_external = true;
                        }
                }
                if (!builtin)
                        continue;
                if (!shared_with_external)
                        rotation = ply_orientation_compose (rotation, orientation);
                /* fbcon owns the text exposed when the splash releases its VT.
                 * Only rotate all consoles when this GPU has a single output,
                 * avoiding a global text rotation on a multi-monitor setup. */
                if (backend->connected_count == 1 &&
                    (!backend->console_rotation_valid || backend->console_rotation != rotation)) {
                        backend->console_rotation_valid = ply_orientation_update_console (
                                "/sys/class/graphics/fbcon/rotate_all", rotation);
                        if (backend->console_rotation_valid) {
                                backend->console_rotation = rotation;
                                ply_trace ("framebuffer console orientation changed to %d", rotation);
                        }
                }

                if (rotation == ply_pixel_buffer_get_device_rotation (head->pixel_buffer))
                        continue;

                ply_pixel_buffer_set_device_rotation (head->pixel_buffer, rotation);
                ply_pixel_buffer_fill_with_hex_color (head->pixel_buffer, NULL, 0xff000000);
                changed = true;
                ply_trace ("built-in splash orientation changed to %d", rotation);
        }
        return changed;
}

ply_renderer_plugin_interface_t *
ply_renderer_backend_get_interface (void)
{
        static ply_renderer_plugin_interface_t plugin_interface =
        {
                .create_backend               = create_backend,
                .destroy_backend              = destroy_backend,
                .open_device                  = open_device,
                .close_device                 = close_device,
                .query_device                 = query_device,
                .handle_change_event          = handle_change_event,
                .map_to_device                = map_to_device,
                .unmap_from_device            = unmap_from_device,
                .activate                     = activate,
                .deactivate                   = deactivate,
                .flush_head                   = flush_head,
                .get_heads                    = get_heads,
                .get_buffer_for_head          = get_buffer_for_head,
                .get_input_source             = get_input_source,
                .open_input_source            = open_input_source,
                .set_handler_for_input_source = set_handler_for_input_source,
                .close_input_source           = close_input_source,
                .get_device_name              = get_device_name,
                .get_panel_properties         = get_panel_properties,
                .get_capslock_state           = get_capslock_state,
                .get_keymap                   = get_keymap,
                .add_input_device             = add_input_device,
                .remove_input_device          = remove_input_device,
                .get_keyboard_layout          = get_keyboard_layout,
        };

        return &plugin_interface;
}
