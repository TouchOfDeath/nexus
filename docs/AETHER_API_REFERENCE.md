# Aether Engine: Comprehensive API Reference

This document provides a complete, itemized reference for all constants, functions, structures, and error codes available in the **Aether** engine (`import aether` / `stdlib/aether.nex`).

---

## 1. Constants & Error Codes

### Return & Error Codes (`core.nex`)
| Constant | Value | Description |
| :--- | :--- | :--- |
| `AE_OK` | `0` | Success / no error. |
| `AE_ERR_INIT` | `1` | Failed subsystem initialization. |
| `AE_ERR_WINDOW` | `2` | Window creation, configuration, or resize failure. |
| `AE_ERR_ASSET_NOT_FOUND` | `3` | File missing; fallback asset substituted. |
| `AE_ERR_INVALID_FORMAT` | `4` | Invalid file or header structure (OBJ, PPM, WAV). |
| `AE_ERR_SHADER_COMPILE` | `5` | Shader compilation syntax error or unknown uniform. |
| `AE_ERR_OUT_OF_MEMORY` | `6` | Allocation failure. |
| `AE_ERR_AUDIO_DEVICE` | `7` | Audio initialization or driver error. |
| `AE_ERR_INVALID_HANDLE` | `8` | Null or invalid handle passed to API. |

### Color Constants (`core.nex`)
Packed 31-bit ARGB values (`0..2147483647`):
* `AE_COLOR_TRANSPARENT = 0`
* `AE_COLOR_BLACK = 2130706432`
* `AE_COLOR_WHITE = 2147483647`
* `AE_COLOR_RED = 2146387012`
* `AE_COLOR_GREEN = 2132985182`
* `AE_COLOR_BLUE = 2134606582`
* `AE_COLOR_CYAN = 2131146452`
* `AE_COLOR_MAGENTA = 2144945903`
* `AE_COLOR_YELLOW = 2146087688`
* `AE_COLOR_ORANGE = 2147054358`
* `AE_COLOR_PURPLE = 2141738487`
* `AE_COLOR_GOLD = 2147142677`
* `AE_COLOR_GRAY = 2137289867`
* `AE_COLOR_DARK_GRAY = 2132683067`
* `AE_COLOR_LIGHT_GRAY = 2144064993`
* `AE_COLOR_NAVY = 2131695402`
* `AE_COLOR_SLATE = 2134065493`
* `AE_COLOR_CRIMSON = 2145134118`
* `AE_COLOR_LIME = 2139409430`
* `AE_COLOR_SKY = 2134425080`
* `AE_COLOR_CORAL = 2147185029`

### Blending Modes (`core.nex`)
* `AE_BLEND_OPAQUE = 0`
* `AE_BLEND_ALPHA = 1`
* `AE_BLEND_ADDITIVE = 2`
* `AE_BLEND_MULTIPLY = 3`

### Shaders & Cull Modes (`gfx3d.nex`)
* `AE_SHADER_UNLIT = 0`
* `AE_SHADER_LAMBERT = 1`
* `AE_SHADER_PHONG = 2`
* `AE_SHADER_TOON = 3`
* `AE_SHADER_NORMAL_VIS = 4`
* `AE_SHADER_WIREFRAME = 5`
* `AE_SHADER_SKYBOX = 6`
* `AE_SHADER_CUSTOM = 7`
* `AE_CULL_NONE = 0`, `AE_CULL_BACK = 1`, `AE_CULL_FRONT = 2`

### Audio Waveform Types (`audio.nex`)
* `AE_WAVE_SINE = 0`
* `AE_WAVE_SQUARE = 1`
* `AE_WAVE_TRIANGLE = 2`
* `AE_WAVE_SAWTOOTH = 3`
* `AE_WAVE_NOISE = 4`

---

## 2. Core Functions (`stdlib/aether/core.nex`)

