libply_test_sources = {
  'array': 'test-array.c',
  'bitarray': 'test-bitarray.c',
  'buffer': 'test-buffer.c',
  'command-parser': 'test-command-parser.c',
  'event-loop': 'test-event-loop.c',
  'hashtable': 'test-hashtable.c',
  'key-file': 'test-key-file.c',
  'list': 'test-list.c',
  'logger': 'test-logger.c',
  'rectangle': 'test-rectangle.c',
  'region': 'test-region.c',
  'terminal-session': 'test-terminal-session.c',
  'trigger': 'test-trigger.c',
  'utils': 'test-utils.c',
}

test_c_args = [
  '-Wall',
  '-Wextra',
  '-Werror',
  '-Wno-unused-parameter',
]

test_environment = environment()
test_environment.set('LC_ALL', 'C')
test_timeout = 30

subdir('plugins')

orientation_test = executable(
  'test-orientation',
  'test-orientation.c',
  dependencies: [libply_dep, libply_splash_core_dep],
  c_args: test_c_args,
  include_directories: include_directories('.'),
)
test('splash-core-orientation', orientation_test,
  env: test_environment, protocol: 'tap', suite: ['unit', 'splash-core'])

foreach test_name, test_source : libply_test_sources
  test_executable = executable(
    'test-' + test_name,
    test_source,
    c_args: test_c_args,
    dependencies: libply_dep,
    include_directories: include_directories('.'),
  )

  test(
    'libply-' + test_name,
    test_executable,
    env: test_environment,
    protocol: 'tap',
    suite: ['unit', 'libply'],
    timeout: test_timeout,
  )
endforeach

command_parser_fuzz_executable = executable(
  'fuzz-command-parser',
  'fuzz-command-parser.c',
  c_args: test_c_args,
  dependencies: libply_dep,
  include_directories: include_directories('.'),
)

test(
  'fuzz-command-parser',
  command_parser_fuzz_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['fuzz', 'libply'],
  timeout: test_timeout,
)

key_file_fuzz_executable = executable(
  'fuzz-key-file',
  'fuzz-key-file.c',
  c_args: test_c_args,
  dependencies: libply_dep,
  include_directories: include_directories('.'),
)

test(
  'fuzz-key-file',
  key_file_fuzz_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['fuzz', 'libply'],
  timeout: test_timeout,
)

progress_test_executable = executable(
  'test-progress',
  'test-progress.c',
  c_args: test_c_args,
  dependencies: ply_progress_dep,
  include_directories: include_directories('.'),
)

test(
  'libply-progress',
  progress_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'libply'],
  timeout: test_timeout,
)

secure_boot_test_executable = executable(
  'test-secure-boot',
  'test-secure-boot.c',
  c_args: test_c_args,
  dependencies: [libply_dep, ply_secure_boot_dep],
  include_directories: include_directories('.'),
)

test(
  'libply-secure-boot',
  secure_boot_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'libply'],
  timeout: test_timeout,
)

boot_client_test_executable = executable(
  'test-boot-client',
  'test-boot-client.c',
  c_args: test_c_args,
  dependencies: [
    libply_dep,
    libply_boot_client_dep,
  ],
  include_directories: include_directories('.'),
)

test(
  'boot-client-protocol',
  boot_client_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'boot-client'],
  timeout: test_timeout,
)

boot_server_test_c_args = test_c_args + [
  '-Wno-cast-function-type',
]

boot_server_test_executable = executable(
  'test-boot-server',
  'test-boot-server.c',
  c_args: boot_server_test_c_args,
  dependencies: ply_boot_server_dep,
  include_directories: [
    include_directories('.'),
    include_directories('../src'),
  ],
)

test(
  'boot-server-protocol',
  boot_server_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'boot-server'],
  timeout: test_timeout,
)

plymouthd_policy_test_executable = executable(
  'test-plymouthd-policy',
  'test-plymouthd-policy.c',
  c_args: test_c_args,
  dependencies: plymouthd_policy_dep,
  include_directories: include_directories('.'),
)

