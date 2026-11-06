/*
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 */

#include "ply-test.h"

#include <string.h>

#include "plugins/renderers/fake-renderer.h"
#include "ply-list.h"
#include "ply-pixel-buffer.h"
#include "ply-renderer-private.h"
#include "ply-utils.h"

static const test_renderer_plugin_state_t *
get_plugin_state (ply_module_handle_t *module)
{
        test_renderer_plugin_get_state_function_t get_state;

        get_state = (test_renderer_plugin_get_state_function_t)
                    ply_module_look_up_function (module,
                                                 "test_renderer_plugin_get_state");
        if (get_state == NULL)
                return NULL;

        return get_state ();
}

static void
on_input (void                        *user_data,
          ply_buffer_t                *key_buffer,
          ply_renderer_input_source_t *input_source)
{
        bool *called = user_data;

        if (key_buffer != NULL && input_source != NULL)
                *called = true;
}

static bool
test_renderer_delegates_backend_lifecycle (void)
{
        const test_renderer_plugin_state_t *state;
        ply_renderer_input_source_t *input_source;
        ply_renderer_head_t *head;
        ply_pixel_buffer_t *buffer;
        ply_module_handle_t *module;
        ply_renderer_t *renderer;
        ply_list_node_t *node;
        ply_rectangle_t size;
        int terminal_marker;
        int local_console_marker;
        int input_device_marker;
        int handler_marker;
        int width;
        int height;
        int scale;
        ply_pixel_buffer_rotation_t rotation;

        module = ply_open_module (TEST_RENDERER_PLUGIN_PATH);
        PLY_TEST_ASSERT (module != NULL);

        renderer = ply_renderer_new_with_plugin_directory (
                PLY_RENDERER_TYPE_FRAME_BUFFER,
                TEST_RENDERER_PLUGIN_DIR,
                "/dev/requested",
                (ply_terminal_t *) &terminal_marker,
                (ply_terminal_t *) &local_console_marker);
        PLY_TEST_ASSERT (renderer != NULL);
        PLY_TEST_ASSERT (ply_renderer_get_type (renderer) ==
                         PLY_RENDERER_TYPE_FRAME_BUFFER);
        PLY_TEST_ASSERT (ply_renderer_open (renderer, true));
        PLY_TEST_ASSERT (ply_renderer_is_active (renderer));
        PLY_TEST_ASSERT (strcmp (ply_renderer_get_device_name (renderer),
                                 "/dev/fake-renderer") == 0);

        state = get_plugin_state (module);
        PLY_TEST_ASSERT (state != NULL);
        PLY_TEST_ASSERT (state->create_count == 1);
        PLY_TEST_ASSERT (state->open_count == 1);
        PLY_TEST_ASSERT (state->query_count == 1);
        PLY_TEST_ASSERT (state->query_force);
        PLY_TEST_ASSERT (strcmp (state->requested_device, "/dev/requested") == 0);
        PLY_TEST_ASSERT (state->terminal == (ply_terminal_t *) &terminal_marker);
        PLY_TEST_ASSERT (state->local_console_terminal ==
                         (ply_terminal_t *) &local_console_marker);

        node = ply_list_get_first_node (ply_renderer_get_heads (renderer));
        PLY_TEST_ASSERT (node != NULL);
        head = ply_list_node_get_data (node);
        buffer = ply_renderer_get_buffer_for_head (renderer, head);
        PLY_TEST_ASSERT (buffer != NULL);
        ply_pixel_buffer_get_size (buffer, &size);
        PLY_TEST_ASSERT (size.width == 80);
        PLY_TEST_ASSERT (size.height == 50);

        ply_renderer_flush_head (renderer, head);
        ply_renderer_flush_head (renderer, head);
        PLY_TEST_ASSERT (state->map_count == 1);
        PLY_TEST_ASSERT (state->flush_count == 2);
        PLY_TEST_ASSERT (ply_renderer_handle_change_event (renderer));
        PLY_TEST_ASSERT (state->change_count == 1);

        ply_renderer_deactivate (renderer);
        PLY_TEST_ASSERT (!ply_renderer_is_active (renderer));
        ply_renderer_activate (renderer);
        PLY_TEST_ASSERT (ply_renderer_is_active (renderer));
        PLY_TEST_ASSERT (state->deactivate_count == 1);
        PLY_TEST_ASSERT (state->activate_count == 1);

        input_source = ply_renderer_get_input_source (renderer);
        PLY_TEST_ASSERT (input_source != NULL);
        PLY_TEST_ASSERT (ply_renderer_open_input_source (renderer, input_source));
        ply_renderer_set_handler_for_input_source (renderer,
                                                   input_source,
                                                   on_input,
                                                   &handler_marker);
        PLY_TEST_ASSERT (state->input_handler == on_input);
        PLY_TEST_ASSERT (state->input_handler_user_data == &handler_marker);
        ply_renderer_close_input_source (renderer, input_source);
        ply_renderer_close_input_source (renderer, input_source);
        PLY_TEST_ASSERT (state->input_source_count == 1);
        PLY_TEST_ASSERT (state->open_input_count == 1);
        PLY_TEST_ASSERT (state->set_input_handler_count == 1);
        PLY_TEST_ASSERT (state->close_input_count == 1);

        PLY_TEST_ASSERT (ply_renderer_get_panel_properties (renderer,
                                                            &width,
                                                            &height,
                                                            &rotation,
                                                            &scale));
        PLY_TEST_ASSERT (width == 1024);
        PLY_TEST_ASSERT (height == 768);
        PLY_TEST_ASSERT (rotation == PLY_PIXEL_BUFFER_ROTATE_UPRIGHT);
        PLY_TEST_ASSERT (scale == 2);
        PLY_TEST_ASSERT (ply_renderer_get_capslock_state (renderer));
        PLY_TEST_ASSERT (strcmp (ply_renderer_get_keymap (renderer), "us") == 0);
        PLY_TEST_ASSERT (strcmp (ply_renderer_get_keyboard_layout (renderer), "Russian") == 0);
        PLY_TEST_ASSERT (state->panel_count == 1);
        PLY_TEST_ASSERT (state->capslock_count == 1);
        PLY_TEST_ASSERT (state->keymap_count == 1);
        PLY_TEST_ASSERT (state->keyboard_layout_count == 1);

        ply_renderer_add_input_device (
                renderer,
                (ply_input_device_t *) &input_device_marker);
        ply_renderer_remove_input_device (
                renderer,
                (ply_input_device_t *) &input_device_marker);
        PLY_TEST_ASSERT (state->add_input_device_count == 1);
        PLY_TEST_ASSERT (state->remove_input_device_count == 1);

        PLY_TEST_ASSERT (ply_renderer_set_orientation (renderer, PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE));
        ply_pixel_buffer_get_size (buffer, &size);
        PLY_TEST_ASSERT (size.width == 50 && size.height == 80);
        PLY_TEST_ASSERT (!ply_renderer_set_orientation (renderer, PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE));
        ply_renderer_deactivate (renderer);
        PLY_TEST_ASSERT (!ply_renderer_set_orientation (renderer, PLY_PIXEL_BUFFER_ROTATE_UPRIGHT));
        PLY_TEST_ASSERT (ply_pixel_buffer_get_device_rotation (buffer) == PLY_PIXEL_BUFFER_ROTATE_CLOCKWISE);
        ply_renderer_activate (renderer);
        PLY_TEST_ASSERT (ply_renderer_set_orientation (renderer, PLY_PIXEL_BUFFER_ROTATE_UPRIGHT));

        ply_renderer_close (renderer);
        PLY_TEST_ASSERT (!ply_renderer_is_active (renderer));
        PLY_TEST_ASSERT (state->unmap_count == 1);
        PLY_TEST_ASSERT (state->close_count == 1);
        ply_renderer_free (renderer);
        PLY_TEST_ASSERT (state->destroy_count == 1);

        ply_close_module (module);
        return true;
}

static const ply_test_case_t test_cases[] =
{
        PLY_TEST_CASE (test_renderer_delegates_backend_lifecycle),
};

PLY_TEST_MAIN (test_cases)