* `ae_init()`: Initializes Aether engine context.
* `ae_is_initialized()`: Returns `1` if initialized, `0` otherwise.
* `ae_set_error(code, msg)`: Records error code and diagnostic message.
* `ae_get_error()`: Returns current error code.
* `ae_get_error_message()`: Returns current error string.
* `ae_clear_error()`: Resets error state to `AE_OK`.
* `ae_log_set_level(level)`: Sets minimum log level (`AE_LOG_DEBUG`..`AE_LOG_NONE`).
* `ae_time_now_ms()`: Returns monotonic system time in milliseconds.
* `ae_time_now_us()`: Returns monotonic system time in microseconds.
* `ae_sleep_ms(ms)`: High-resolution millisecond sleep.
* `ae_delta_time_ms()`: Milliseconds elapsed between the last two frames.
* `ae_delta_time()`: Delta time in fixed-point (`1000 = 1.0` second).
* `ae_fps()`: Current measured frames per second.
* `ae_frame_count()`: Total rendered frames since window creation.
* `ae_rgba(r, g, b, a = 255)`: Packs RGBA components into a 31-bit color integer.
* `ae_rgb(r, g, b)`: Packs RGB with opaque alpha.
* `ae_color_r(c)`, `ae_color_g(c)`, `ae_color_b(c)`, `ae_color_a(c)`: Extracts channel (`0..255`).
* `ae_color_lerp(c1, c2, t_fp)`: Linear interpolation between two colors (`t_fp` in `0..1000`).
* `ae_color_from_hsv(h_deg, s_pct, v_pct)`: Constructs 31-bit color from Hue (`0..360`), Saturation (`0..100`), and Value (`0..100`).
* `ae_color_modulate(c1, c2)`: Multiplies two colors per channel.
* `ae_blend_color(dst, src, mode = 1)`: Blends source color over destination using `mode`.

---

## 3. Math Functions (`stdlib/aether/math.nex`)

* `ae_clamp(v, lo, hi)`: Restricts integer `v` to `[lo, hi]`.
* `ae_lerp(a, b, t_fp)`: Linear interpolation (`1000 = 1.0`).
* `ae_isqrt(n)`: Fast integer square root.
* `ae_sin_deg(deg)`: Sine function taking degrees (`-1000..1000`).
* `ae_cos_deg(deg)`: Cosine function taking degrees (`-1000..1000`).
* `ae_tan_deg(deg)`: Tangent function taking degrees.
* `ae_atan2_deg(y, x)`: Four-quadrant arc tangent returning degrees (`-180..180`).
* `ae_vec2(x, y)`: Allocates `AeVec2`.
* `ae_vec2_len(a)`: Length of 2D vector.
* `ae_vec3(x, y, z)`: Allocates `AeVec3`.
* `ae_vec3_normalize(a)`: Returns unit-length `AeVec3` (`1000 = 1.0`).
* `ae_vec3_cross(a, b)`: Vector cross product.
* `ae_vec3_dot(a, b)`: Vector dot product.
* `ae_vec4(x, y, z, w)`: Allocates `AeVec4`.
* `ae_quat_from_axis_angle(ax, ay, az, deg)`: Constructs unit quaternion from rotation axis and angle.
* `ae_quat_rotate_vec3(q, v)`: Rotates 3D vector `v` by quaternion `q` with 64-bit precision.
* `ae_mat4_new()`: Allocates 128-byte 4x4 matrix initialized to identity.
* `ae_mat4_from_trs(m, tx, ty, tz, rx, ry, rz, sx, sy, sz)`: Composes translation, rotation, and scaling.
* `ae_mat4_look_at(m, ex, ey, ez, tx, ty, tz, ux, uy, uz)`: Constructs View matrix.
* `ae_mat4_perspective(m, fov_deg, aspect_fp, near_fp, far_fp)`: Constructs Perspective Projection matrix.
* `ae_mat4_ortho(m, l_fp, r_fp, b_fp, t_fp, near_fp, far_fp)`: Constructs Orthographic Projection matrix.
* `let rx, ry, rz = ae_mat4_transform_point(m, x, y, z)`: Multi-return affine point transformation.
* `let rx, ry, rz, rw = ae_mat4_transform_vec4(m, x, y, z, w)`: Multi-return 4D vector transformation.
* `ae_aabb_new(min_x, min_y, min_z, max_x, max_y, max_z)`: Constructs `AeAABB`.
* `ae_aabb_contains_point(b, px, py, pz)`: Point in AABB test (`1` or `0`).
* `ae_aabb_intersects(a, b)`: AABB vs AABB overlap test (`1` or `0`).
* `ae_ray_new(ox, oy, oz, dx, dy, dz)`: Constructs `AeRay`.
* `ae_ray_intersect_sphere(r, sx, sy, sz, radius_fp)`: Distance to intersection, or `-1` on miss.
* `ae_ray_intersect_aabb(r, box)`: Distance to intersection, or `-1` on miss.

---

## 4. Input Functions (`stdlib/aether/input.nex`)

