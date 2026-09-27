#!/usr/bin/env python3
"""
NEXUS Studio (nexstudio) - Lightweight Built-in IDE for the NEXUS Programming Language
Provides a visual editor with syntax highlighting, line numbering, example browser,
and instant F5 execution (compile & direct kernel execution).
"""

import os
import sys
import re
import queue
import threading
import subprocess
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

# --- Syntax Highlighting Configuration ---
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
    "warn": "#f9e2af"
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
        self.configure(width=48, bg=THEME["gutter_bg"], highlightthickness=0)

    def redraw(self):
        self.delete("all")
        i = self.text_widget.index("@0,0")
        while True:
            dline = self.text_widget.dlineinfo(i)
            if dline is None:
                break
            y = dline[1]
            linenum = str(i).split(".")[0]
            self.create_text(
                40, y + 2,
                anchor="ne",
                text=linenum,
                fill=THEME["gutter_fg"],
                font=("Monospace", 10)
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

        self.title("NEXUS Studio - Native Machine Code IDE")
        self.geometry("1024x720")
        self.minsize(640, 480)
        self.configure(bg=THEME["bg"])

        self._setup_styles()
        self._build_menu()
        self._build_toolbar()
        self._build_main_split()
        self._build_statusbar()
        self._bind_events()
        self._check_log_queue()

        if filename and os.path.exists(filename):
            self.open_file(filename)
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

        # Run Menu
        run_menu = tk.Menu(menubar, tearoff=0, bg=THEME["toolbar_bg"], fg=THEME["fg"], activebackground=THEME["select_bg"])
        run_menu.add_command(label="Run Program", accelerator="F5", command=self.run_program)
        run_menu.add_command(label="Compile Tri-Platform (PE/ELF/Mach-O)", accelerator="F6", command=self.compile_tri)
        run_menu.add_command(label="Compile Apple Silicon ARM64 Mach-O", command=self.compile_arm64)
        run_menu.add_command(label="Run Full Regression Test Suite", command=self.run_full_test)
        run_menu.add_separator()
        run_menu.add_command(label="Stop Execution", accelerator="Ctrl+C", command=self.stop_execution)
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
            b.pack(side="left", padx=4, pady=4)
            return b

        make_btn("▶  Run (F5)", self.run_program, color="#a6e3a1")
        make_btn("⚙  Compile Tri (F6)", self.compile_tri, color="#89b4fa")
        make_btn("■  Stop", self.stop_execution, color="#f38ba8")
        make_btn("📂 Open", self.open_file_dialog)
        make_btn("💾 Save", self.save_file)
        make_btn("🧹 Clear Console", self.clear_console)

        # Target badge on the right
        badge = tk.Label(tb, text="TARGET: BARE-METAL KERNEL (0% LIBC)",
                         bg="#1e1e2e", fg="#cba6f7", font=("Monospace", 8, "bold"), padx=10)
        badge.pack(side="right", padx=10)

    def _build_main_split(self):
        paned = tk.PanedWindow(self, orient=tk.VERTICAL, bg=THEME["bg"], sashwidth=4, sashrelief="flat")
        paned.pack(fill="both", expand=True)

        # Top Pane: Code Editor
        editor_frame = tk.Frame(paned, bg=THEME["editor_bg"])
        paned.add(editor_frame, minsize=200, height=450)

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
            font=("Monospace", 11),
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
        self.text.tag_configure("kw_control", foreground=THEME["kw_control"], font=("Monospace", 11, "bold"))
        self.text.tag_configure("kw_builtin", foreground=THEME["kw_builtin"])
        self.text.tag_configure("string", foreground=THEME["string"])
        self.text.tag_configure("number", foreground=THEME["number"])
        self.text.tag_configure("comment", foreground=THEME["comment"], font=("Monospace", 11, "italic"))
        self.text.tag_configure("fn_name", foreground=THEME["fn_name"], font=("Monospace", 11, "bold"))

        # Bottom Pane: Console / Output
        console_frame = tk.Frame(paned, bg=THEME["console_bg"])
        paned.add(console_frame, minsize=100, height=200)

        console_title = tk.Frame(console_frame, bg=THEME["toolbar_bg"], height=24)
        console_title.pack(side="top", fill="x")
        tk.Label(console_title, text="TERMINAL / CONSOLE OUTPUT",
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

    def _build_statusbar(self):
        self.statusbar = tk.Frame(self, bg=THEME["status_bg"], height=22)
        self.statusbar.pack(side="bottom", fill="x")

        self.status_file = tk.Label(self.statusbar, text="Untitled", bg=THEME["status_bg"],
                                    fg=THEME["status_fg"], font=("Monospace", 8))
        self.status_file.pack(side="left", padx=8)

        self.status_pos = tk.Label(self.statusbar, text="Ln 1, Col 0", bg=THEME["status_bg"],
                                   fg=THEME["status_fg"], font=("Monospace", 8))
        self.status_pos.pack(side="right", padx=8)

        self.status_info = tk.Label(self.statusbar, text="NEXUS v6.1 (100% Native Ahead-Of-Time)",
                                    bg=THEME["status_bg"], fg=THEME["kw_control"], font=("Monospace", 8, "bold"))
        self.status_info.pack(side="right", padx=16)

    def _bind_events(self):
        self.text.bind("<KeyRelease>", self._on_key_release)
        self.text.bind("<Return>", self._on_return)
        self.text.bind("<Tab>", self._on_tab)
        self.text.bind("<BackSpace>", self._on_backspace)
        self.text.bind("<ButtonRelease-1>", self._update_cursor_pos)

        self.bind("<F5>", lambda e: self.run_program())
        self.bind("<F6>", lambda e: self.compile_tri())
        self.bind("<Control-n>", lambda e: self.new_file(STARTER_CODE))
        self.bind("<Control-o>", lambda e: self.open_file_dialog())
        self.bind("<Control-s>", lambda e: self.save_file())
        self.bind("<Control-S>", lambda e: self.save_file_as())
        self.bind("<Control-q>", lambda e: self.quit_app())

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

    def _on_key_release(self, event=None):
        self._update_cursor_pos()
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
            # check if inside comment or string
            tags = self.text.tag_names(f"1.0+{start}c")
            if "comment" in tags or "string" in tags:
                continue
            if word in KEYWORDS_CONTROL:
                self.text.tag_add("kw_control", f"1.0+{start}c", f"1.0+{end}c")
            elif word in KEYWORDS_BUILTIN:
                self.text.tag_add("kw_builtin", f"1.0+{start}c", f"1.0+{end}c")

    def new_file(self, content=""):
        self.text.delete("1.0", tk.END)
        self.text.insert("1.0", content)
        self.current_file = None
        self.is_dirty = False
        self.status_file.config(text="Untitled")
        self.highlight_syntax()
        self.line_numbers.redraw()

    def open_file(self, filepath):
        if not os.path.exists(filepath):
            messagebox.showerror("File Error", f"File not found: {filepath}")
            return
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            code = f.read()
        self.text.delete("1.0", tk.END)
        self.text.insert("1.0", code)
        self.current_file = os.path.abspath(filepath)
        self.is_dirty = False
        self.status_file.config(text=os.path.basename(self.current_file))
        self.highlight_syntax()
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
                    text, tag = args
                    self.console.insert(tk.END, text, tag)
                    self.console.see(tk.END)
                elif action == "clear":
                    self.console.delete("1.0", tk.END)
        except Exception:
            pass
        self.after(50, self._check_log_queue)

    def log(self, text, tag=None):
        self.log_queue.put(("log", (text, tag)))

    def clear_console(self):
        self.log_queue.put(("clear", ()))

    def run_program(self):
        """Save file and run via ./nexus run <file> in a background thread."""
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
                for line in proc.stdout:
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
                self.log(out)
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
            "NEXUS Studio v1.0\n\n"
            "Built-in IDE for the NEXUS Self-Hosting Native Compiler.\n"
            "0% libc - 0% .NET - 100% Native Machine Code.\n\n"
            "Keybindings:\n"
            "  F5         : Save & Run Program\n"
            "  F6         : Compile Tri-Platform\n"
            "  Ctrl+S     : Save File\n"
            "  Ctrl+O     : Open File\n"
            "  Ctrl+N     : New File\n"
            "  Ctrl+Q     : Quit"
        )

    def quit_app(self):
        self.stop_execution()
        self.destroy()


def main():
    fname = sys.argv[1] if len(sys.argv) > 1 else None
    app = NexusStudio(filename=fname)
    app.mainloop()

if __name__ == "__main__":
    main()