test(
  'plymouthd-policy',
  plymouthd_policy_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_logging_test_executable = executable(
  'test-plymouthd-logging',
  'test-plymouthd-logging.c',
  c_args: test_c_args,
  dependencies: plymouthd_logging_dep,
  include_directories: include_directories('.'),
)

test(
  'plymouthd-logging',
  plymouthd_logging_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_session_test_executable = executable(
  'test-plymouthd-session',
  'test-plymouthd-session.c',
  c_args: test_c_args,
  dependencies: plymouthd_session_dep,
  include_directories: include_directories('.'),
)

test(
  'plymouthd-session',
  plymouthd_session_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_progress_test_executable = executable(
  'test-plymouthd-progress',
  'test-plymouthd-progress.c',
  c_args: test_c_args,
  dependencies: plymouthd_progress_dep,
  include_directories: [config_h_inc, include_directories('.')],
)

test(
  'plymouthd-progress',
  plymouthd_progress_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_diagnostics_test_executable = executable(
  'test-plymouthd-diagnostics',
  'test-plymouthd-diagnostics.c',
  c_args: test_c_args,
  dependencies: plymouthd_diagnostics_dep,
  include_directories: include_directories('.'),
)

test(
  'plymouthd-diagnostics',
  plymouthd_diagnostics_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_process_test_executable = executable(
  'test-plymouthd-process',
  'test-plymouthd-process.c',
  c_args: test_c_args,
  dependencies: plymouthd_private_dep,
  include_directories: include_directories('.'),
)

test(
  'plymouthd-process',
  plymouthd_process_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_transition_test_executable = executable(
  'test-plymouthd-transition',
  'test-plymouthd-transition.c',
  c_args: test_c_args,
  dependencies: plymouthd_transition_dep,
  include_directories: include_directories('.'),
)

test(
  'plymouthd-transition',
  plymouthd_transition_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_options_test_executable = executable(
  'test-plymouthd-options',
  'test-plymouthd-options.c',
  c_args: test_c_args,
  dependencies: plymouthd_options_dep,
  include_directories: include_directories('.'),
)

test(
  'plymouthd-options',
  plymouthd_options_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_settings_test_executable = executable(
  'test-plymouthd-settings',
  'test-plymouthd-settings.c',
  c_args: test_c_args,
  dependencies: plymouthd_settings_dep,
  include_directories: include_directories('.'),
)

test(
  'plymouthd-settings',
  plymouthd_settings_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_splash_delay_test_executable = executable(
  'test-plymouthd-splash-delay',
  'test-plymouthd-splash-delay.c',
  c_args: test_c_args,
  dependencies: [libply_dep, plymouthd_splash_delay_dep],
  include_directories: include_directories('.'),
)

test(
  'plymouthd-splash-delay',
  plymouthd_splash_delay_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

script_source_dir = meson.project_source_root() / 'src/plugins/splash/script'

script_scan_test_executable = executable(
  'test-script-scan',
  'test-script-scan.c',
  c_args: test_c_args,
  dependencies: script_engine_dep,
  include_directories: [
    include_directories('.'),
    include_directories('../src/plugins/splash/script'),
  ],
)

test(
  'script-scan',
  script_scan_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'script'],
  timeout: test_timeout,
)

script_scan_fuzz_executable = executable(
  'fuzz-script-scan',
  'fuzz-script-scan.c',
  c_args: test_c_args,
  dependencies: script_engine_dep,
  include_directories: [
    include_directories('.'),
    include_directories('../src/plugins/splash/script'),
  ],
)

test(
  'fuzz-script-scan',
  script_scan_fuzz_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['fuzz', 'script'],
  timeout: test_timeout,
)

script_execute_test_c_args = test_c_args + [
  '-DPLYMOUTH_SOURCE_ROOT="@0@"'.format(meson.project_source_root()),
]

script_execute_test_executable = executable(
  'test-script-execute',
  'test-script-execute.c',
  c_args: script_execute_test_c_args,
  dependencies: script_engine_dep,
  include_directories: [
    include_directories('.'),
    include_directories('../src/plugins/splash/script'),
  ],
)

test(
  'script-execute',
  script_execute_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'script'],
  timeout: test_timeout,
)

splash_core_test_sources = {
  'pixel-buffer': 'test-pixel-buffer.c',
  'rich-text': 'test-rich-text.c',
  'terminal-emulator': 'test-terminal-emulator.c',
}

foreach test_name, test_source : splash_core_test_sources
  test_executable = executable(
    'test-' + test_name,
    test_source,
    c_args: test_c_args,
    dependencies: libply_splash_core_dep,
    include_directories: include_directories('.'),
  )

  test(
    'splash-core-' + test_name,
    test_executable,
    env: test_environment,
    protocol: 'tap',
    suite: ['unit', 'splash-core'],
    timeout: test_timeout,
  )
endforeach

kmsg_reader_test_executable = executable(
  'test-kmsg-reader',
  [
    'test-kmsg-reader.c',
    '../src/libply-splash-core/ply-kmsg-reader.c',
  ],
  c_args: test_c_args + [
    '-Dopen=ply_test_kmsg_open',
    '-Dread=ply_test_kmsg_read',
    '-Wno-cast-function-type',
  ],
  dependencies: libply_dep,
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
  ],
)

test(
  'splash-core-kmsg-reader',
  kmsg_reader_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-core'],
  timeout: test_timeout,
)

terminal_emulator_fuzz_executable = executable(
  'fuzz-terminal-emulator',
  'fuzz-terminal-emulator.c',
  c_args: test_c_args,
  dependencies: libply_splash_core_dep,
  include_directories: include_directories('.'),
)

test(
  'fuzz-terminal-emulator',
  terminal_emulator_fuzz_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['fuzz', 'splash-core'],
  timeout: test_timeout,
)

image_test_executable = executable(
  'test-image',
  'test-image.c',
  c_args: test_c_args,
  dependencies: [
    libply_splash_core_dep,
    libply_splash_graphics_dep,
  ],
  include_directories: include_directories('.'),
)

test(
  'splash-graphics-image',
  image_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-graphics'],
  timeout: test_timeout,
)

image_fuzz_executable = executable(
  'fuzz-image',
  'fuzz-image.c',
  c_args: test_c_args,
  dependencies: [
    libply_splash_core_dep,
    libply_splash_graphics_dep,
  ],
  include_directories: include_directories('.'),
)

test(
  'fuzz-image',
  image_fuzz_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['fuzz', 'splash-graphics'],
  timeout: test_timeout,
)

vconsole_test_executable = executable(
  'test-vconsole',
  'test-vconsole.c',
  c_args: test_c_args,
  dependencies: ply_vconsole_dep,
  include_directories: include_directories('.'),
)

test(
  'splash-core-vconsole',
  vconsole_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-core'],
  timeout: test_timeout,
)

active_console_test_executable = executable(
  'test-active-console',
  'test-active-console.c',
  c_args: test_c_args,
  dependencies: ply_active_console_dep,
  include_directories: include_directories('.'),
)

test(
  'splash-core-active-console',
  active_console_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-core'],
  timeout: test_timeout,
)

animation_time_test_executable = executable(
  'test-animation-time',
  'test-animation-time.c',
  c_args: test_c_args,
  dependencies: [libply_dep, ply_animation_time_dep],
  include_directories: include_directories('.'),
)

test(
  'splash-graphics-animation-time',
  animation_time_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-graphics'],
  timeout: test_timeout,
)

bgrt_test_executable = executable(
  'test-bgrt',
  'test-bgrt.c',
  c_args: test_c_args,
  dependencies: ply_bgrt_dep,
  include_directories: include_directories('.'),
)

test(
  'splash-plugin-boot-logo-metadata',
  bgrt_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-plugin'],
  timeout: test_timeout,
)

two_step_test_theme_config = configuration_data()
two_step_test_theme_config.set(
  'IMAGE_DIR',
  meson.project_source_root() / 'themes/spinner',
)
configure_file(
  input: 'plugins/two-step-theme.conf.in',
  output: 'two-step-theme.conf',
  configuration: two_step_test_theme_config,
)

two_step_test_c_args = test_c_args + [
  '-DTEST_TWO_STEP_PLUGIN_PATH="@0@"'.format(
    two_step_plugin.full_path()
  ),
  '-DTEST_TWO_STEP_THEME_PATH="@0@"'.format(
    meson.current_build_dir() / 'two-step-theme.conf'
  ),
  '-DTEST_RENDERER_PLUGIN_DIR="@0@"'.format(
    meson.project_build_root() / 'tests/plugins'
  ),
]

two_step_test_executable = executable(
  'test-two-step',
  'test-two-step.c',
  c_args: two_step_test_c_args,
  dependencies: [libply_dep, libply_splash_core_dep, ply_renderer_dep],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
  ],
)

test(
  'splash-plugin-two-step-progress',
  two_step_test_executable,
  depends: [fake_renderer_plugin, two_step_plugin],
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-plugin'],
  timeout: test_timeout,
)

frame_buffer_renderer_test_executable = executable(
  'test-frame-buffer-renderer',
  'test-frame-buffer-renderer.c',
  c_args: test_c_args + [
    '-DTEST_FRAME_BUFFER_RENDERER_PLUGIN_PATH="@0@"'.format(
      frame_buffer_plugin.full_path()
    ),
  ],
  dependencies: [libply_dep, libply_splash_core_dep],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
  ],
)

test(
  'frame-buffer-renderer-plugin',
  frame_buffer_renderer_test_executable,
  depends: frame_buffer_plugin,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'renderer-plugin'],
  timeout: test_timeout,
)

renderer_test_c_args = test_c_args + [
  '-DTEST_RENDERER_PLUGIN_DIR="@0@"'.format(
    meson.project_build_root() / 'tests/plugins'
  ),
  '-DTEST_RENDERER_PLUGIN_PATH="@0@"'.format(
    fake_renderer_plugin.full_path()
  ),
]

renderer_test_executable = executable(
  'test-renderer',
  'test-renderer.c',
  c_args: renderer_test_c_args,
  dependencies: [libply_dep, ply_renderer_dep],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
  ],
)

test(
  'splash-core-renderer-plugin',
  renderer_test_executable,
  depends: fake_renderer_plugin,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-core'],
  timeout: test_timeout,
)

script_lib_plymouth_test_executable = executable(
  'test-script-lib-plymouth',
  [
    'test-script-lib-plymouth.c',
    script_headers,
    script_source_dir / 'script-lib-plymouth.c',
  ],
  c_args: renderer_test_c_args,
  dependencies: [libply_dep, ply_renderer_dep, script_engine_dep],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
    include_directories('../src/plugins/splash/script'),
  ],
)

test(
  'script-lib-plymouth',
  script_lib_plymouth_test_executable,
  depends: fake_renderer_plugin,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'script'],
  timeout: test_timeout,
)

if libdrm_dep.found()
  drm_renderer_test_executable = executable(
    'test-drm-renderer',
    'test-drm-renderer.c',
    '../src/plugins/renderers/drm/ply-renderer-drm-tile.c',
    c_args: test_c_args + [
      '-DTEST_DRM_RENDERER_PLUGIN_PATH="@0@"'.format(drm_plugin.full_path()),
    ],
    dependencies: [libply_dep, libply_splash_core_dep, libdrm_dep],
    include_directories: [
      include_directories('.'),
      include_directories('../src/libply-splash-core'),
      include_directories('../src/plugins/renderers/drm'),
    ],
  )

  test(
    'drm-renderer-plugin',
    drm_renderer_test_executable,
    depends: drm_plugin,
    env: test_environment,
    protocol: 'tap',
    suite: ['unit', 'renderer-plugin'],
    timeout: test_timeout,
  )
endif

label_test_c_args = test_c_args + [
  '-DTEST_LABEL_PLUGIN_DIR="@0@"'.format(
    meson.project_build_root() / 'tests/plugins'
  ),
  '-DTEST_LABEL_PLUGIN_PATH="@0@"'.format(
    fake_label_plugin.full_path()
  ),
]

label_test_executable = executable(
  'test-label',
  'test-label.c',
  c_args: label_test_c_args,
  dependencies: [libply_dep, libply_splash_core_dep, ply_label_dep],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-graphics'),
  ],
)