* `ae_key_down(key)`: Returns `1` if key is currently held down.
* `ae_key_pressed(key)`: Returns `1` if key was pressed this frame.
* `ae_key_released(key)`: Returns `1` if key was released this frame.
* `ae_key_repeated(key)`: Returns `1` if key triggered auto-repeat.
* `ae_text_input_count()`: Number of printable characters entered this frame.
* `ae_text_input_char(idx)`: ASCII character code at index `idx`.
* `ae_mouse_x()`, `ae_mouse_y()`: Current mouse cursor coordinates.
* `ae_mouse_dx()`, `ae_mouse_dy()`: Mouse delta movement since last frame.
* `ae_mouse_wheel_y()`: Vertical mouse wheel scroll delta.
* `ae_mouse_down(btn)`: Returns `1` if mouse button is held (`AE_MOUSE_LEFT`..).
* `ae_mouse_pressed(btn)`: Returns `1` if mouse button was clicked this frame.
* `ae_gamepad_down(btn)`: Returns `1` if gamepad button is pressed.
* `ae_gamepad_axis(axis_idx)`: Analog axis value (`-1000..1000`).
* `ae_action_bind(action_id, primary_key, alt_key = 0, mouse_btn = 0, pad_btn = 0)`: Binds abstract action.
* `ae_action_down(action_id)`: Evaluates if any bound input is down.
* `ae_action_pressed(action_id)`: Evaluates if any bound input was newly triggered.

---

## 5. Window Functions (`stdlib/aether/window.nex`)

* `ae_window_create(title, width, height)`: Creates a window.
* `ae_window_make_current(win)`: Activates target window in multi-window environments.
* `ae_window_is_open(win = 0)`: Evaluates if window should continue running.
* `ae_begin_frame()`: Starts new frame; returns `0` when window closed or max frames reached.
* `ae_end_frame()`: Presents frame buffer to display and increments frame counter.
* `ae_clear(color)`: Clears color and depth buffers.
* `ae_read_pixel(x, y)`: Reads 31-bit color value at pixel coordinates.
* `ae_read_depth(x, y)`: Reads depth buffer integer at pixel coordinates.
* `ae_framebuffer_checksum()`: Returns 32-bit FNV-1a checksum of active color buffer.
* `ae_framebuffer_nonblack_pixels()`: Counts non-zero pixels in color buffer.
* `ae_save_ppm(path)`: Exports active framebuffer to binary P6 `.ppm` image.
* `ae_set_max_frames(max_f)`: Automatically closes window after `max_f` frames (ideal for headless CI).
* `ae_shutdown()`: Destroys windows and releases engine resources.

---

## 6. 2D Rendering Functions (`stdlib/aether/gfx2d.nex`)

* `ae_set_blend_mode(mode)`: Sets blending mode (`AE_BLEND_OPAQUE`..`AE_BLEND_MULTIPLY`).
* `ae_scissor_begin(x, y, w, h)`, `ae_scissor_end()`: Hardware-clipped rectangular rendering region.
* `ae_draw_point(x, y, color)`: Single pixel draw.
* `ae_draw_line(x0, y0, x1, y1, color)`: 1-pixel Bresenham line.
* `ae_draw_line_thick(x0, y0, x1, y1, thickness, color)`: Thick line rasterization.
* `ae_fill_rect(x, y, w, h, color)`, `ae_stroke_rect(x, y, w, h, thickness, color)`: Solid / stroked rectangles.
* `ae_fill_rounded_rect(x, y, w, h, radius, color)`: Rounded rectangle.
* `ae_fill_circle(cx, cy, radius, color)`, `ae_stroke_circle(cx, cy, radius, color)`: Circle drawing.
* `ae_fill_ellipse(cx, cy, rx, ry, color)`: Ellipse drawing.
* `ae_fill_triangle(x0, y0, x1, y1, x2, y2, color)`: Filled 2D triangle.
* `ae_fill_polygon(pts_buf, count, color)`: Convex polygon triangulation.
* `ae_fill_gradient_v(x, y, w, h, c_top, c_bot)`: Vertical gradient.
* `ae_fill_gradient_h(x, y, w, h, c_left, c_right)`: Horizontal gradient.
* `ae_camera2d_new(x, y, zoom_fp, rot_deg)`: Creates `AeCamera2D`.
* `ae_camera2d_begin(cam)`, `ae_camera2d_end()`: Activates / deactivates 2D camera transform.
* `let sx, sy = ae_world_to_screen_2d(wx, wy)`: Converts world coordinate to screen space.
* `let wx, wy = ae_screen_to_world_2d(sx, sy)`: Converts screen space to world space.
* `ae_texture_create(w, h)`: Creates mutable `AeTexture`.
* `ae_texture_checkerboard(w, h, cell, c1, c2)`: Generates procedural checkerboard.
* `ae_texture_gradient(w, h, c_tl, c_tr, c_bl, c_br)`: Generates 4-corner bilinear gradient texture.
* `ae_texture_load_ppm(path)`: Loads binary P6 `.ppm` image into texture.
* `ae_texture_set_filter(tex, mode)`: Sets `AE_FILTER_NEAREST` or `AE_FILTER_BILINEAR`.
* `ae_texture_set_wrap(tex, mode)`: Sets `AE_WRAP_REPEAT`, `AE_WRAP_CLAMP`, or `AE_WRAP_MIRROR`.
* `ae_sprite_new(tex, u0, v0, u1, v1)`: Allocates `AeSprite`.
* `ae_draw_sprite(sp, x, y, scale_x, scale_y, rot_deg, tint)`: Draws transformed sprite.
* `ae_spritesheet_new(tex, cell_w, cell_h)`: Creates sprite atlas.
* `ae_spritesheet_draw_frame(sheet, idx, x, y, scale, flip_x, tint)`: Renders specific atlas cell.
* `ae_animator_new(sheet, start_f, count, fps, looping)`: Frame-based sprite animator.
* `ae_animator_update(anim, dt_ms)`: Advances animator timeline.
* `ae_animator_draw(anim, x, y, scale, flip_x, tint)`: Renders active animation frame.
* `ae_batch_begin()`, `ae_batch_rect(x, y, w, h, color)`, `ae_batch_flush()`: Deferred command batching.
* `ae_text_width(text, scale)`: Measures rendered text width in pixels.
* `ae_text_height(text, scale, spacing)`: Measures multiline text height.
* `ae_draw_text(text, x, y, scale, color)`: Renders 8x8 bitmap typography.
* `ae_draw_text_aligned(text, x, y, scale, color, align)`: Aligned text (`AE_ALIGN_LEFT`..`AE_ALIGN_RIGHT`).
* `ae_draw_text_shadow(text, x, y, scale, color, shadow_color)`: Text with drop shadow.

