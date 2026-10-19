# Aether Engine: Comprehensive Developer Manual & User Guide

**Aether** is the official 2D and 3D graphics, audio, input, asset management, and game development engine for the **Nexus** programming language. Built entirely in native Nexus with zero external runtime dependencies, Aether brings production-grade multimedia and game development capabilities to Nexus programs across Windows, Linux, and macOS.

---

## Table of Contents
1. [Introduction & Architectural Overview](#1-introduction--architectural-overview)
2. [Quickstart & The First Window](#2-quickstart--the-first-window)
3. [The Core Context & Color Pipeline](#3-the-core-context--color-pipeline)
4. [2D Graphics Engine](#4-2d-graphics-engine)
   - 2D Primitives & Gradients
   - Alpha, Additive & Multiply Blending
   - Scissor Clipping
   - 2D Camera (Pan, Zoom, Rotation & Picking)
   - Textures, Filtering & Wrapping
   - Sprites, Atlases & Frame-Based Animation
   - Bitmap Font Typography & Text Layout
   - Deferred 2D Command Batching
5. [3D Graphics Engine & Scene Graph](#5-3d-graphics-engine--scene-graph)
   - Vertex & Index Buffers (`AeMesh`)
   - Procedural Primitives & Wavefront `.obj` Loading
   - 32-bit Depth Buffer & Backface Culling
   - Programmable Shader Pipelines & Materials
   - Perspective & Orthographic 3D Cameras
   - Lighting & Planar Shadow Mapping
   - Skybox Rendering
   - Offscreen RenderTargets & Post-Processing
   - Hierarchical Scene Graph (`AeNode3D`)
6. [Multi-Voice Audio Engine & 3D Sound](#6-multi-voice-audio-engine--3d-sound)
   - Waveform & ADSR Synthesizer
   - RIFF/WAVE (.wav) Loading & Saving
   - Playback Controls & 16-Voice Stereo Mixer
   - 3D Spatial Audio Attenuation & Panning
7. [Unified Input & Event System](#7-unified-input--event-system)
   - Keyboard & Key Repeat Handling
   - Separate Printable Text Input Queue
   - Mouse Position, Buttons & Wheel
   - Gamepad Controller Axes & Deadzones
   - Abstract Action Mapping
8. [Asset Management & Game Utilities](#8-asset-management--game-utilities)
   - Reference-Counted Asset Cache & Deduplication
   - Missing Asset Graceful Fallbacks
   - Fixed-Timestep Game Clock (`AeClock`)
   - Timers & Cooldowns (`AeTimer`)
   - 2D & 3D Collision Detection
   - Debug HUD & Vector Overlays
9. [Running the Examples](#9-running-the-examples)

---

## 1. Introduction & Architectural Overview

Nexus programs compile directly to native x86-64 machine code with 0% C runtime and 0% garbage collector overhead. Aether adheres strictly to this philosophy:
* **Zero Runtime Overhead**: No heavyweight runtimes or external shared library dependencies.
* **Dual Execution Modes**: Seamlessly transitions between live hardware-accelerated desktop windows (via bare-metal Linux X11 wire protocol) and deterministic offscreen software rendering (`AE_BACKEND_HEADLESS`).
* **Idiomatic Nexus Syntax**: Full support for unquoted module imports (`import aether`, `import aether.gfx3d`), namespace method dispatch (`aether.func(...)`, `AeWindow.create(...)`), and multi-return tuple destructuring (`let wx, wy, wz = ae_mat4_transform_point(...)`).

---

## 2. Quickstart & The First Window

To build and run an Aether program, import `aether` at the top of your file:

```nexus
import aether

# Create a 640x360 window
let win = ae_window_create("My First Aether Window", 640, 360)

# Main game loop
while ae_begin_frame() {
    ae_clear(AE_COLOR_NAVY)
    ae_draw_text("HELLO NEXUS & AETHER!", 180, 160, 2, AE_COLOR_GOLD)
    ae_end_frame()
}

ae_shutdown()
```

Run your program from anywhere using the Nexus CLI:
```bash
./nexus run my_game.nex
```

---

## 3. The Core Context & Color Pipeline

### Engine Initialization & Lifecycle
All Aether subsystems share an internal context block (`_ae_ctx`). It is automatically initialized on your first call to any `ae_*` function, or explicitly via:
```nexus
ae_init()
```

### Color Representation
Aether packs colors into 31-bit signed integers in `0..2147483647` (`0x00000000..0x7FFFFFFF`) so that all color constants fit into standard 32-bit immediate operands without Horner decomposition:
* Format: `(A << 24) | (R << 16) | (G << 8) | B` where Alpha ranges from `0` (transparent) to `127` (opaque), automatically scaled to `0..255` in shaders and blending passes.

Built-in Color Constants:
* `AE_COLOR_BLACK`, `AE_COLOR_WHITE`, `AE_COLOR_RED`, `AE_COLOR_GREEN`, `AE_COLOR_BLUE`
* `AE_COLOR_CYAN`, `AE_COLOR_MAGENTA`, `AE_COLOR_YELLOW`, `AE_COLOR_ORANGE`, `AE_COLOR_GOLD`
* `AE_COLOR_NAVY`, `AE_COLOR_SLATE`, `AE_COLOR_CRIMSON`, `AE_COLOR_CORAL`, `AE_COLOR_GRAY`

Custom Colors:
```nexus
let my_color = ae_rgba(255, 128, 64, 255)
let lerped = ae_color_lerp(AE_COLOR_RED, AE_COLOR_BLUE, 500) # 50% blend
let hsv_color = ae_color_from_hsv(180, 90, 100)              # Cyan HSV
```

---

## 4. 2D Graphics Engine

### 2D Primitives
```nexus
# Points & Lines
ae_draw_point(x, y, color)
ae_draw_line(x0, y0, x1, y1, color)
ae_draw_line_thick(x0, y0, x1, y1, thickness, color)

# Rectangles & Gradients
ae_fill_rect(x, y, w, h, color)
ae_stroke_rect(x, y, w, h, thickness, color)
ae_fill_rounded_rect(x, y, w, h, radius, color)
ae_fill_gradient_v(x, y, w, h, top_color, bottom_color)
ae_fill_gradient_h(x, y, w, h, left_color, right_color)

# Circles & Triangles
ae_fill_circle(cx, cy, radius, color)
ae_stroke_circle(cx, cy, radius, color)
ae_fill_ellipse(cx, cy, rx, ry, color)
ae_fill_triangle(x0, y0, x1, y1, x2, y2, color)
ae_stroke_triangle(x0, y0, x1, y1, x2, y2, color)
```

### Blending Modes & Scissor Clipping
```nexus
ae_set_blend_mode(AE_BLEND_ALPHA)    # Standard transparency
ae_set_blend_mode(AE_BLEND_ADDITIVE) # Glowing effects / particles
ae_set_blend_mode(AE_BLEND_MULTIPLY) # Shadow overlays
ae_set_blend_mode(AE_BLEND_OPAQUE)   # Fast solid overwrite

ae_scissor_begin(100, 50, 200, 150)
# All draw calls here are strictly clipped to the rectangular scissor bounds
ae_scissor_end()
```

### 2D Camera
The 2D camera provides translation, zoom, and rotation:
```nexus
let cam = ae_camera2d_new(player_x, player_y, 1500, 0) # 1.5x zoom
ae_camera2d_begin(cam)
# Draw world-space tiles and characters here
let screen_x, screen_y = ae_world_to_screen_2d(obj_x, obj_y)
let world_x, world_y = ae_screen_to_world_2d(mouse_x, mouse_y)
ae_camera2d_end()
```

### Textures, Sprites & Animation
```nexus
# Procedural textures or loaded PPM files
let tex = ae_texture_checkerboard(64, 64, 8, AE_COLOR_WHITE, AE_COLOR_BLUE)
ae_texture_set_filter(tex, AE_FILTER_BILINEAR)

# Single sprite
let sp = ae_sprite_new(tex, 0, 0, 1000, 1000)
ae_draw_sprite(sp, x, y, 1000, 1000, 45, AE_COLOR_WHITE) # Rotated 45 deg

# Spritesheet & Animator
let sheet = ae_spritesheet_new(tex, 16, 16)
let anim = ae_animator_new(sheet, 0, 8, 12, 1) # 8 frames, 12 fps, looping
ae_animator_update(anim, dt_ms)
ae_animator_draw(anim, x, y, 2000, 0, AE_COLOR_WHITE)
```

---

## 5. 3D Graphics Engine & Scene Graph

### Meshes & OBJ Loading
```nexus
let cube = ae_mesh_create_cube(1000)
let sphere = ae_mesh_create_sphere(800, 8, 12)
let plane = ae_mesh_create_plane(3000, 3000, 4)
let custom_mesh = ae_mesh_load_obj("assets/models/ship.obj")
```

### Materials & Programmable Shaders
Aether includes 7 built-in shader profiles:
* `AE_SHADER_UNLIT`: Flat color / unshaded.
* `AE_SHADER_LAMBERT`: Diffuse cosine lighting.
* `AE_SHADER_PHONG`: Blinn-Phong diffuse + specular highlights.
* `AE_SHADER_TOON`: 3-band quantized cel shading.
* `AE_SHADER_NORMAL_VIS`: Diagnostic RGB surface normal mapping.
* `AE_SHADER_WIREFRAME`: Triangle edge wireframe.
* `AE_SHADER_CUSTOM`: User-defined programmable vertex & fragment stages.

```nexus
let mat = ae_material_create(AE_SHADER_PHONG, AE_COLOR_RED, tex)
ae_material_set_cull(mat, AE_CULL_BACK)
```

### 3D Camera & Orbital Controls
```nexus
let cam = ae_camera3d_perspective(60, 1333, 100, 50000) # 60 deg FOV
ae_camera3d_orbit(cam, target_x, target_y, target_z, distance, yaw_deg, pitch_deg)
ae_camera3d_begin(cam)
```

### Lighting & Shadow Mapping
```nexus
ae_light_set_ambient(40, 45, 60, 1000)
ae_light_set_directional(-400, -800, -400, 255, 245, 220, 1000)
ae_light_add_point(pos_x, pos_y, pos_z, 255, 120, 50, 4000, 900)
ae_shadow_enable(-900, 400) # Projects shadows onto plane Y = -900
```

### Scene Graph
```nexus
let root = ae_node3d_create(cube, mat)
let child = ae_node3d_create(sphere, mat)
ae_node3d_set_position(child, 0, 800, 0)
ae_node3d_add_child(root, child)

# Traverses and draws entire hierarchy with matrix inheritance
ae_scene_draw(root)
let world_x, world_y, world_z = ae_node3d_world_position(child)
```

---

## 6. Multi-Voice Audio Engine & 3D Sound

Aether includes a built-in 16-voice stereo mixer and software synthesizer running at 22,050 Hz:
```nexus
# Synthesize tone or sound effect
let laser_sfx = ae_sound_synth_sweep(AE_WAVE_SQUARE, 880, 220, 120, 800)
let bgm = ae_sound_load_wav("assets/audio/theme.wav")

# Playback controls
let voice = ae_sound_play(laser_sfx, 900) # Volume 90%
ae_voice_set_pitch(voice, 1200)           # 1.2x pitch
ae_voice_set_pan(voice, -500)             # Pan 50% left
ae_music_play(bgm, 800, 1)                # Looping music

# 3D Spatial Audio
ae_audio_set_listener(player_x, player_y, player_z, 1000, 0, 0)
ae_sound_play_spatial(laser_sfx, enemy_x, enemy_y, enemy_z, 10000, 1000)

# Advance mixer
ae_audio_update(16)
let left_peak = ae_audio_peak_left()
let right_peak = ae_audio_peak_right()
```

---

## 7. Unified Input & Event System

```nexus
# Keyboard queries
if ae_key_down(AE_KEY_SPACE) { ... }
if ae_key_pressed(AE_KEY_ENTER) { ... }
if ae_key_released(AE_KEY_ESCAPE) { ... }

# Mouse queries
let mx = ae_mouse_x()
let my = ae_mouse_y()
let mdx = ae_mouse_dx()
let mdy = ae_mouse_dy()
if ae_mouse_down(AE_MOUSE_LEFT) { ... }

# Action mapping
ae_action_bind(1, AE_KEY_W, AE_KEY_UP, 0, AE_PAD_DPAD_UP)
if ae_action_down(1) { player_y -= 5 }
```

---

## 8. Asset Management & Game Utilities

```nexus
# Reference-counted cached resource loading
let tex = ae_asset_load_texture("assets/textures/player.ppm")
let mesh = ae_asset_load_mesh("assets/models/boss.obj")
let sfx = ae_asset_load_sound("assets/sounds/hit.wav")
ae_asset_release(tex)

# Fixed-timestep physics clock
let clk = ae_clock_create(16) # 60 Hz fixed updates
ae_clock_accumulate(clk, delta_time_ms)
while ae_clock_consume_step(clk) {
    # Update physics with fixed dt
}
let alpha = ae_clock_alpha_fp(clk) # Sub-frame interpolation alpha

# Collision detection
if ae_collide_rect_rect(x1, y1, w1, h1, x2, y2, w2, h2) { ... }
if ae_collide_circle_circle(x1, y1, r1, x2, y2, r2) { ... }
if ae_collide_circle_rect(cx, cy, r, rx, ry, rw, rh) { ... }
if ae_collide_sphere_sphere(x1, y1, z1, r1, x2, y2, z2, r2) { ... }
```

---

## 9. Running the Examples

12 complete runnable example programs are included in `examples/`:
```bash
./nexus run examples/aether_01_shapes.nex      # 2D Primitives & Gradients
./nexus run examples/aether_02_sprites.nex     # Sprites & Animation
./nexus run examples/aether_03_text_ui.nex     # UI & Bitmap Typography
./nexus run examples/aether_04_particles.nex   # Additive Particle System
./nexus run examples/aether_05_camera2d.nex    # 2D Camera Pan & Zoom
./nexus run examples/aether_06_game2d_pong.nex # Playable 2D Brick Breaker
./nexus run examples/aether_07_cube3d.nex      # 3D Textured Cube
./nexus run examples/aether_08_lighting3d.nex  # 3D Shaders & Shadows
./nexus run examples/aether_09_camera3d.nex    # 3D Orbit & Raycasting
./nexus run examples/aether_10_scene_graph.nex # OBJ Loader & Scene Graph
./nexus run examples/aether_11_audio_mixer.nex # Audio Mixer & Spatial Sound
./nexus run examples/aether_12_game3d.nex      # Playable 3D Starfighter Game
```
