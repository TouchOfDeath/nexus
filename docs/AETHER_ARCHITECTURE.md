# Aether Engine: Architecture & Systems Internals

This document details the low-level systems architecture, compiler and preprocessor transformations, binary layouts, memory models, and execution mechanics of the **Aether** engine in the **Nexus** self-hosted toolchain.

---

## 1. System Overview & Design Philosophy

The Nexus programming language compiles directly from `.nex` source code into native x86-64 machine code without a garbage collector or C standard library. To integrate a full-featured 2D/3D graphics and audio engine under these constraints, Aether implements a zero-overhead architecture centered on:

1. **Bare-Metal OS & Wire Protocols**: Direct System V Linux kernel syscalls (`sys_write`, `sys_read`, `sys_mmap`, `sys_socket`, `sys_connect`, `sys_clock_gettime`) and raw X11 protocol packet generation.
2. **Context Memory Layout**: A single preallocated, structured memory block (`_ae_ctx`) that houses all global engine states, color buffers, depth buffers, matrix caches, and telemetry counters.
3. **XL Binary Architecture**: Extension of the self-hosted Nexus compiler to support 245 KB code sections (`template_xl.bin` / `nexc_xl.elf`), allowing large graphics engines and games to compile and link without exhausting text section bounds.
4. **Stage 5 Convergence Invariant**: 100% bit-for-bit self-hosting identity is maintained across all original standard library modules and compiler stages (`0` byte differences on `nexc.nex`).

---

## 2. Memory Architecture & Context Structure (`_ae_ctx`)

All Aether modules interact through the unified 64 KB engine context allocated during initialization:

```
Offset (Bytes)       Field Name                   Description
0x0000 (0)           magic                        0xAE780001 (Aether context magic)
0x0008 (8)           active_backend               0=Auto, 1=X11, 2=Win32, 3=Cocoa, 4=Headless
0x0010 (16)          last_error_code              Current error code (AE_OK .. AE_ERR_INVALID_HANDLE)
0x0018 (24)          last_error_msg               Pointer to diagnostic message string
0x0020 (32)          log_level                    Minimum log level filter
0x0028 (40)          frame_count                  Total frames presented
0x0030 (48)          target_fps                   Target framerate for VSync pacing
0x0038 (56)          vsync_enabled                VSync flag (1=enabled, 0=immediate)
0x0040 (64)          delta_time_ms                Frame delta time in milliseconds
0x0048 (72)          delta_time_us                Frame delta time in microseconds
0x0050 (80)          total_time_ms                Monotonic engine running time
0x0058 (88)          current_fps                  Real-time calculated FPS
0x0098 (152)         draw_calls                   Performance draw call accumulator
0x00A0 (160)         vertices                     Performance vertex accumulator
0x00A8 (168)         triangles                    Performance triangle accumulator
0x00B0 (176)         batches                      2D batch flush count
0x00B8 (184)         max_frames                   Headless CI frame exit limit (0=infinite)
0x0200 (512)         window_count                 Number of allocated window handles
0x0208 (520)         active_window_id             ID of currently active window
0x0210 (528)         window_table_ptr             Pointer to 8-slot window descriptor table
0x0218 (536)         window_width                 Active window logical width
0x0220 (544)         window_height                Active window logical height
0x0228 (552)         framebuffer_width            Active framebuffer pixel width
0x0230 (560)         framebuffer_height           Active framebuffer pixel height
0x0240 (576)         window_is_open               Window open flag
0x0278 (632)         color_buffer_ptr             Pointer to 32-bit ARGB pixel memory
0x0280 (640)         depth_buffer_ptr             Pointer to 32-bit integer depth buffer
0x0288 (648)         x11_socket_fd                Linux X11 UNIX domain socket file descriptor
0x0A00 (2560..2584)  scissor_rect                 Scissor clipping bounds (sx, sy, sw, sh)
0x0A20 (2592)        blend_mode                   Active 2D blend mode
0x0A30 (2608..2656)  camera2d_state               Active 2D camera parameters
0x0BB8 (3000..3216)  3d_pipeline_state            Depth test, cull mode, lights & shadow states
0x0D00 (3300..3332)  render_target_backup         Saved window pointers during offscreen rendering
0x1000 (4096..4208)  audio_mixer_state            16-voice mixer table, master volume, 3D listener
0x1200 (4600..4632)  asset_manager_state          Asset cache descriptors, ref-counts & hit counters
```

---

## 3. Compiler & Preprocessor Integration (`tools/nexprep.c`)

### 1. Unquoted Module & Submodule Imports
`tools/nexprep.c` intercepts `import aether` and submodules (`import aether.math`, `import aether.gfx3d`, etc.) before file processing:
* `import aether` -> resolves to `include "stdlib/aether.nex"`.
* `import aether.<submod>` -> resolves to `include "stdlib/aether/<submod>.nex"`.

### 2. Fast ABI Calling Convention (`uses_nx_abi`)
To prevent the overhead of stack frame push/pop sequences for mathematical routines and per-pixel operations, all functions matching `ae_*` and `Ae*__*` utilize the register-slot ABI:
* Arguments pass in caller registers `nx_i0..nx_i19` without dynamic allocation.
* Return values pass in `nx_r0..nx_r3`.
* Caller-saved values use DAG call-graph coalescing.