---

## 7. 3D Rendering Functions (`stdlib/aether/gfx3d.nex`)

* `ae_mesh_create(max_verts, max_indices)`: Allocates vertex and index buffers.
* `ae_mesh_add_vertex(m, px, py, pz, nx, ny, nz, u, v, color)`: Adds 72-byte vertex.
* `ae_mesh_add_triangle(m, i0, i1, i2)`: Adds triangle indices.
* `ae_mesh_create_cube(size_fp)`: Generates 6-sided 12-triangle cube with normals and UVs.
* `ae_mesh_create_sphere(radius_fp, rings, sectors)`: Generates UV sphere mesh.
* `ae_mesh_create_pyramid(base_fp, height_fp)`: Generates 5-face pyramid mesh.
* `ae_mesh_create_plane(width_fp, depth_fp, subdiv)`: Generates tessellated ground plane.
* `ae_mesh_from_obj_str(obj_text)`: Parses Wavefront `.obj` ASCII string.
* `ae_mesh_load_obj(path)`: Loads `.obj` file from disk.
* `ae_shader_create(kind, vert_src, frag_src)`: Compiles shader object.
* `ae_shader_set_uniform_int(sh, name, val)`: Sets uniform parameter.
* `ae_material_create(shader_kind, albedo_color, texture)`: Creates `AeMaterial`.
* `ae_camera3d_perspective(fov_deg, aspect_fp, near_fp, far_fp)`: Perspective 3D camera.
* `ae_camera3d_orthographic(size_fp, aspect_fp, near_fp, far_fp)`: Orthographic 3D camera.
* `ae_camera3d_orbit(cam, tx, ty, tz, dist, yaw, pitch)`: Sets camera position via spherical coordinates.
* `ae_camera3d_begin(cam)`: Binds camera View-Projection matrix for rendering.
* `ae_light_set_ambient(r, g, b, intensity_fp)`: Sets ambient lighting color.
* `ae_light_set_directional(dx, dy, dz, r, g, b, intensity_fp)`: Sets directional sun light.
* `ae_light_add_point(px, py, pz, r, g, b, radius_fp, intensity_fp)`: Registers point light source.
* `ae_shadow_enable(plane_y_fp, darkness_fp)`: Enables directional planar shadow mapping pass.
* `ae_draw_skybox(zenith_col, horizon_col, ground_col)`: Atmospheric 3-band skybox.
* `ae_draw_mesh_trs(mesh, tx, ty, tz, rx, ry, rz, sx, sy, sz, mat)`: Draws 3D mesh transformed by TRS.
* `ae_render_target_create(w, h)`: Creates offscreen framebuffer.
* `ae_render_target_begin(rt)`, `ae_render_target_end()`: Binds / unbinds offscreen render target.
* `ae_post_process_apply(effect)`: Applies fullscreen post-process (`AE_POST_INVERT`..`AE_POST_VIGNETTE`).
* `ae_node3d_create(mesh, mat)`: Allocates scene graph node.
* `ae_node3d_add_child(parent, child)`: Attaches child to parent hierarchy.
* `ae_scene_draw(root_node)`: Recursively traverses and renders scene graph.
* `let wx, wy, wz = ae_node3d_world_position(nd)`: Returns computed world coordinates of node.