test(
  'splash-graphics-label-plugin',
  label_test_executable,
  depends: fake_label_plugin,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-graphics'],
  timeout: test_timeout,
)

boot_splash_test_c_args = test_c_args + [
  '-DTEST_SPLASH_PLUGIN_DIR="@0@/"'.format(
    meson.project_build_root() / 'tests/plugins'
  ),
  '-DTEST_SPLASH_PLUGIN_PATH="@0@"'.format(
    fake_splash_plugin.full_path()
  ),
  '-DTEST_SPLASH_THEME_PATH="@0@"'.format(
    meson.current_source_dir() / 'plugins/fake-splash-theme.conf'
  ),
  '-DTEST_RENDERER_PLUGIN_DIR="@0@"'.format(
    meson.project_build_root() / 'tests/plugins'
  ),
]

plymouthd_interaction_test_executable = executable(
  'test-plymouthd-interaction',
  'test-plymouthd-interaction.c',
  c_args: boot_splash_test_c_args,
  dependencies: [libply_dep, libply_splash_core_dep, plymouthd_interaction_dep],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
  ],
)

test(
  'plymouthd-interaction',
  plymouthd_interaction_test_executable,
  depends: fake_splash_plugin,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_messages_test_executable = executable(
  'test-plymouthd-messages',
  'test-plymouthd-messages.c',
  c_args: boot_splash_test_c_args,
  dependencies: [libply_dep, libply_splash_core_dep, plymouthd_messages_dep],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
  ],
)

