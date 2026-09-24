#!/usr/bin/env bash
# ==============================================================================
#                        NEXREPL - NEXUS Interactive REPL
# ==============================================================================
# A stateful, line-by-line NEXUS playground built on the native toolchain.
#
# How it works:
#   Every accepted line is appended to a running session program. The whole
#   session is recompiled with the self-hosted compiler and executed; only
#   the NEW output (the delta against the previous run) is displayed. If a
#   line breaks compilation it is rejected and the session stays intact.
#
# Session commands (start with ':'):
#   :help            show this help
#   :clear           start a fresh session
#   :save [file.nex] write the session program to a file
#   :history         show the current session program
#   :stdlib on|off   auto-include the standard library (default: on)
#   :quit            leave the REPL (Ctrl-D also works)
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER_DIR="$PROJECT_DIR/compiler"
SCRATCH_DIR="$PROJECT_DIR/scratch"
SESSION="$SCRATCH_DIR/repl_session.nex"
REPL_OUT="$SCRATCH_DIR/repl_last_output.txt"

NEXPREP="$PROJECT_DIR/bin/nexprep"
NEXELF="$PROJECT_DIR/bin/nexelf"

mkdir -p "$SCRATCH_DIR"
: > "$SESSION"
: > "$REPL_OUT"
STDLIB=1
DEPTH=0
BUFFER=""
PREV_LINES=0

# make sure the toolchain binaries exist
if [ ! -x "$NEXPREP" ]; then gcc -O2 "$PROJECT_DIR/tools/nexprep.c" -o "$NEXPREP"; fi
if [ ! -x "$NEXELF" ] && [ -f "$PROJECT_DIR/tools/nexelf.c" ]; then
    gcc -O2 "$PROJECT_DIR/tools/nexelf.c" -o "$NEXELF"
fi

banner() {
    echo "=================================================="
    echo " NEXREPL - NEXUS Interactive Session"
    echo " Compiler: self-hosted nexc (0% C# / 0% .NET)"
    echo " stdlib: ON   |  :help for commands  |  :quit exits"
    echo "=================================================="
}

session_compile() {
    # 1. assemble full program
    local full="$SCRATCH_DIR/repl_full.nex"
    if [ "$STDLIB" = 1 ]; then
        echo "include \"stdlib/nstdlib.nex\"" > "$full"
    else
        : > "$full"
    fi
    cat "$SESSION" >> "$full"

    # 2. preprocess + compile + run, all failures silent (caller reports)
    rm -f "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf"
    "$NEXPREP" "$full" "$COMPILER_DIR/code.nex" 2>/dev/null || return 1
    ( cd "$COMPILER_DIR" && ./nexc.elf > /dev/null 2>&1 ) || return 1
    [ -f "$COMPILER_DIR/app.exe" ] || return 1
    if [ ! -f "$COMPILER_DIR/app.elf" ]; then
        [ -x "$NEXELF" ] || return 1
        "$NEXELF" "$COMPILER_DIR/app.exe" "$COMPILER_DIR/app.elf" > /dev/null 2>&1 || return 1
    fi
    "$COMPILER_DIR/app.elf" > "$REPL_OUT" 2>/dev/null
    return 0
}

show_delta() {
    local total now
    total=$(wc -l < "$REPL_OUT")
    now=$total
    if [ "$now" -gt "$PREV_LINES" ]; then
        sed -n "$((PREV_LINES + 1)),${now}p" "$REPL_OUT"
    fi
    PREV_LINES=$now
}

reject_line() {
    # drop the last accepted line from the session
    local n
    n=$(wc -l < "$SESSION")
    if [ "$n" -gt 0 ]; then sed -i "${n}d" "$SESSION"; fi
}

handle_command() {
    case "$BUFFER" in
        ":help")
            sed -n '3,30p' "$SCRIPT_DIR/nexrepl.sh" | sed 's/^# \{0,1\}//'
            ;;
        ":clear"|":reset")
            : > "$SESSION"; : > "$REPL_OUT"; PREV_LINES=0; DEPTH=0; BUFFER=""
            echo "[*] session cleared"
            ;;
        ":quit"|":q"|":exit")
            echo "[*] goodbye."
            exit 0
            ;;
        ":history")
            cat "$SESSION"
            ;;
        ":save")
            cp "$SESSION" "$SCRATCH_DIR/repl_saved.nex"
            echo "[*] saved to scratch/repl_saved.nex"
            ;;
        ":save "*)
            local f
            f=$(echo "$BUFFER" | cut -d' ' -f2-)
            cp "$SESSION" "$f" && echo "[*] saved to $f" || echo "[-] save failed"
            ;;
        ":stdlib "*)
            local v
            v=$(echo "$BUFFER" | awk '{print tolower($2)}')
            if [ "$v" = "on" ]; then STDLIB=1; echo "[*] stdlib on"
            elif [ "$v" = "off" ]; then STDLIB=0; echo "[*] stdlib off"
            else echo "[-] usage: :stdlib on|off"; fi
            ;;
        ":stdlib")
            if [ "$STDLIB" = 1 ]; then echo "[*] stdlib is on"; else echo "[*] stdlib is off"; fi
            ;;
        *)
            echo "[-] unknown command: $BUFFER (try :help)"
            ;;
    esac
}

banner
while true; do
    if [ "$DEPTH" -gt 0 ]; then
        printf '..... '
    else
        printf 'nex> '
    fi
    if ! read -r LINE; then
        echo ""
        echo "[*] goodbye."
        exit 0
    fi

    # trim leading/trailing whitespace
    LINE="$(echo "$LINE" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
    [ -z "$LINE" ] && continue

    # session commands only at top level
    if [ "$DEPTH" -eq 0 ]; then
        case "$LINE" in
            :*) BUFFER="$LINE"; handle_command; continue ;;
        esac
    fi

    # block accumulation
    BUFFER="$BUFFER$LINE"
    local_opens=$(printf '%s' "$LINE" | tr -cd '{' | wc -c)
    local_closes=$(printf '%s' "$LINE" | tr -cd '}' | wc -c)
    DEPTH=$(( DEPTH + local_opens - local_closes ))
    if [ "$DEPTH" -lt 0 ]; then DEPTH=0; fi
    if [ "$DEPTH" -gt 0 ]; then
        BUFFER="$BUFFER"$'\n'
        continue
    fi

    # compile + run the updated session
    echo "$BUFFER" >> "$SESSION"
    BUFFER=""
    if session_compile; then
        show_delta
    else
        echo "[-] rejected (compilation failed)"
        reject_line
        session_compile > /dev/null 2>&1   # restore prior output baseline
    fi
done
