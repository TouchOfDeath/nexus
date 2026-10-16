#!/usr/bin/env python3
"""
NEXUS Reference Semantic Interpreter (nexinterp.py)
A pure, high-precision semantic oracle for the NEXUS programming language.

Usage:
    ./nexus interpret <file.nex>
    python3 tools/nexinterp.py <file.nex>
"""

import sys, os, struct, re

def to_int64(val):
    val = int(val) & 0xFFFFFFFFFFFFFFFF
    if val >= 0x8000000000000000:
        val -= 0x10000000000000000
    return val

class NexusInterpreter:
    def __init__(self):
        # 64 MB simulated physical memory
        self.memory = bytearray(64 * 1024 * 1024)
        self.heap_ptr = 4096
        self.globals = {}
        self.call_stack = []
        self.functions = {}
        self.stdout = []

    def alloc(self, size):
        # 16-byte aligned allocation
        ptr = (self.heap_ptr + 15) & ~15
        self.heap_ptr = ptr + size
        if self.heap_ptr > len(self.memory):
            raise MemoryError("NEXUS Interpreter: out of simulated memory")
        return ptr

    def load8(self, addr):
        return self.memory[addr]

    def store8(self, addr, val):
        self.memory[addr] = val & 0xFF

    def load16(self, addr):
        return struct.unpack_from("<h", self.memory, addr)[0]

    def store16(self, addr, val):
        struct.pack_into("<h", self.memory, addr, val & 0xFFFF)

    def load32(self, addr):
        return struct.unpack_from("<i", self.memory, addr)[0]

    def store32(self, addr, val):
        struct.pack_into("<i", self.memory, addr, val & 0xFFFFFFFF)

    def load64(self, addr):
        return struct.unpack_from("<q", self.memory, addr)[0]

    def store64(self, addr, val):
        # Handle 64-bit integer packing
        val = int(val) & 0xFFFFFFFFFFFFFFFF
        if val >= 0x8000000000000000:
            val -= 0x10000000000000000
        struct.pack_into("<q", self.memory, addr, val)

    def store_str_literal(self, s):
        # Unescape common escape sequences including \e
        s = s.replace(r'\e', '\x1b')
        s = s.encode('utf-8').decode('unicode_escape')
        b = s.encode('latin1', errors='replace') + b'\x00'
        ptr = self.alloc(len(b))
        self.memory[ptr:ptr+len(b)] = b
        return ptr

    def read_str_from_mem(self, ptr):
        end = ptr
        while end < len(self.memory) and self.memory[end] != 0:
            end += 1
        return self.memory[ptr:end].decode('latin1', errors='replace')

    def get_var(self, name):
        if self.call_stack and name in self.call_stack[-1]:
            return self.call_stack[-1][name]
        if name in self.globals:
            return self.globals[name]
        return 0

    def set_var(self, name, val):
        if isinstance(val, int):
            val = to_int64(val)
        if self.call_stack and name in self.call_stack[-1]:
            self.call_stack[-1][name] = val
        else:
            self.globals[name] = val

    def eval_atom(self, tok):
        tok = tok.strip()
        if not tok: return 0
        if tok.startswith('"') and tok.endswith('"'):
            return self.store_str_literal(tok[1:-1])
        if tok.startswith("'") and tok.endswith("'") and len(tok) == 3:
            return ord(tok[1])
        try:
            if '.' in tok:
                return float(tok)
            try:
                return int(tok, 0)
            except ValueError:
                return int(tok)
        except ValueError:
            return self.get_var(tok)

    def eval_expr(self, expr):
        expr = expr.strip()
        if not expr: return 0

        # Null coalescing operator: expr ?? fallback
        if '??' in expr:
            in_str = False
            paren_depth = 0
            bracket_depth = 0
            nc_idx = -1
            for idx in range(len(expr) - 1):
                ch = expr[idx]
                if ch == '"': in_str = not in_str
                if in_str: continue
                if ch == '(': paren_depth += 1
                elif ch == ')': paren_depth = max(0, paren_depth - 1)
                elif ch == '[': bracket_depth += 1
                elif ch == ']': bracket_depth = max(0, bracket_depth - 1)
                elif paren_depth == 0 and bracket_depth == 0 and ch == '?' and expr[idx+1] == '?':
                    nc_idx = idx
                    break
            if nc_idx != -1:
                lhs_str = expr[:nc_idx].strip()
                rhs_str = expr[nc_idx+2:].strip()
                lhs_val = self.eval_expr(lhs_str)
                if lhs_val != 0:
                    return lhs_val
                return self.eval_expr(rhs_str)

        # Ternary conditional expression: cond ? true_val : false_val
        if '?' in expr:
            in_str = False
            paren_depth = 0
            q_idx = -1
            colon_idx = -1
            for idx, ch in enumerate(expr):
                if ch == '"': in_str = not in_str
                if in_str: continue
                if ch == '(': paren_depth += 1
                elif ch == ')': paren_depth -= 1
                elif ch == '?' and paren_depth == 0 and q_idx == -1:
                    if idx + 1 < len(expr) and expr[idx+1] == '?':
                        continue
                    q_idx = idx
                elif ch == ':' and paren_depth == 0 and q_idx != -1:
                    colon_idx = idx
                    break
            if q_idx != -1 and colon_idx != -1:
                cond_str = expr[:q_idx].strip()
                true_str = expr[q_idx+1:colon_idx].strip()
                false_str = expr[colon_idx+1:].strip()
                if self.eval_cond(cond_str):
                    return self.eval_expr(true_str)
                else:
                    return self.eval_expr(false_str)

        # String literal
        if expr.startswith('"') and expr.endswith('"'):
            return self.store_str_literal(expr[1:-1])

        # Built-in abs
        if expr.startswith("abs "):
            return abs(self.eval_expr(expr[4:]))

        # Built-in len
        if expr.startswith("len "):
            arg = expr[4:].strip()
            if arg.startswith('"') and arg.endswith('"'):
                return len(arg[1:-1].encode('utf-8').decode('unicode_escape'))
            ptr = self.eval_atom(arg)
            s = self.read_str_from_mem(ptr)
            return len(s)

        # Built-in alloc
        if expr.startswith("alloc "):
            size = self.eval_expr(expr[6:])
            return self.alloc(int(size))

        # Memory loads: load [ptr + offset]
        m = re.match(r'^(load|load8|load16|load32|load64)\s*\[(.*)\]$', expr)
        if m:
            op, mem_expr = m.groups()
            addr = self.eval_addr(mem_expr)
            if op in ("load", "load8"): return self.load8(addr)
            elif op == "load16": return self.load16(addr)
            elif op == "load32": return self.load32(addr)
            elif op == "load64": return self.load64(addr)

        # Exponentiation
        if " ** " in expr:
            parts = expr.split(" ** ")
            base = self.eval_expr(parts[0])
            exp = self.eval_expr(parts[1])
            return int(base) ** int(exp)

        # Left-to-right integer/float chained evaluation
        tokens = re.findall(r'(\+|-|\*|/|%|<=|>=|<|>|==|!=|\S+)', expr)
        if not tokens: return 0
        res = self.eval_atom(tokens[0])
        i = 1
        while i < len(tokens):
            op = tokens[i]
            if i + 1 >= len(tokens): break
            rhs = self.eval_atom(tokens[i+1])
            i += 2

            is_float = isinstance(res, float) or isinstance(rhs, float)
            if op == '+':
                res = res + rhs if is_float else to_int64(res + rhs)
            elif op == '-':
                res = res - rhs if is_float else to_int64(res - rhs)
            elif op == '*':
                res = res * rhs if is_float else to_int64(res * rhs)
            elif op == '/':
                if is_float:
                    res = res / rhs if rhs != 0.0 else 0.0
                else:
                    # C99 truncating integer division
                    a = to_int64(res); b = to_int64(rhs)
                    if b == 0: res = 0
                    else:
                        sign = -1 if (a < 0) ^ (b < 0) else 1
                        res = to_int64(sign * (abs(a) // abs(b)))
            elif op == '%':
                a = to_int64(res); b = to_int64(rhs)
                res = to_int64((abs(a) % abs(b)) * (-1 if a < 0 else 1) if b != 0 else 0)
            elif op == '<':
                res = 1 if res < rhs else 0
            elif op == '<=':
                res = 1 if res <= rhs else 0
            elif op == '>':
                res = 1 if res > rhs else 0
            elif op == '>=':
                res = 1 if res >= rhs else 0
            elif op == '==':
                res = 1 if res == rhs else 0
            elif op == '!=':
                res = 1 if res != rhs else 0

        return res

    def eval_addr(self, mem_expr):
        # Evaluate ptr + offset or ptr - offset (supports scaled index: ptr + idx * 8)
        mem_expr = mem_expr.strip()
        parts = re.split(r'(\+|\-)', mem_expr)
        base = self.eval_atom(parts[0].strip())
        addr = int(base)
        i = 1
        while i < len(parts):
            sign = parts[i].strip()
            rhs_str = parts[i+1].strip()
            i += 2
            if '*' in rhs_str:
                term, scale = rhs_str.split('*', 1)
                val = int(self.eval_atom(term.strip())) * int(self.eval_atom(scale.strip()))
            else:
                val = int(self.eval_atom(rhs_str))
            if sign == '+': addr += val
            elif sign == '-': addr -= val
        return addr

    def eval_cond(self, cond):
        cond = cond.strip()
        # Handle condition: expr op expr or truthiness
        tokens = re.findall(r'(<=|>=|==|!=|<|>|\S+)', cond)
        for rel in ('<=', '>=', '==', '!=', '<', '>'):
            if rel in tokens:
                idx = tokens.index(rel)
                left_expr = " ".join(tokens[:idx])
                right_expr = " ".join(tokens[idx+1:])
                left = self.eval_expr(left_expr)
                right = self.eval_expr(right_expr)
                if rel == '==': return left == right
                if rel == '!=': return left != right
                if rel == '<': return left < right
                if rel == '<=': return left <= right
                if rel == '>': return left > right
                if rel == '>=': return left >= right
        val = self.eval_expr(cond)
        return val != 0

    def parse_blocks(self, lines):
        """Parse indented/nested braced lines into a list of AST statement tuples."""
        stmts = []
        i = 0
        while i < len(lines):
            line = lines[i].strip()
            i += 1
            if not line or line.startswith(';') or line.startswith('#'):
                continue

            if line.startswith('fn ') and line.endswith('{'):
                fn_name = line[3:-1].strip()
                body = []
                depth = 1
                while i < len(lines) and depth > 0:
                    l = lines[i]
                    i += 1
                    for ch in l:
                        if ch == '{': depth += 1
                        elif ch == '}': depth -= 1
                    if depth > 0: body.append(l)
                self.functions[fn_name] = self.parse_blocks(body)
                continue

            if line.startswith('while ') and line.endswith('{'):
                cond = line[6:-1].strip()
                body = []
                depth = 1
                while i < len(lines) and depth > 0:
                    l = lines[i]
                    i += 1
                    for ch in l:
                        if ch == '{': depth += 1
                        elif ch == '}': depth -= 1
                    if depth > 0: body.append(l)
                stmts.append(('while', cond, self.parse_blocks(body)))
                continue

            if line.startswith('if ') and line.endswith('{'):
                cond = line[3:-1].strip()
                then_body = []
                else_body = []
                depth = 1
                in_else = False
                while i < len(lines) and depth > 0:
                    l = lines[i]
                    i += 1
                    trimmed = l.strip()
                    if trimmed == '} else {' and depth == 1:
                        in_else = True
                        continue
                    for ch in l:
                        if ch == '{': depth += 1
                        elif ch == '}': depth -= 1
                    if depth > 0:
                        if in_else:
                            else_body.append(l)
                        else:
                            then_body.append(l)
                stmts.append(('if', cond, self.parse_blocks(then_body), self.parse_blocks(else_body)))
                continue

            if line.startswith('for ') and line.endswith('{'):
                header = line[4:-1].strip()
                body = []
                depth = 1
                while i < len(lines) and depth > 0:
                    l = lines[i]
                    i += 1
                    for ch in l:
                        if ch == '{': depth += 1
                        elif ch == '}': depth -= 1
                    if depth > 0: body.append(l)
                stmts.append(('for', header, self.parse_blocks(body)))
                continue

            # Simple statement
            stmts.append(('stmt', line))
        return stmts

    def exec_block(self, stmts):
        for stmt in stmts:
            kind = stmt[0]
            if kind == 'while':
                cond, body = stmt[1], stmt[2]
                while self.eval_cond(cond):
                    ret = self.exec_block(body)
                    if ret == 'break': break
                    if ret == 'return': return 'return'
            elif kind == 'for':
                header, body = stmt[1], stmt[2]
                if ' in ' in header:
                    var_part, range_part = header.split(' in ', 1)
                    var = var_part.strip()
                    step_val = 1
                    if ' step ' in range_part:
                        range_part, step_part = range_part.split(' step ', 1)
                        step_val = self.eval_expr(step_part.strip())
                    range_part = range_part.strip()
                    inclusive = False
                    if '..=' in range_part:
                        inclusive = True
                        s_str, e_str = range_part.split('..=', 1)
                    elif '..' in range_part:
                        s_str, e_str = range_part.split('..', 1)
                    else:
                        raise SyntaxError(f"Invalid range in for loop: {header}")
                    start_val = self.eval_expr(s_str.strip())
                    end_val = self.eval_expr(e_str.strip())
                    cur = start_val
                    while True:
                        if step_val > 0:
                            if inclusive and cur > end_val: break
                            if not inclusive and cur >= end_val: break
                        else:
                            if inclusive and cur < end_val: break
                            if not inclusive and cur <= end_val: break
                        self.set_var(var, cur)
                        ret = self.exec_block(body)
                        if ret == 'break': break
                        if ret == 'return': return 'return'
                        cur += step_val
                    continue
                parts = header.split(',')
                if len(parts) == 3:
                    init_str, cond_str, upd_str = parts[0].strip(), parts[1].strip(), parts[2].strip()
                    self.exec_block([('stmt', init_str)])
                    while self.eval_cond(cond_str):
                        ret = self.exec_block(body)
                        if ret == 'break': break
                        if ret == 'return': return 'return'
                        self.exec_block([('stmt', upd_str)])
                elif len(parts) == 4:
                    var, start_v, end_v, upd_str = parts[0].strip(), parts[1].strip(), parts[2].strip(), parts[3].strip()
                    self.set_var(var, self.eval_expr(start_v))
                    cmp_op = '<' if ('++' in upd_str or '+=' in upd_str or '+' in upd_str) else '>'
                    cond_str = f"{var} {cmp_op} {end_v}"
                    while self.eval_cond(cond_str):
                        ret = self.exec_block(body)
                        if ret == 'break': break
                        if ret == 'return': return 'return'
                        self.exec_block([('stmt', upd_str)])
            elif kind == 'if':
                cond, then_body = stmt[1], stmt[2]
                else_body = stmt[3] if len(stmt) > 3 else []
                if self.eval_cond(cond):
                    ret = self.exec_block(then_body)
                    if ret in ('break', 'continue', 'return'): return ret
                elif else_body:
                    ret = self.exec_block(else_body)
                    if ret in ('break', 'continue', 'return'): return ret
            elif kind == 'stmt':
                line = stmt[1]
                if line == 'break': return 'break'
                if line == 'continue': return 'continue'
                if line.startswith('return'):
                    rest = line[6:].strip()
                    if rest:
                        self.set_var('_ret_val', self.eval_expr(rest))
                    return 'return'

                if line.startswith('const '):
                    rest = line[6:].strip()
                    if '=' in rest:
                        name, expr = rest.split('=', 1)
                        name = name.strip()
                        val = self.eval_expr(expr.strip())
                        self.globals[name] = val
                    continue

                # Increment / decrement: var++ or ++var or var-- or --var
                tline = line
                if tline.startswith('let ') or tline.startswith('local '):
                    tline = tline.split(' ', 1)[1].strip()
                if tline.endswith('++') or tline.endswith('--'):
                    op = tline[-2:]
                    var = tline[:-2].strip()
                    val = self.eval_expr(var)
                    new_val = (val + 1) if op == '++' else (val - 1)
                    self.set_var(var, new_val)
                    continue
                if tline.startswith('++') or tline.startswith('--'):
                    op = tline[:2]
                    var = tline[2:].strip()
                    val = self.eval_expr(var)
                    new_val = (val + 1) if op == '++' else (val - 1)
                    self.set_var(var, new_val)
                    continue

                # Print statement
                if line.startswith('print '):
                    expr = line[6:].strip()
                    if expr.startswith('"') and expr.endswith('"'):
                        val = expr[1:-1].encode('utf-8').decode('unicode_escape')
                    else:
                        v = self.eval_expr(expr)
                        if isinstance(v, float):
                            val = f"{v:.6f}"
                        else:
                            val = str(v)
                    print(val)
                    self.stdout.append(val)
                    continue

                if line.startswith('print_str '):
                    arg = line[10:].strip()
                    if arg.startswith('"') and arg.endswith('"'):
                        val = arg[1:-1].encode('utf-8').decode('unicode_escape')
                    else:
                        ptr = self.eval_atom(arg)
                        val = self.read_str_from_mem(int(ptr))
                    print(val)
                    self.stdout.append(val)
                    continue

                # Memory store: store [ptr + offset] val
                m = re.match(r'^(store|store8|store16|store32|store64)\s*\[(.*)\]\s+(.*)$', line)
                if m:
                    op, mem_expr, val_expr = m.groups()
                    addr = self.eval_addr(mem_expr)
                    val = self.eval_expr(val_expr)
                    if op in ('store', 'store8'): self.store8(addr, int(val))
                    elif op == 'store16': self.store16(addr, int(val))
                    elif op == 'store32': self.store32(addr, int(val))
                    elif op == 'store64': self.store64(addr, val)
                    continue

                # Call subroutine
                if line.startswith('call '):
                    fn_name = line[5:].strip()
                    if fn_name in self.functions:
                        self.exec_block(self.functions[fn_name])
                    continue

                # Let assignment: let var = expr
                if line.startswith('let ') or line.startswith('local '):
                    prefix = 'let ' if line.startswith('let ') else 'local '
                    rest = line[len(prefix):].strip()
                    if '=' in rest:
                        vname, vexpr = rest.split('=', 1)
                        vname = vname.strip()
                        val = self.eval_expr(vexpr.strip())
                        self.set_var(vname, val)
                    continue

                # Bare assignment: var = expr
                if '=' in line and not line.startswith('if ') and not line.startswith('while '):
                    vname, vexpr = line.split('=', 1)
                    vname = vname.strip()
                    if ' ' not in vname:
                        val = self.eval_expr(vexpr.strip())
                        self.set_var(vname, val)
                    continue

        return None

    def run_source(self, code_str):
        lines = code_str.splitlines()
        stmts = self.parse_blocks(lines)
        self.exec_block(stmts)
        return "\n".join(self.stdout)

def main():
    if len(sys.argv) < 2:
        print("Usage: nexinterp.py <source.nex>")
        sys.exit(1)

    src = sys.argv[1]
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    prep_bin = os.path.join(base_dir, "bin", "nexprep")

    # Preprocess file with --no-hydron to get canonical resolved AST
    import subprocess, tempfile
    with tempfile.NamedTemporaryFile(suffix=".nex", delete=False) as tmp:
        tmp_path = tmp.name

    try:
        subprocess.run([prep_bin, "--no-hydron", src, tmp_path], check=True, capture_output=True)
        with open(tmp_path, "r") as f:
            code = f.read()
    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

    interp = NexusInterpreter()
    interp.run_source(code)

if __name__ == "__main__":
    main()
