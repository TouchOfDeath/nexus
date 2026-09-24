#!/usr/bin/env python3
"""
NEXUS Sprite & Pixel Art Compiler (nexsprite.py)
Converts ASCII art (.sprite) and PNG images into high-performance NEXUS sprite assets.
Features:
- Run-length span compression (skips transparent pixels completely)
- ASCII art grid parsing with custom color palettes
- PNG image conversion with auto alpha transparency / color-keying
- Multi-frame sprite sheet / animation support
- Generates clean, self-contained NEXUS source files (.nex)
"""

import sys, os, re

def parse_hex_color(hex_str):
    hex_str = hex_str.strip().lstrip('#')
    if len(hex_str) == 3:
        hex_str = ''.join([c*2 for c in hex_str])
    val = int(hex_str, 16)
    r = (val >> 16) & 0xFF
    g = (val >> 8) & 0xFF
    b = val & 0xFF
    return (r, g, b)

def compress_grid_to_spans(width, height, grid):
    """
    grid: 2D array [y][x] containing (r, g, b) or None for transparent.
    Returns list of spans: (dx, dy, span_len, r, g, b)
    """
    spans = []
    for y in range(height):
        x = 0
        while x < width:
            pixel = grid[y][x]
            if pixel is None:
                x += 1
                continue
            
            # Find run of identical colors on the same row
            run_len = 1
            while (x + run_len < width) and (grid[y][x + run_len] == pixel):
                run_len += 1
            
            r, g, b = pixel
            spans.append((x, y, run_len, r, g, b))
            x += run_len
    return spans

def parse_ascii_sprite(filepath):
    """
    Parses a .sprite ASCII art text file.
    Format:
    name: sprite_name
    palette:
      . = transparent
      Y = #f7d038
      W = #ffffff
      B = #000000
    pixels:
      . . Y Y . .
      . Y W B Y .
    """
    content = open(filepath, 'r').read()
    
    # Check for multiple frames
    frames = []
    frame_blocks = re.split(r'---+\s*', content)
    
    for block in frame_blocks:
        block = block.strip()
        if not block:
            continue
        
        name_match = re.search(r'name:\s*([a-zA-Z0-9_]+)', block)
        name = name_match.group(1) if name_match else os.path.splitext(os.path.basename(filepath))[0]
        
        palette = {'.': None, ' ': None}
        pal_match = re.search(r'palette:\s*([\s\S]*?)(?=pixels:|$)', block)
        if pal_match:
            for line in pal_match.group(1).strip().splitlines():
                line = line.strip()
                if not line or '=' not in line:
                    continue
                char, _, col_str = line.partition('=')
                char = char.strip()
                col_str = col_str.strip()
                if col_str.lower() in ('transparent', 'none', 'clear', '.'):
                    palette[char] = None
                else:
                    palette[char] = parse_hex_color(col_str)
        
        pix_match = re.search(r'pixels:\s*([\s\S]*)$', block)
        if not pix_match:
            continue
        
        raw_lines = [l for l in pix_match.group(1).splitlines() if l.strip()]
        grid = []
        for line in raw_lines:
            tokens = line.strip().split()
            # If tokens are space separated, use tokens; otherwise use individual characters
            if len(tokens) > 1 and all(len(t) == 1 for t in tokens):
                row = [palette.get(t, None) for t in tokens]
            else:
                row = [palette.get(c, None) for c in line.strip().replace(' ', '')]
            grid.append(row)
        
        if not grid:
            continue
            
        height = len(grid)
        width = max(len(r) for r in grid)
        # Pad shorter rows
        for r in grid:
            while len(r) < width:
                r.append(None)
                
        spans = compress_grid_to_spans(width, height, grid)
        frames.append({
            'name': name,
            'width': width,
            'height': height,
            'spans': spans
        })
    
    return frames

def parse_image_sprite(filepath):
    """
    Parses a PNG / BMP image file using PIL.
    """
    from PIL import Image
    im = Image.open(filepath).convert('RGBA')
    width, height = im.size
    
    grid = []
    for y in range(height):
        row = []
        for x in range(width):
            r, g, b, a = im.getpixel((x, y))
            if a < 128:
                row.append(None)
            else:
                row.append((r, g, b))
        grid.append(row)
        
    name = os.path.splitext(os.path.basename(filepath))[0]
    # sanitize name
    name = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    spans = compress_grid_to_spans(width, height, grid)
    return [{
        'name': name,
        'width': width,
        'height': height,
        'spans': spans
    }]

