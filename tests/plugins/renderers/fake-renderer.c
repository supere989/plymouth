/*
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include "fake-renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ply-list.h"
#include "ply-pixel-buffer.h"
#include "ply-renderer-plugin.h"

struct _ply_renderer_head
{
        int marker;
};

struct _ply_renderer_input_source
{
        int marker;
};

struct _ply_renderer_backend
{
        ply_list_t                 *heads;
        ply_pixel_buffer_t         *buffer;
        ply_renderer_head_t         head;
        ply_renderer_input_source_t input_source;
};

static test_renderer_plugin_state_t state;

bool
ply_renderer_backend_set_orientation (ply_renderer_backend_t     *backend,
                                      ply_pixel_buffer_rotation_t orientation)
{
        if (ply_pixel_buffer_get_device_rotation (backend->buffer) == orientation)
                return false;
        ply_pixel_buffer_set_device_rotation (backend->buffer, orientation);
        return true;
}

const test_renderer_plugin_state_t *
test_renderer_plugin_get_state (void)
{
        return &state;
}

static ply_renderer_backend_t *
create_backend (const char     *device_name,
                ply_terminal_t *terminal,
                ply_terminal_t *local_console_terminal)
{
        ply_renderer_backend_t *backend;

        memset (&state, 0, sizeof(state));
        state.create_count++;
        state.terminal = terminal;
        state.local_console_terminal = local_console_terminal;
        if (device_name != NULL) {
                snprintf (state.requested_device,
                          sizeof(state.requested_device),
                          "%s",
                          device_name);
        }

        backend = calloc (1, sizeof(ply_renderer_backend_t));
        backend->heads = ply_list_new ();
        backend->buffer = ply_pixel_buffer_new (80, 50);
        backend->head.marker = 1;
        backend->input_source.marker = 2;
        ply_list_append_data (backend->heads, &backend->head);

        return backend;
}

static void
destroy_backend (ply_renderer_backend_t *backend)
{
        state.destroy_count++;
        ply_pixel_buffer_free (backend->buffer);
        ply_list_free (backend->heads);
        free (backend);
}

static bool
open_device (ply_renderer_backend_t *backend)
{
        state.open_count += backend->head.marker;
        return true;
}

static void
close_device (ply_renderer_backend_t *backend)
{
        state.close_count += backend->head.marker;
}

static bool
query_device (ply_renderer_backend_t *backend,
              bool                    force)
{
        state.query_count += backend->head.marker;
        state.query_force = force;
        return true;
}

static bool
handle_change_event (ply_renderer_backend_t *backend)
{
        state.change_count += backend->head.marker;
        return true;
}

static bool
map_to_device (ply_renderer_backend_t *backend)
{
        state.map_count += backend->head.marker;
        return true;
}

static void
unmap_from_device (ply_renderer_backend_t *backend)
{
        state.unmap_count += backend->head.marker;
}

static void
activate (ply_renderer_backend_t *backend)
{
        state.activate_count += backend->head.marker;
}

static void
deactivate (ply_renderer_backend_t *backend)
{
        state.deactivate_count += backend->head.marker;
}

static void
flush_head (ply_renderer_backend_t *backend,
            ply_renderer_head_t    *head)
{
        if (head == &backend->head)
                state.flush_count++;
}

static ply_list_t *
get_heads (ply_renderer_backend_t *backend)
{
        state.heads_count++;
        return backend->heads;
}

static ply_pixel_buffer_t *
get_buffer_for_head (ply_renderer_backend_t *backend,
                     ply_renderer_head_t    *head)
{
        if (head == &backend->head)
                state.buffer_count++;
        return backend->buffer;
}

static ply_renderer_input_source_t *
get_input_source (ply_renderer_backend_t *backend)
{
        state.input_source_count++;
        return &backend->input_source;
}

static bool
open_input_source (ply_renderer_backend_t      *backend,
                   ply_renderer_input_source_t *input_source)
{
        if (input_source == &backend->input_source)
                state.open_input_count++;
        return true;
}

static void
set_handler_for_input_source (ply_renderer_backend_t             *backend,
                              ply_renderer_input_source_t        *input_source,
                              ply_renderer_input_source_handler_t handler,
                              void                               *user_data)
{
        if (input_source == &backend->input_source)
                state.set_input_handler_count++;
        state.input_handler = handler;
        state.input_handler_user_data = user_data;
}

static void
close_input_source (ply_renderer_backend_t      *backend,
                    ply_renderer_input_source_t *input_source)
{
        if (input_source == &backend->input_source)
                state.close_input_count++;
}

static const char *
get_device_name (ply_renderer_backend_t *backend)
{
        if (backend->head.marker == 1)
                return "/dev/fake-renderer";
        return NULL;
}

static bool
get_panel_properties (ply_renderer_backend_t      *backend,
                      int                         *width,
                      int                         *height,
                      ply_pixel_buffer_rotation_t *rotation,
                      int                         *scale)
{
        state.panel_count += backend->head.marker;
        *width = 1024;
        *height = 768;
        *rotation = PLY_PIXEL_BUFFER_ROTATE_UPRIGHT;
        *scale = 2;
        return true;
}

static bool
get_capslock_state (ply_renderer_backend_t *backend)
{
        state.capslock_count += backend->head.marker;
        return true;
}

static const char *
get_keymap (ply_renderer_backend_t *backend)
{
        state.keymap_count += backend->head.marker;
        return "us";
}

static const char *
get_keyboard_layout (ply_renderer_backend_t *backend)
{
        state.keyboard_layout_count += backend->head.marker;
        return "Russian";
}

static void
add_input_device (ply_renderer_backend_t *backend,
                  ply_input_device_t     *input_device)
{
        state.add_input_device_count += backend->head.marker;
        state.input_device = input_device;
}

static void
remove_input_device (ply_renderer_backend_t *backend,
                     ply_input_device_t     *input_device)
{
        if (input_device == state.input_device)
                state.remove_input_device_count += backend->head.marker;
}

const ply_renderer_plugin_interface_t *
ply_renderer_backend_get_interface (void)
{
        static const ply_renderer_plugin_interface_t interface = {
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
                .get_keyboard_layout          = get_keyboard_layout,
                .add_input_device             = add_input_device,
                .remove_input_device          = remove_input_device,
        };

        return &interface;
}