---

## 8. Audio Functions (`stdlib/aether/audio.nex`)

* `ae_audio_init(sample_rate = 22050)`: Initializes audio mixer.
* `ae_audio_shutdown()`: Stops voices and closes mixer.
* `ae_audio_set_master_volume(vol_fp)`: Sets master volume (`1000 = 1.0`).
* `ae_sound_create(samples, srate, channels)`: Allocates raw PCM sound buffer.
* `ae_sound_synth_tone(wave_type, freq_hz, dur_ms, amp_fp)`: Procedural waveform generator.
* `ae_sound_synth_sweep(wave_type, start_hz, end_hz, dur_ms, amp_fp)`: Frequency sweep generator.
* `ae_sound_save_wav(snd, path)`: Exports sound buffer to valid RIFF/WAVE `.wav` file.
* `ae_sound_load_wav(path)`: Loads 16-bit PCM `.wav` file.
* `ae_sound_play(snd, vol_fp = 1000)`: Plays sound effect on free voice slot.
* `ae_music_play(snd, vol_fp = 850, looping = 1)`: Plays looping background music.
* `ae_voice_pause(vid)`, `ae_voice_resume(vid)`, `ae_voice_stop(vid)`: Voice transport controls.
* `ae_voice_set_volume(vid, vol_fp)`, `ae_voice_set_pitch(vid, pitch_fp)`, `ae_voice_set_pan(vid, pan_fp)`: Voice modulation.
* `ae_voice_is_playing(vid)`: Returns `1` if voice is actively producing audio.
* `ae_audio_set_listener(x, y, z, rx, ry, rz)`: Updates 3D listener position and orientation.
* `ae_sound_play_spatial(snd, sx, sy, sz, max_dist, vol)`: Distance-attenuated 3D spatial playback.
* `ae_audio_update(dt_ms)`: Mixes pending audio frames.
* `ae_audio_peak_left()`, `ae_audio_peak_right()`: Returns current stereo peak levels (`0..32767`).

---

## 9. Asset & Game Functions (`assets.nex`, `game.nex`)

* `ae_asset_load_texture(path)`: Reference-counted texture load with missing asset fallback.
* `ae_asset_load_mesh(path)`: Reference-counted `.obj` mesh load with cube fallback.
* `ae_asset_load_sound(path)`: Reference-counted `.wav` sound load with tone fallback.
* `ae_asset_release(handle)`: Decrements ref-count and frees asset when zero.
* `ae_asset_ref_count(handle)`: Returns current active references.
* `ae_asset_cache_hits()`: Returns total deduplicated cache hits.
* `ae_clock_create(fixed_dt_ms)`: Creates fixed-timestep game loop clock.
* `ae_clock_accumulate(clk, dt_ms)`: Adds elapsed frame time.
* `ae_clock_consume_step(clk)`: Consumes one fixed simulation step (`1` or `0`).
* `ae_clock_alpha_fp(clk)`: Sub-frame interpolation alpha (`0..1000`).
* `ae_timer_create(dur_ms, looping)`: Creates timer.
* `ae_timer_update(tmr, dt_ms)`: Advances timer; returns `1` when expired.
* `ae_timer_is_finished(tmr)`: Checks if timer has finished.
* `ae_timer_progress_fp(tmr)`: Progress ratio in `0..1000`.
* `ae_collide_rect_rect(...)`, `ae_collide_circle_circle(...)`, `ae_collide_circle_rect(...)`: 2D collision tests.
* `ae_collide_sphere_sphere(...)`, `ae_collide_aabb_aabb(...)`: 3D collision tests.
* `ae_debug_draw_grid_2d(cell_size, color)`: 2D coordinate grid overlay.
* `ae_debug_draw_aabb_2d(x, y, w, h, color)`: 2D bounding box wireframe.
* `ae_debug_draw_hud(x, y)`: Telemetry overlay displaying FPS, draw calls, vertex and triangle counts.
* `ae_transition_draw_fade(progress_fp, color)`: Fullscreen alpha fade transition.