test(
  'plymouthd-messages',
  plymouthd_messages_test_executable,
  depends: fake_splash_plugin,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

plymouthd_output_test_executable = executable(
  'test-plymouthd-output',
  'test-plymouthd-output.c',
  c_args: boot_splash_test_c_args,
  dependencies: [libply_dep, libply_splash_core_dep, plymouthd_output_dep],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
  ],
)

test(
  'plymouthd-output',
  plymouthd_output_test_executable,
  depends: fake_splash_plugin,
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'plymouthd'],
  timeout: test_timeout,
)

boot_splash_test_executable = executable(
  'test-boot-splash',
  'test-boot-splash.c',
  c_args: boot_splash_test_c_args,
  dependencies: [
    libply_dep,
    libply_splash_core_dep,
    ply_renderer_dep,
    plymouthd_splash_dep,
  ],
  include_directories: [
    include_directories('.'),
    include_directories('../src/libply-splash-core'),
  ],
)

test(
  'splash-core-boot-splash-plugin',
  boot_splash_test_executable,
  depends: [fake_renderer_plugin, fake_splash_plugin],
  env: test_environment,
  protocol: 'tap',
  suite: ['unit', 'splash-core'],
  timeout: test_timeout,
)

bash = find_program('bash')

test(
  'script-generate-version',
  bash,
  args: [
    files('test-generate-version.sh'),
    meson.project_source_root() / 'scripts/generate-version.sh',
  ],
  env: test_environment,
  protocol: 'tap',
  suite: ['functional', 'scripts'],
  timeout: test_timeout,
)