### 3. Call-Graph Height Local Variable Coalescing (Phase 2B)
In large multi-subsystem graphical programs, local variable counts can exceed traditional stack limits. Phase 2B constructs the static call DAG across all non-recursive `ae_*` functions, computes the call-tree height $H$, and coalesces non-overlapping local variable slots:
$$\text{Slot}(v) = \text{\_lsh}H\_k$$
This allows thousands of local variables across hundreds of engine functions to share a compact pool of less than 200 physical stack slots without interference.

---

## 4. The XL Native Binary Pipeline

Standard Nexus executables utilize a 49,152-byte template (`compiler/template.bin`), providing 44,032 bytes of code space in `.text` (ending at `0xAC00`). While sufficient for compilers and utilities, modern graphics and audio libraries require larger code spaces.

Aether establishes the **XL Binary Toolchain**:

### Memory & Section Layout Comparison
| Header Attribute | Standard Binary (`template.bin`) | Extended Binary (`template_xl.bin`) |
| :--- | :--- | :--- |
| **PE File Size** | 49,152 bytes (`0xC000`) | 245,760 bytes (`0x3C000`) |
| **`.text` Code Capacity** | 44,032 bytes (`0xAC00`) | **241,152 bytes** (`0x3AC00`) |
| **`.rdata` File Offset** | `0xB000` | `0x3B000` (`+0x30000` shift) |
| **`.rdata` Virtual Address** | `0xC000` | `0x3C000` |
| **Built-in Helpers Offset** | `0xA800` (`43008`) | `0x3A800` (`239616`) |
| **Stack Frame Reservation** | 2,088 bytes (`0x828`) | **32,816 bytes** (`0x8030`, 4,096 variables) |
| **Zero-Init Var Count** | 128 variables (`0x80`) | **4,096 variables** (`0x1000`) |

### Direct Linux Kernel Execution (`tools/nexelf.c`)
`tools/nexelf.c` detects both standard (49 KB) and XL (245 KB) PE executables, generates valid 64-bit ELF headers (`e_machine = 0x3E`, `ET_EXEC`), and appends self-contained x86-64 machine-code syscall bridges that map PE section data into virtual memory (`0x400000..0x500000`) before jumping to `main`.

---

## 5. Software Rasterization Pipeline

### 1. 2D Primitive & Span Processing
All 2D drawing operates on 32-bit contiguous color buffers (`cbuf`). Primitive routines (rectangles, triangles, circles) decompose into horizontal spans:
1. Coordinates are transformed through the active `AeCamera2D` matrix.
2. The span is intersected with the active scissor rectangle bounds (`sx0, sy0, sw, sh`).
3. For opaque rendering (`AE_BLEND_OPAQUE`), pixels are written via bulk 32-bit stores.
4. For blended rendering (`AE_BLEND_ALPHA`, `AE_BLEND_ADDITIVE`, `AE_BLEND_MULTIPLY`), each destination pixel is read, blended via channel arithmetic, and written back.

### 2. 3D Scanline Z-Buffer Rasterization
1. **Model -> World**: Multi-return matrix multiplication transforms local model coordinates to world space.
2. **World -> Clip**: Vertices are transformed by the combined View-Projection matrix into homogeneous clip coordinates $(x_c, y_c, z_c, w_c)$.
3. **Clipping & Perspective Divide**: Triangles crossing the near clipping plane ($w_c \le 100$) are clipped. Vertices are normalized to NDC coordinates:
   $$x_{ndc} = \frac{x_c \cdot 1000}{w_c}, \quad y_{ndc} = \frac{y_c \cdot 1000}{w_c}$$
4. **Viewport Mapping**: NDC coordinates map to screen pixels $[0, \text{fbw}] \times [0, \text{fbh}]$.
5. **Backface Culling**: Screen-space signed area $A$ determines face orientation:
   $$A = (x_1 - x_0)(y_2 - y_0) - (y_1 - y_0)(x_2 - x_0)$$
   Depending on `cull_mode` (`AE_CULL_BACK` or `AE_CULL_FRONT`), back-facing polygons are discarded prior to pixel processing.
6. **Barycentric Interpolation & Z-Test**: For each rasterized pixel inside the triangle bounds, barycentric weights $w_0, w_1, w_2$ interpolate depth $z$ and texture coordinates $u, v$. If $z < \text{depth\_buffer}[p]$, the depth buffer is updated and the shaded surface color is written to the color buffer.

---

## 6. Multi-Voice Audio Mixer Architecture

The audio engine runs a deterministic software mixer producing signed 16-bit stereo PCM samples at 22,050 Hz:
* **Waveform Synthesis**: Phase accumulators advance at frequency step $\Delta \theta = \frac{f \cdot 360^\circ}{\text{sample\_rate}}$.
* **16 Voice Slots**: Each active voice maintains an independent sample cursor (with fixed-point fractional pitch stepping), volume multiplier, stereo pan position, and loop flag.
* **Spatial Attenuation**: For a sound at position $(s_x, s_y, s_z)$ and listener at $(l_x, l_y, l_z)$, Euclidean distance $d$ scales volume inversely:
  $$\text{atten} = \max\left(0, \frac{d_{max} - d}{d_{max}}\right)^2$$
  The dot product of the sound vector with the listener's right vector determines stereo pan $p \in [-1000, 1000]$.
* **Mixing & Clamping**: Accumulators sum contributions across all 16 voices, apply master volume scaling, track real-time peak amplitudes, and clamp outputs to $[-32767, 32767]$.
