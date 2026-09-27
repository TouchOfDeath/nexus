#!/usr/bin/env python3
"""
NEXUS Studio (nexstudio) - First-Class Official IDE for the NEXUS Programming Language.
Provides a modern visual editor with syntax highlighting, dynamic line numbering with error badges,
compiler diagnostic parsing, click-to-jump navigation, brace matching, search & replace,
auto-indentation, example browser, and instant F5 execution (compile & direct kernel execution).
"""

import os
import sys
import re
import queue
import threading
import subprocess
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog

# --- Theme Configuration (Catppuccin Mocha Palette) ---
THEME = {
    "bg": "#181825",
    "editor_bg": "#11111b",
    "fg": "#cdd6f4",
    "cursor": "#f5e0dc",
    "select_bg": "#45475a",
    "gutter_bg": "#181825",
    "gutter_fg": "#6c7086",
    "gutter_current": "#cdd6f4",
    "console_bg": "#11111b",
    "console_fg": "#a6adc8",
    "toolbar_bg": "#1e1e2e",
    "status_bg": "#181825",
    "status_fg": "#a6adc8",
    "current_line": "#181828",
    "error_line_bg": "#361a24",

    # Syntax Token Colors
    "kw_control": "#cba6f7",   # let, fn, call, return, if, else, while, for, struct
    "kw_builtin": "#89b4fa",   # print, read, alloc, load, store, syscall, abs, len
    "string": "#a6e3a1",       # "hello"
    "number": "#fab387",       # 42, 3.14159
    "comment": "#6c7086",      # # comment
    "fn_name": "#f9e2af",      # fn my_function
    "success": "#a6e3a1",
    "error": "#f38ba8",
    "info": "#89dceb",
    "warn": "#f9e2af",
    "find_bg": "#f9e2af",
    "find_fg": "#11111b"
}

KEYWORDS_CONTROL = {
    "let", "fn", "call", "return", "if", "else", "while", "for",
    "struct", "include", "import", "break", "continue",
    "assert_eq", "assert_ne", "if_f", "while_f"
}

KEYWORDS_BUILTIN = {
    "print", "print_str", "print_float", "read", "alloc",
    "load", "load16", "load32", "load64",
    "store", "store16", "store32", "store64",
    "len", "abs",
    "file_open", "file_create", "file_read", "file_write", "file_close",
    "syscall", "syscall0", "syscall1", "syscall2", "syscall3",
    "syscall4", "syscall5", "syscall6",
    "os_argc", "os_argv", "fadd", "fsub", "fmul", "fdiv", "fsqrt", "fneg", "itof", "ftoi"
}

STARTER_CODE = """# ==============================================================================
# Welcome to NEXUS Studio!
# Press [F5] to compile & run instantly on bare-metal Linux / Apple Silicon.
# ==============================================================================

fn greet(name) {
    print "Hello from NEXUS Studio!"
    print name
}

call greet("0% C# - 0% libc - 100% Native Machine Code")

# Quick computation demo:
let total = 0
for i = 1, 10, i = i + 1 {
    let total = total + i * i
}

print "Sum of squares (1..10):"
print total
"""


class LineNumbers(tk.Canvas):
    def __init__(self, parent, text_widget, **kwargs):
        super().__init__(parent, **kwargs)
        self.text_widget = text_widget
        self.error_lines = set()
        self.font_size = 10
        self.configure(width=52, bg=THEME["gutter_bg"], highlightthickness=0)

    def redraw(self):
        self.delete("all")
        i = self.text_widget.index("@0,0")
        while True:
            dline = self.text_widget.dlineinfo(i)
            if dline is None:
                break
            y = dline[1]
            h = dline[3]
            linenum_str = str(i).split(".")[0]
            linenum = int(linenum_str)

            if linenum in self.error_lines:
                # Draw high-visibility red error badge in gutter
                self.create_rectangle(3, y + 1, 49, y + h - 1, fill=THEME["error"], outline="")
                self.create_text(
                    45, y + 2,
                    anchor="ne",
                    text=f"! {linenum}",
                    fill="#11111b",
                    font=("Monospace", max(8, self.font_size - 1), "bold")
                )
            else:
                self.create_text(
                    45, y + 2,
                    anchor="ne",
                    text=linenum_str,
                    fill=THEME["gutter_fg"],
                    font=("Monospace", self.font_size)
                )
            i = self.text_widget.index(f"{i}+1line")