test(
  'executable-plymouthd-help',
  bash,
  args: [
    files('test-plymouthd.sh'),
    plymouthd,
  ],
  depends: plymouthd,
  env: test_environment,
  protocol: 'tap',
  suite: ['functional', 'executables'],
  timeout: test_timeout,
)

test(
  'executable-plymouth-help',
  bash,
  args: [
    files('test-plymouth.sh'),
    plymouth_client,
    plymouth_plugin_path,
  ],
  depends: plymouth_client,
  env: test_environment,
  protocol: 'tap',
  suite: ['functional', 'executables'],
  timeout: test_timeout,
)

test(
  'executable-plymouthd-fd-escrow',
  bash,
  args: [
    files('test-plymouthd-fd-escrow.sh'),
    plymouthd_fd_escrow,
  ],
  depends: plymouthd_fd_escrow,
  env: test_environment,
  protocol: 'tap',
  suite: ['functional', 'executables'],
  timeout: test_timeout,
)

test(
  'script-set-default-theme',
  bash,
  args: [
    files('test-set-default-theme.sh'),
    meson.project_build_root() / 'scripts/plymouth-set-default-theme',
  ],
  env: test_environment,
  protocol: 'tap',
  suite: ['functional', 'scripts'],
  timeout: test_timeout,
)

test(
  'script-populate-initrd',
  bash,
  args: [
    files('test-populate-initrd.sh'),
    meson.project_build_root() / 'scripts/plymouth-populate-initrd',
  ],
  env: test_environment,
  protocol: 'tap',
  suite: ['functional', 'scripts'],
  timeout: test_timeout,
)

test(
  'script-generate-initrd',
  bash,
  args: [
    files('test-generate-initrd.sh'),
    meson.project_build_root() / 'scripts/plymouth-generate-initrd',
  ],
  env: test_environment,
  protocol: 'tap',
  suite: ['functional', 'scripts'],
  timeout: test_timeout,
)

server_connection_hangup_test_executable = executable(
  'test-server-connection-hangup',
  'test-server-connection-hangup.c',
  c_args: test_c_args,
  dependencies: ply_boot_server_dep,
  include_directories: [
    include_directories('.'),
    include_directories('../src'),
  ],
)

# Reports its cases as TAP skips when not run as root, since the server
# checks SO_PEERCRED credentials before serving password requests.
test(
  'server-connection-hangup',
  server_connection_hangup_test_executable,
  env: test_environment,
  protocol: 'tap',
  suite: ['functional', 'boot-server'],
  timeout: 15,
)