def generate_nexus_code(frames, out_filename=None):
    """
    Generates NEXUS source code with packed byte arrays and helper procedures.
    """
    lines = []
    lines.append("# ==============================================================================")
    lines.append("#                   AUTOGENERATED NEXUS SPRITE ASSET")
    lines.append(f"#             Generated by nexsprite - 0% C - 100% Native NEXUS")
    lines.append("# ==============================================================================")
    lines.append("")
    
    for f in frames:
        name = f['name']
        w = f['width']
        h = f['height']
        spans = f['spans']
        num_spans = len(spans)
        
        # Buffer size: 4 bytes header (width, height, spans_lo, spans_hi) + 6 bytes per span
        buf_size = 4 + num_spans * 6
        
        lines.append(f"# Sprite '{name}': {w}x{h}, {num_spans} spans ({buf_size} bytes)")
        lines.append(f"let {name}_sprite_data = alloc {buf_size}")
        lines.append(f"let {name}_sprite_w = {w}")
        lines.append(f"let {name}_sprite_h = {h}")
        lines.append(f"let {name}_sprite_spans = {num_spans}")
        lines.append("")
        lines.append(f"fn init_{name}_sprite {{")
        lines.append(f"    store [{name}_sprite_data + 0] {w}")
        lines.append(f"    store [{name}_sprite_data + 1] {h}")
        lines.append(f"    let s_lo = {num_spans % 256}")
        lines.append(f"    let s_hi = {num_spans // 256}")
        lines.append(f"    store [{name}_sprite_data + 2] s_lo")
        lines.append(f"    store [{name}_sprite_data + 3] s_hi")
        lines.append("")
        
        # Pack spans
        for i, (dx, dy, slen, r, g, b) in enumerate(spans):
            base = 4 + i * 6
            lines.append(f"    # Span {i}: pos=({dx},{dy}), len={slen}, rgb=({r},{g},{b})")
            lines.append(f"    store [{name}_sprite_data + {base + 0}] {dx}")
            lines.append(f"    store [{name}_sprite_data + {base + 1}] {dy}")
            lines.append(f"    store [{name}_sprite_data + {base + 2}] {slen}")
            lines.append(f"    store [{name}_sprite_data + {base + 3}] {r}")
            lines.append(f"    store [{name}_sprite_data + {base + 4}] {g}")
            lines.append(f"    store [{name}_sprite_data + {base + 5}] {b}")
            
        lines.append("}")
        lines.append("")
        lines.append(f"call init_{name}_sprite")
        lines.append("")
        
    return '\n'.join(lines)

def main():
    if len(sys.argv) < 2:
        print("NEXUS Sprite Compiler (nexsprite)")
        print("Usage:")
        print("  nexsprite <input.sprite|input.png> [output.nex]")
        sys.exit(1)
        
    infile = sys.argv[1]
    if not os.path.exists(infile):
        print(f"[-] Error: File not found: {infile}")
        sys.exit(1)
        
    ext = os.path.splitext(infile)[1].lower()
    if ext in ('.png', '.bmp', '.gif', '.jpg'):
        frames = parse_image_sprite(infile)
    else:
        frames = parse_ascii_sprite(infile)
        
    if not frames:
        print("[-] Error: No valid sprite frames parsed.")
        sys.exit(1)
        
    outfile = None
    i = 2
    while i < len(sys.argv):
        if sys.argv[i] == '-o' and i + 1 < len(sys.argv):
            outfile = sys.argv[i + 1]
            i += 2
        else:
            outfile = sys.argv[i]
            i += 1
    if not outfile:
        outfile = os.path.splitext(infile)[0] + "_sprite.nex"
    code = generate_nexus_code(frames, outfile)
    with open(outfile, 'w') as f:
        f.write(code)
        
    total_spans = sum(len(f['spans']) for f in frames)
    print(f"[+] Successfully compiled {len(frames)} sprite(s) ({total_spans} spans) -> {outfile}")

if __name__ == '__main__':
    main()