class NexusStudio(tk.Tk):
    def __init__(self, filename=None, repo_root=None):
        super().__init__()
        self.repo_root = repo_root or os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.current_file = None
        self.is_dirty = False
        self.running_proc = None
        self.log_queue = queue.Queue()
        self.font_size = 11
        self.error_lines = set()
        self.find_bar_visible = False

        self.title("NEXUS Studio - Official IDE for NEXUS")
        self.geometry("1100x750")
        self.minsize(700, 500)
        self.configure(bg=THEME["bg"])

        self._setup_styles()
        self._build_menu()
        self._build_toolbar()
        self._build_find_bar()
        self._build_main_split()
        self._build_statusbar()
        self._bind_events()
        self._check_log_queue()

        if filename and os.path.exists(filename):
            self.open_file(filename)
        elif filename:
            self.new_file("")
            self.current_file = os.path.abspath(filename)
            self.status_file.config(text=os.path.basename(self.current_file))
        else:
            self.new_file(STARTER_CODE)

    def _setup_styles(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TPanedwindow", background=THEME["bg"])
        style.configure("TFrame", background=THEME["bg"])
        style.configure("Toolbar.TFrame", background=THEME["toolbar_bg"])

    def _build_menu(self):
        menubar = tk.Menu(self, bg=THEME["toolbar_bg"], fg=THEME["fg"], activebackground=THEME["select_bg"], activeforeground=THEME["fg"])

        # File Menu
        file_menu = tk.Menu(menubar, tearoff=0, bg=THEME["toolbar_bg"], fg=THEME["fg"], activebackground=THEME["select_bg"])
        file_menu.add_command(label="New File", accelerator="Ctrl+N", command=lambda: self.new_file(STARTER_CODE))
        file_menu.add_command(label="Open File...", accelerator="Ctrl+O", command=self.open_file_dialog)
        file_menu.add_command(label="Save", accelerator="Ctrl+S", command=self.save_file)
        file_menu.add_command(label="Save As...", accelerator="Ctrl+Shift+S", command=self.save_file_as)
        file_menu.add_separator()

        # Examples Submenu
        examples_menu = tk.Menu(file_menu, tearoff=0, bg=THEME["toolbar_bg"], fg=THEME["fg"], activebackground=THEME["select_bg"])
        examples = [
            ("Hello World", "hello_world.nex"),
            ("Builtins (abs & len)", "builtins_demo.nex"),
            ("Fibonacci Sequence", "fibonacci.nex"),
            ("Factorial & Loops", "factorial.nex"),
            ("Strings Demo", "string_demo.nex"),
            ("Neural Network (XOR AI)", "neural_network.nex"),
            ("HTTP Web Server (Socket API)", "web_server.nex"),
            ("Deep Recursion (Ackermann/GCD)", "recursion_showcase.nex"),
            ("Flappy Bird (Pure X11 60 FPS)", "gui_flappy.nex"),
            ("Wolfenstein 3D Raycaster", "raycaster_3d.nex"),
            ("Mathematics & Simulation Ecosystem", "math_ecosystem_showcase.nex"),
            ("Orbital Rocket Physics", "rocket_physics.nex"),
            ("Interactive Calculator", "interactive_calc.nex"),
            ("Prime Sieve", "prime_sieve.nex"),
        ]
        for label, fname in examples:
            ex_path = os.path.join(self.repo_root, "examples", fname)
            examples_menu.add_command(label=label, command=lambda p=ex_path: self.open_file(p))
        file_menu.add_cascade(label="Open Example...", menu=examples_menu)

        file_menu.add_separator()
        file_menu.add_command(label="Exit", accelerator="Ctrl+Q", command=self.quit_app)
        menubar.add_cascade(label="File", menu=file_menu)

        # Edit Menu
        edit_menu = tk.Menu(menubar, tearoff=0, bg=THEME["toolbar_bg"], fg=THEME["fg"], activebackground=THEME["select_bg"])
        edit_menu.add_command(label="Undo", accelerator="Ctrl+Z", command=lambda: self.text.edit_undo())
        edit_menu.add_command(label="Redo", accelerator="Ctrl+Y", command=lambda: self.text.edit_redo())
        edit_menu.add_separator()
        edit_menu.add_command(label="Cut", accelerator="Ctrl+X", command=lambda: self.text.event_generate("<<Cut>>"))
        edit_menu.add_command(label="Copy", accelerator="Ctrl+C", command=lambda: self.text.event_generate("<<Copy>>"))
        edit_menu.add_command(label="Paste", accelerator="Ctrl+V", command=lambda: self.text.event_generate("<<Paste>>"))
        edit_menu.add_command(label="Select All", accelerator="Ctrl+A", command=self.select_all)
        edit_menu.add_separator()
        edit_menu.add_command(label="Toggle Comment", accelerator="Ctrl+/", command=self.toggle_comment)
        edit_menu.add_command(label="Find & Replace...", accelerator="Ctrl+F", command=self.toggle_find_bar)
        edit_menu.add_command(label="Go to Line...", accelerator="Ctrl+G", command=self.goto_line_dialog)
        menubar.add_cascade(label="Edit", menu=edit_menu)

        # View Menu
        view_menu = tk.Menu(menubar, tearoff=0, bg=THEME["toolbar_bg"], fg=THEME["fg"], activebackground=THEME["select_bg"])
        view_menu.add_command(label="Zoom In", accelerator="Ctrl++", command=self.zoom_in)
        view_menu.add_command(label="Zoom Out", accelerator="Ctrl+-", command=self.zoom_out)
        view_menu.add_command(label="Reset Zoom", accelerator="Ctrl+0", command=self.zoom_reset)
        view_menu.add_separator()
        view_menu.add_command(label="Clear Console Output", command=self.clear_console)
        menubar.add_cascade(label="View", menu=view_menu)

        # Run Menu
        run_menu = tk.Menu(menubar, tearoff=0, bg=THEME["toolbar_bg"], fg=THEME["fg"], activebackground=THEME["select_bg"])
        run_menu.add_command(label="Run Program", accelerator="F5", command=self.run_program)
        run_menu.add_command(label="Compile Tri-Platform (PE/ELF/Mach-O)", accelerator="F6", command=self.compile_tri)
        run_menu.add_command(label="Compile Apple Silicon ARM64 Mach-O", command=self.compile_arm64)
        run_menu.add_command(label="Run Full Regression Test Suite", command=self.run_full_test)
        run_menu.add_command(label="Open Interactive REPL Shell", command=self.open_repl_shell)
        run_menu.add_separator()
        run_menu.add_command(label="Stop Execution", command=self.stop_execution)
        menubar.add_cascade(label="Run", menu=run_menu)

        # Help Menu
        help_menu = tk.Menu(menubar, tearoff=0, bg=THEME["toolbar_bg"], fg=THEME["fg"], activebackground=THEME["select_bg"])
        help_menu.add_command(label="Language Quick Reference", command=self.show_reference)
        help_menu.add_command(label="About NEXUS Studio", command=self.show_about)
        menubar.add_cascade(label="Help", menu=help_menu)

        self.config(menu=menubar)

    def _build_toolbar(self):
        tb = tk.Frame(self, bg=THEME["toolbar_bg"], height=38)
        tb.pack(side="top", fill="x")

        def make_btn(text, cmd, color=THEME["fg"], bg=THEME["toolbar_bg"]):
            b = tk.Button(tb, text=text, command=cmd, fg=color, bg=bg,
                          activeforeground="#ffffff", activebackground=THEME["select_bg"],
                          relief="flat", padx=10, pady=4, font=("Monospace", 9, "bold"),
                          bd=0, cursor="hand2")
            b.pack(side="left", padx=3, pady=4)
            return b

        make_btn("▶  Run (F5)", self.run_program, color="#a6e3a1")
        make_btn("⚙  Compile Tri (F6)", self.compile_tri, color="#89b4fa")
        make_btn("■  Stop", self.stop_execution, color="#f38ba8")
        make_btn("📂 Open", self.open_file_dialog)
        make_btn("💾 Save", self.save_file)
        make_btn("🔍 Find", self.toggle_find_bar)
        make_btn("💬 Comment", self.toggle_comment)
        make_btn("⚡ REPL", self.open_repl_shell, color="#f9e2af")
        make_btn("🧹 Clear", self.clear_console)

        # Target badge on the right
        badge = tk.Label(tb, text="NEXUS v6.3 • BARE-METAL AOT",
                         bg="#1e1e2e", fg="#cba6f7", font=("Monospace", 8, "bold"), padx=10)
        badge.pack(side="right", padx=10)

    def _build_find_bar(self):
        self.find_frame = tk.Frame(self, bg="#1e1e2e", height=32, padx=8, pady=4)

        tk.Label(self.find_frame, text="Find:", bg="#1e1e2e", fg=THEME["fg"], font=("Monospace", 9)).pack(side="left", padx=4)
        self.find_entry = tk.Entry(self.find_frame, bg=THEME["editor_bg"], fg=THEME["fg"],
                                   insertbackground=THEME["cursor"], font=("Monospace", 10), width=18, bd=1, relief="solid")
        self.find_entry.pack(side="left", padx=4)
        self.find_entry.bind("<Return>", lambda e: self.find_next())

        tk.Label(self.find_frame, text="Replace:", bg="#1e1e2e", fg=THEME["fg"], font=("Monospace", 9)).pack(side="left", padx=4)
        self.replace_entry = tk.Entry(self.find_frame, bg=THEME["editor_bg"], fg=THEME["fg"],
                                      insertbackground=THEME["cursor"], font=("Monospace", 10), width=18, bd=1, relief="solid")
        self.replace_entry.pack(side="left", padx=4)
        self.replace_entry.bind("<Return>", lambda e: self.replace_match())

        def make_f_btn(text, cmd):
            b = tk.Button(self.find_frame, text=text, command=cmd, fg=THEME["fg"], bg="#313244",
                          activebackground=THEME["select_bg"], font=("Monospace", 8), padx=6, pady=2, bd=0, relief="flat", cursor="hand2")
            b.pack(side="left", padx=2)
            return b

        make_f_btn("Next", self.find_next)
        make_f_btn("Replace", self.replace_match)
        make_f_btn("Replace All", self.replace_all)
        make_f_btn("✕", self.toggle_find_bar)

    def toggle_find_bar(self, event=None):
        if self.find_bar_visible:
            self.find_frame.pack_forget()
            self.find_bar_visible = False
            self.text.tag_remove("find_match", "1.0", tk.END)
            self.text.focus_set()
        else:
            self.find_frame.pack(side="top", fill="x", before=self.paned)
            self.find_bar_visible = True
            self.find_entry.focus_set()
            self.find_entry.select_range(0, tk.END)
        return "break"

    def find_next(self):
        query = self.find_entry.get()
        if not query:
            return
        self.text.tag_remove("find_match", "1.0", tk.END)
        start = self.text.index(f"{tk.INSERT}+1c")
        pos = self.text.search(query, start, tk.END)
        if not pos:
            # Wrap around
            pos = self.text.search(query, "1.0", start)
        if pos:
            end = f"{pos}+{len(query)}c"
            self.text.tag_add("find_match", pos, end)
            self.text.mark_set(tk.INSERT, end)
            self.text.see(pos)
            self._highlight_current_line()

    def replace_match(self):
        query = self.find_entry.get()
        repl = self.replace_entry.get()
        if not query:
            return
        try:
            sel_start = self.text.index(tk.SEL_FIRST)
            sel_end = self.text.index(tk.SEL_LAST)
            if self.text.get(sel_start, sel_end) == query:
                self.text.delete(sel_start, sel_end)
                self.text.insert(sel_start, repl)
        except tk.TclError:
            pass
        self.find_next()

    def replace_all(self):
        query = self.find_entry.get()
        repl = self.replace_entry.get()
        if not query:
            return
        count = 0
        pos = "1.0"
        while True:
            pos = self.text.search(query, pos, tk.END)
            if not pos:
                break
            end = f"{pos}+{len(query)}c"
            self.text.delete(pos, end)
            self.text.insert(pos, repl)
            pos = f"{pos}+{len(repl)}c"
            count += 1
        self.log(f"[+] Replaced {count} occurrences of '{query}'.\n", "info")
        self.highlight_syntax()
        self.line_numbers.redraw()

    def _build_main_split(self):
        self.paned = tk.PanedWindow(self, orient=tk.VERTICAL, bg=THEME["bg"], sashwidth=4, sashrelief="flat")
        self.paned.pack(fill="both", expand=True)

        # Top Pane: Code Editor
        editor_frame = tk.Frame(self.paned, bg=THEME["editor_bg"])
        self.paned.add(editor_frame, minsize=200, height=460)

        # Scrollbars
        scroll_y = tk.Scrollbar(editor_frame, bg=THEME["bg"])
        scroll_y.pack(side="right", fill="y")
        scroll_x = tk.Scrollbar(editor_frame, orient="horizontal", bg=THEME["bg"])
        scroll_x.pack(side="bottom", fill="x")

        # Text Area
        self.text = tk.Text(
            editor_frame,
            wrap="none",
            bg=THEME["editor_bg"],
            fg=THEME["fg"],
            insertbackground=THEME["cursor"],
            selectbackground=THEME["select_bg"],
            font=("Monospace", self.font_size),
            undo=True,
            padx=8,
            pady=8,
            yscrollcommand=self._on_text_scroll,
            xscrollcommand=scroll_x.set,
            relief="flat",
            bd=0
        )
        scroll_y.config(command=self._on_scroll_y)
        scroll_x.config(command=self.text.xview)

        # Line numbers
        self.line_numbers = LineNumbers(editor_frame, self.text)
        self.line_numbers.pack(side="left", fill="y")
        self.text.pack(side="left", fill="both", expand=True)

        # Configure Syntax Highlight Tags
        self._update_tag_fonts()

        # Bottom Pane: Console / Output
        console_frame = tk.Frame(self.paned, bg=THEME["console_bg"])
        self.paned.add(console_frame, minsize=100, height=200)

        console_title = tk.Frame(console_frame, bg=THEME["toolbar_bg"], height=24)
        console_title.pack(side="top", fill="x")
        tk.Label(console_title, text="TERMINAL / CONSOLE OUTPUT (Click error line to jump)",
                 bg=THEME["toolbar_bg"], fg=THEME["status_fg"],
                 font=("Monospace", 8, "bold"), padx=8, pady=3).pack(side="left")

        con_scroll = tk.Scrollbar(console_frame, bg=THEME["bg"])
        con_scroll.pack(side="right", fill="y")

        self.console = tk.Text(
            console_frame,
            wrap="word",
            bg=THEME["console_bg"],
            fg=THEME["console_fg"],
            font=("Monospace", 10),
            yscrollcommand=con_scroll.set,
            relief="flat",
            padx=8,
            pady=6,
            bd=0
        )
        con_scroll.config(command=self.console.yview)
        self.console.pack(fill="both", expand=True)

        self.console.tag_configure("success", foreground=THEME["success"], font=("Monospace", 10, "bold"))
        self.console.tag_configure("error", foreground=THEME["error"], font=("Monospace", 10, "bold"))
        self.console.tag_configure("info", foreground=THEME["info"])
        self.console.tag_configure("warn", foreground=THEME["warn"])
        self.console.tag_configure("err_link", foreground=THEME["error"], font=("Monospace", 10, "bold", "underline"))
        self.console.tag_bind("err_link", "<Enter>", lambda e: self.console.config(cursor="hand2"))
        self.console.tag_bind("err_link", "<Leave>", lambda e: self.console.config(cursor="xterm"))

    def _update_tag_fonts(self):
        sz = self.font_size
        self.text.configure(font=("Monospace", sz))
        self.text.tag_configure("kw_control", foreground=THEME["kw_control"], font=("Monospace", sz, "bold"))
        self.text.tag_configure("kw_builtin", foreground=THEME["kw_builtin"], font=("Monospace", sz))
        self.text.tag_configure("string", foreground=THEME["string"], font=("Monospace", sz))
        self.text.tag_configure("number", foreground=THEME["number"], font=("Monospace", sz))
        self.text.tag_configure("comment", foreground=THEME["comment"], font=("Monospace", sz, "italic"))
        self.text.tag_configure("fn_name", foreground=THEME["fn_name"], font=("Monospace", sz, "bold"))
        self.text.tag_configure("current_line", background=THEME["current_line"])
        self.text.tag_configure("error_line", background=THEME["error_line_bg"], underline=True)
        self.text.tag_configure("matching_brace", foreground="#f9e2af", background="#313244", font=("Monospace", sz, "bold"))
        self.text.tag_configure("find_match", background=THEME["find_bg"], foreground=THEME["find_fg"])
        self.line_numbers.font_size = max(8, sz - 1)
        self.line_numbers.redraw()

    def zoom_in(self, event=None):
        if self.font_size < 24:
            self.font_size += 1
            self._update_tag_fonts()
        return "break"

    def zoom_out(self, event=None):
        if self.font_size > 8:
            self.font_size -= 1
            self._update_tag_fonts()
        return "break"

    def zoom_reset(self, event=None):
        self.font_size = 11
        self._update_tag_fonts()
        return "break"

    def select_all(self, event=None):
        self.text.tag_add(tk.SEL, "1.0", tk.END)
        self.text.mark_set(tk.INSERT, "1.0")
        self.text.see(tk.INSERT)
        return "break"

    def _build_statusbar(self):
        self.statusbar = tk.Frame(self, bg=THEME["status_bg"], height=22)
        self.statusbar.pack(side="bottom", fill="x")

        self.status_file = tk.Label(self.statusbar, text="Untitled", bg=THEME["status_bg"],
                                    fg=THEME["status_fg"], font=("Monospace", 8))
        self.status_file.pack(side="left", padx=8)

        self.status_pos = tk.Label(self.statusbar, text="Ln 1, Col 0", bg=THEME["status_bg"],
                                    fg=THEME["status_fg"], font=("Monospace", 8))
        self.status_pos.pack(side="right", padx=8)

        self.status_info = tk.Label(self.statusbar, text="NEXUS v6.3 (100% Native AOT)",
                                    bg=THEME["status_bg"], fg=THEME["kw_control"], font=("Monospace", 8, "bold"))
        self.status_info.pack(side="right", padx=16)

    def _bind_events(self):
        self.text.bind("<KeyRelease>", self._on_key_release)
        self.text.bind("<Return>", self._on_return)
        self.text.bind("<Tab>", self._on_tab)
        self.text.bind("<BackSpace>", self._on_backspace)
        self.text.bind("<ButtonRelease-1>", self._on_click)

        self.bind("<F5>", lambda e: self.run_program())
        self.bind("<F6>", lambda e: self.compile_tri())
        self.bind("<Control-n>", lambda e: self.new_file(STARTER_CODE))
        self.bind("<Control-o>", lambda e: self.open_file_dialog())
        self.bind("<Control-s>", lambda e: self.save_file())
        self.bind("<Control-S>", lambda e: self.save_file_as())
        self.bind("<Control-q>", lambda e: self.quit_app())
        self.bind("<Control-f>", self.toggle_find_bar)
        self.bind("<Control-g>", self.goto_line_dialog)
        self.bind("<Control-slash>", self.toggle_comment)
        self.bind("<Control-plus>", self.zoom_in)
        self.bind("<Control-equal>", self.zoom_in)
        self.bind("<Control-minus>", self.zoom_out)
        self.bind("<Control-0>", self.zoom_reset)
        self.bind("<Escape>", lambda e: self.toggle_find_bar() if self.find_bar_visible else None)

    def _on_scroll_y(self, *args):
        self.text.yview(*args)
        self.line_numbers.redraw()

    def _on_text_scroll(self, first, last):
        self.line_numbers.redraw()
        return first, last

    def _update_cursor_pos(self, event=None):
        pos = self.text.index(tk.INSERT)
        line, col = pos.split(".")
        self.status_pos.config(text=f"Ln {line}, Col {col}")

    def _highlight_current_line(self):
        self.text.tag_remove("current_line", "1.0", tk.END)
        insert = self.text.index(tk.INSERT)
        self.text.tag_add("current_line", f"{insert} linestart", f"{insert} lineend + 1c")

    def _match_braces(self):
        self.text.tag_remove("matching_brace", "1.0", tk.END)
        cursor = self.text.index(tk.INSERT)
        # Check char before cursor and char at cursor
        for pos in [f"{cursor}-1c", cursor]:
            ch = self.text.get(pos, f"{pos}+1c")
            if ch in "{[(":
                close_ch = {"{": "}", "[": "]", "(": ")"}[ch]
                depth = 1
                cur = self.text.index(f"{pos}+1c")
                while cur and depth > 0:
                    c = self.text.get(cur, f"{cur}+1c")
                    if not c:
                        break
                    if c == ch:
                        depth += 1
                    elif c == close_ch:
                        depth -= 1
                        if depth == 0:
                            self.text.tag_add("matching_brace", pos, f"{pos}+1c")
                            self.text.tag_add("matching_brace", cur, f"{cur}+1c")
                            return
                    cur = self.text.index(f"{cur}+1c")
            elif ch in "}])":
                open_ch = {"}": "{", "]": "[", ")": "("}[ch]
                depth = 1
                cur = self.text.index(f"{pos}-1c")
                while cur and depth > 0:
                    c = self.text.get(cur, f"{cur}+1c")
                    if not c or cur == "1.0":
                        break
                    if c == ch:
                        depth += 1
                    elif c == open_ch:
                        depth -= 1
                        if depth == 0:
                            self.text.tag_add("matching_brace", pos, f"{pos}+1c")
                            self.text.tag_add("matching_brace", cur, f"{cur}+1c")
                            return
                    cur = self.text.index(f"{cur}-1c")

    def _on_click(self, event=None):
        self._update_cursor_pos()
        self._highlight_current_line()
        self._match_braces()

    def _on_key_release(self, event=None):
        self._update_cursor_pos()
        self._highlight_current_line()
        self._match_braces()
        self.line_numbers.redraw()
        self.highlight_syntax()
        if not self.is_dirty:
            self.is_dirty = True
            fname = os.path.basename(self.current_file) if self.current_file else "Untitled"
            self.status_file.config(text=f"{fname} *")

    def _on_return(self, event):
        # Auto-indentation
        cursor = self.text.index(tk.INSERT)
        curr_line = self.text.get(f"{cursor} linestart", f"{cursor} lineend")
        indent = len(curr_line) - len(curr_line.lstrip(" "))
        extra = 4 if curr_line.rstrip().endswith("{") else 0
        new_indent = " " * (indent + extra)

        self.text.insert(tk.INSERT, "\n" + new_indent)
        self.text.see(tk.INSERT)
        self._on_key_release()
        return "break"

    def _on_tab(self, event):
        # Insert 4 spaces
        self.text.insert(tk.INSERT, "    ")
        self._on_key_release()
        return "break"

    def _on_backspace(self, event):
        # Smart un-indent 4 spaces
        cursor = self.text.index(tk.INSERT)
        line_start = self.text.get(f"{cursor} linestart", cursor)
        if line_start and line_start.isspace() and len(line_start) % 4 == 0:
            self.text.delete(f"{cursor}-4c", cursor)
            self._on_key_release()
            return "break"
        return None

    def toggle_comment(self, event=None):
        try:
            sel_start = self.text.index(tk.SEL_FIRST)
            sel_end = self.text.index(tk.SEL_LAST)
            start_line = int(sel_start.split(".")[0])
            end_line = int(sel_end.split(".")[0])
        except tk.TclError:
            cursor = self.text.index(tk.INSERT)
            start_line = int(cursor.split(".")[0])
            end_line = start_line

        all_commented = True
        for ln in range(start_line, end_line + 1):
            line_text = self.text.get(f"{ln}.0", f"{ln}.end")
            if line_text.strip() and not line_text.lstrip().startswith("#"):
                all_commented = False
                break

        for ln in range(start_line, end_line + 1):
            line_text = self.text.get(f"{ln}.0", f"{ln}.end")
            if all_commented:
                if "#" in line_text:
                    idx = line_text.find("#")
                    delete_len = 2 if len(line_text) > idx + 1 and line_text[idx + 1] == " " else 1
                    self.text.delete(f"{ln}.{idx}", f"{ln}.{idx + delete_len}")
            else:
                indent = len(line_text) - len(line_text.lstrip(" "))
                self.text.insert(f"{ln}.{indent}", "# ")

        self._on_key_release()
        return "break"

    def goto_line_dialog(self, event=None):
        target = simpledialog.askinteger("Go to Line", "Enter line number:", parent=self, minvalue=1)
        if target:
            self.jump_to_line(target)
        return "break"

    def jump_to_line(self, line_num):
        pos = f"{line_num}.0"
        self.text.mark_set(tk.INSERT, pos)
        self.text.see(pos)
        self.text.focus_set()
        self._update_cursor_pos()
        self._highlight_current_line()

    def highlight_syntax(self):
        content = self.text.get("1.0", tk.END)
        for tag in ["kw_control", "kw_builtin", "string", "number", "comment", "fn_name"]:
            self.text.tag_remove(tag, "1.0", tk.END)

        # 1. Comments: # and ; to end of line
        for match in re.finditer(r"(#|;).*$", content, re.MULTILINE):
            self.text.tag_add("comment", f"1.0+{match.start()}c", f"1.0+{match.end()}c")

        # 2. Strings: "..."
        for match in re.finditer(r'"(\\.|[^"\\])*"', content):
            self.text.tag_add("string", f"1.0+{match.start()}c", f"1.0+{match.end()}c")

        # 3. Numbers: float or integer
        for match in re.finditer(r"\b\d+(\.\d+)?\b", content):
            self.text.tag_add("number", f"1.0+{match.start()}c", f"1.0+{match.end()}c")

        # 4. Function definitions: fn <ident>
        for match in re.finditer(r"\bfn\s+([a-zA-Z_][a-zA-Z0-9_]*)", content):
            start = match.start(1)
            end = match.end(1)
            self.text.tag_add("fn_name", f"1.0+{start}c", f"1.0+{end}c")

        # 5. Keywords
        for match in re.finditer(r"\b([a-zA-Z_][a-zA-Z0-9_]*)\b", content):
            word = match.group(1)
            start = match.start(1)
            end = match.end(1)
            tags = self.text.tag_names(f"1.0+{start}c")
            if "comment" in tags or "string" in tags:
                continue
            if word in KEYWORDS_CONTROL:
                self.text.tag_add("kw_control", f"1.0+{start}c", f"1.0+{end}c")
            elif word in KEYWORDS_BUILTIN:
                self.text.tag_add("kw_builtin", f"1.0+{start}c", f"1.0+{end}c")

    def clear_error_highlights(self):
        self.error_lines.clear()
        self.line_numbers.error_lines.clear()
        self.text.tag_remove("error_line", "1.0", tk.END)
        self.line_numbers.redraw()

    def mark_error_line(self, line_num):
        self.error_lines.add(line_num)
        self.line_numbers.error_lines.add(line_num)
        self.text.tag_add("error_line", f"{line_num}.0", f"{line_num}.end + 1c")
        self.line_numbers.redraw()
        self.jump_to_line(line_num)

    def map_compiler_line(self, code_line):
        """Map code.nex 1-based line number to editor 1-based line number."""
        candidate = max(1, code_line - 4)
        try:
            total_editor_lines = int(self.text.index("end - 1c").split(".")[0])
        except Exception:
            total_editor_lines = 1

        code_nex_path = os.path.join(self.repo_root, "compiler", "code.nex")
        if os.path.exists(code_nex_path):
            try:
                with open(code_nex_path, "r", encoding="utf-8", errors="ignore") as f:
                    all_code_lines = f.readlines()
                if 1 <= code_line <= len(all_code_lines):
                    target_text = all_code_lines[code_line - 1].strip()
                    cand_text = self.text.get(f"{candidate}.0", f"{candidate}.end").strip()
                    first_tok = target_text.split()[0] if target_text.split() else ""
                    if cand_text and (cand_text in target_text or (first_tok and cand_text.startswith(first_tok))):
                        return candidate
                    for i in range(1, total_editor_lines + 1):
                        ed_text = self.text.get(f"{i}.0", f"{i}.end").strip()
                        if ed_text and (ed_text in target_text or (first_tok and ed_text.startswith(first_tok))):
                            return i
            except Exception:
                pass
        return min(candidate, max(1, total_editor_lines))

    def new_file(self, content=""):
        self.clear_error_highlights()
        self.text.delete("1.0", tk.END)
        self.text.insert("1.0", content)
        self.current_file = None
        self.is_dirty = False
        self.status_file.config(text="Untitled")
        self.highlight_syntax()
        self._highlight_current_line()
        self.line_numbers.redraw()

    def open_file(self, filepath):
        if not os.path.exists(filepath):
            messagebox.showerror("File Error", f"File not found: {filepath}")
            return
        self.clear_error_highlights()
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            code = f.read()
        self.text.delete("1.0", tk.END)
        self.text.insert("1.0", code)
        self.current_file = os.path.abspath(filepath)
        self.is_dirty = False
        self.status_file.config(text=os.path.basename(self.current_file))
        self.highlight_syntax()
        self._highlight_current_line()
        self.line_numbers.redraw()
        self.log(f"[+] Loaded: {self.current_file}\n", "info")

    def open_file_dialog(self):
        f = filedialog.askopenfilename(
            initialdir=os.path.join(self.repo_root, "examples"),
            filetypes=[("NEXUS Source Files", "*.nex"), ("All Files", "*.*")]
        )
        if f:
            self.open_file(f)

    def save_file(self):
        if not self.current_file:
            return self.save_file_as()
        with open(self.current_file, "w", encoding="utf-8") as f:
            f.write(self.text.get("1.0", tk.END).rstrip() + "\n")
        self.is_dirty = False
        self.status_file.config(text=os.path.basename(self.current_file))
        return True

    def save_file_as(self):
        f = filedialog.asksaveasfilename(
            initialdir=os.path.join(self.repo_root, "examples"),
            defaultextension=".nex",
            filetypes=[("NEXUS Source Files", "*.nex"), ("All Files", "*.*")]
        )
        if not f:
            return False
        self.current_file = os.path.abspath(f)
        return self.save_file()

    def _check_log_queue(self):
        try:
            while not self.log_queue.empty():
                item = self.log_queue.get_nowait()
                if item is None:
                    break
                action, args = item
                if action == "log":
                    text, tag, err_line = args
                    start_idx = self.console.index(tk.END)
                    self.console.insert(tk.END, text, tag)
                    if err_line:
                        # Make error diagnostic clickable
                        end_idx = self.console.index(f"{start_idx}+{len(text)}c")
                        self.console.tag_add("err_link", start_idx, end_idx)
                        self.console.tag_bind("err_link", "<Button-1>", lambda e, ln=err_line: self.jump_to_line(ln))
                    self.console.see(tk.END)
                elif action == "error_line":
                    code_ln = args[0]
                    ed_ln = self.map_compiler_line(code_ln)
                    start_idx = self.console.index(tk.END)
                    text = f" {ed_ln}  (editor line {ed_ln})\n"
                    self.console.insert(tk.END, text, "error")
                    end_idx = self.console.index(f"{start_idx}+{len(text)}c")
                    self.console.tag_add("err_link", start_idx, end_idx)
                    self.console.tag_bind("err_link", "<Button-1>", lambda e, ln=ed_ln: self.jump_to_line(ln))
                    self.mark_error_line(ed_ln)
                    self.console.see(tk.END)
                elif action == "clear":
                    self.console.delete("1.0", tk.END)
        except Exception:
            pass
        self.after(50, self._check_log_queue)

    def log(self, text, tag=None, err_line=None):
        self.log_queue.put(("log", (text, tag, err_line)))

    def clear_console(self):
        self.log_queue.put(("clear", ()))

    def run_program(self):
        """Save file and run via ./nexus run <file> in a background thread."""
        self.clear_error_highlights()
        if not self.current_file:
            tmp_path = os.path.join(self.repo_root, "compiler", "temp_studio_app.nex")
            with open(tmp_path, "w", encoding="utf-8") as f:
                f.write(self.text.get("1.0", tk.END).rstrip() + "\n")
            target = tmp_path
        else:
            self.save_file()
            target = self.current_file

        self.clear_console()
        self.log(f"=== [F5] Executing {os.path.basename(target)} ===\n", "info")
        self.log(f"Driver: ./nexus run {target}\n\n", "info")

        def target_thread():
            t0 = time.time()
            driver = os.path.join(self.repo_root, "nexus")
            cmd = [driver, "run", target]
            try:
                proc = subprocess.Popen(
                    cmd,
                    cwd=self.repo_root,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1
                )
                self.running_proc = proc
                expect_line_number = False
                for line in proc.stdout:
                    # Check for compiler diagnostic line
                    m_line = re.search(r"\[!\]\s*Line:\s*(\d+)", line)
                    if m_line:
                        code_ln = int(m_line.group(1))
                        self.log_queue.put(("error_line", (code_ln,)))
                    elif line.strip() == "[!] Line:" or line.strip().startswith("[!] Line:"):
                        expect_line_number = True
                        self.log(line, "error")
                    elif expect_line_number:
                        expect_line_number = False
                        try:
                            code_ln = int(line.strip())
                            self.log_queue.put(("error_line", (code_ln,)))
                        except ValueError:
                            self.log(line, "error")
                    elif "error" in line.lower() or "[!]" in line or "fail" in line.lower():
                        self.log(line, "error")
                    else:
                        self.log(line)
                proc.wait()
                dt = (time.time() - t0) * 1000
                if proc.returncode == 0:
                    self.log(f"\n[+] Process finished with exit code 0 ({dt:.0f} ms)\n", "success")
                else:
                    self.log(f"\n[-] Process terminated with exit code {proc.returncode} ({dt:.0f} ms)\n", "error")
            except Exception as e:
                self.log(f"\n[-] Execution error: {e}\n", "error")
            finally:
                self.running_proc = None

        threading.Thread(target=target_thread, daemon=True).start()

    def compile_tri(self):
        """Compile tri-platform: PE32+, ELF64, Mach-O."""
        self.clear_error_highlights()
        if not self.current_file:
            if not self.save_file_as():
                return
        else:
            self.save_file()

        target = self.current_file
        self.clear_console()
        self.log(f"=== [F6] Compiling Tri-Platform: {os.path.basename(target)} ===\n", "info")

        def target_thread():
            driver = os.path.join(self.repo_root, "nexus")
            cmd = [driver, "compile", target]
            try:
                proc = subprocess.Popen(
                    cmd,
                    cwd=self.repo_root,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True
                )
                self.running_proc = proc
                out, _ = proc.communicate()
                expect_line_number = False
                for line in out.splitlines(keepends=True):
                    m_line = re.search(r"\[!\]\s*Line:\s*(\d+)", line)
                    if m_line:
                        code_ln = int(m_line.group(1))
                        self.log_queue.put(("error_line", (code_ln,)))
                    elif line.strip() == "[!] Line:" or line.strip().startswith("[!] Line:"):
                        expect_line_number = True
                        self.log(line, "error")
                    elif expect_line_number:
                        expect_line_number = False
                        try:
                            code_ln = int(line.strip())
                            self.log_queue.put(("error_line", (code_ln,)))
                        except ValueError:
                            self.log(line, "error")
                    elif "error" in line.lower() or "fail" in line.lower() or "[!]" in line:
                        self.log(line, "error")
                    else:
                        self.log(line)
                if proc.returncode == 0:
                    self.log("\n[+] Tri-Platform Executables Ready!\n", "success")
                else:
                    self.log(f"\n[-] Compilation failed with code {proc.returncode}\n", "error")
            except Exception as e:
                self.log(f"\n[-] Compilation error: {e}\n", "error")
            finally:
                self.running_proc = None

        threading.Thread(target=target_thread, daemon=True).start()

    def compile_arm64(self):
        """Compile directly to Apple Silicon ARM64 Mach-O."""
        self.clear_error_highlights()
        if not self.current_file:
            if not self.save_file_as():
                return
        else:
            self.save_file()

        target = self.current_file
        self.clear_console()
        self.log(f"=== Compiling Native Apple Silicon ARM64: {os.path.basename(target)} ===\n", "info")

        def target_thread():
            driver = os.path.join(self.repo_root, "nexus")
            cmd = [driver, "compile", "--target", "arm64-macos", target]
            try:
                proc = subprocess.Popen(
                    cmd,
                    cwd=self.repo_root,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True
                )
                self.running_proc = proc
                out, _ = proc.communicate()
                self.log(out)
            except Exception as e:
                self.log(f"[-] Error: {e}\n", "error")
            finally:
                self.running_proc = None

        threading.Thread(target=target_thread, daemon=True).start()

    def run_full_test(self):
        self.clear_console()
        self.log("=== Running Full NEXUS Regression Test Suite ===\n", "info")

        def target_thread():
            driver = os.path.join(self.repo_root, "nexus")
            cmd = [driver, "test"]
            try:
                proc = subprocess.Popen(cmd, cwd=self.repo_root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                self.running_proc = proc
                for line in proc.stdout:
                    if "PASS" in line or "verified" in line:
                        self.log(line, "success")
                    elif "FAIL" in line or "error" in line.lower():
                        self.log(line, "error")
                    else:
                        self.log(line)
                proc.wait()
                if proc.returncode == 0:
                    self.log("\n[+] ALL TEST SUITES PASSED!\n", "success")
            except Exception as e:
                self.log(f"[-] Error: {e}\n", "error")
            finally:
                self.running_proc = None

        threading.Thread(target=target_thread, daemon=True).start()

    def open_repl_shell(self):
        """Launches the interactive stateful NEXUS REPL."""
        driver = os.path.join(self.repo_root, "nexus")
        # Try running in a terminal emulator if available, else in console
        terms = [
            ["x-terminal-emulator", "-e", driver, "repl"],
            ["gnome-terminal", "--", driver, "repl"],
            ["konsole", "-e", driver, "repl"],
            ["xfce4-terminal", "-e", f"{driver} repl"],
            ["xterm", "-e", driver, "repl"]
        ]
        launched = False
        for t in terms:
            try:
                subprocess.Popen(t)
                launched = True
                self.log("[+] Launched Interactive REPL terminal.\n", "success")
                break
            except Exception:
                continue
        if not launched:
            self.log("[i] Run './nexus repl' in your terminal to start the interactive REPL.\n", "info")

    def stop_execution(self):
        if self.running_proc:
            try:
                self.running_proc.terminate()
                self.log("\n[!] Execution terminated by user.\n", "warn")
            except Exception:
                pass
            self.running_proc = None

    def show_reference(self):
        ref_path = os.path.join(self.repo_root, "docs", "LANGUAGE_REFERENCE.md")
        if os.path.exists(ref_path):
            self.open_file(ref_path)
        else:
            messagebox.showinfo("Reference", "See docs/LANGUAGE_REFERENCE.md")

    def show_about(self):
        messagebox.showinfo(
            "About NEXUS Studio",
            "NEXUS Studio v1.5 (Official IDE)\n\n"
            "Lightweight, full-featured IDE for the NEXUS Self-Hosting Native Compiler.\n"
            "0% libc - 0% .NET - 100% Native Machine Code.\n\n"
            "Keybindings:\n"
            "  F5             : Save & Run Program\n"
            "  F6             : Compile Tri-Platform (PE/ELF/Mach-O)\n"
            "  Ctrl+S         : Save File\n"
            "  Ctrl+O         : Open File\n"
            "  Ctrl+N         : New File\n"
            "  Ctrl+F         : Find & Replace\n"
            "  Ctrl+G         : Go to Line\n"
            "  Ctrl+/         : Toggle Comment\n"
            "  Ctrl++ / - / 0 : Zoom In / Out / Reset\n"
            "  Ctrl+Q         : Quit"
        )

    def quit_app(self):
        self.stop_execution()
        self.destroy()


def main():
    fname = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
    app = NexusStudio(filename=fname)
    app.mainloop()

if __name__ == "__main__":
    main()
