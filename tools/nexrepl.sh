#!/usr/bin/env bash
# ==============================================================================
#                        NEXREPL - NEXUS Interactive REPL (v1.0.0)
# ==============================================================================
# A stateful, line-by-line and multi-line NEXUS playground built on the native
# self-hosted toolchain.
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
export NEXUS_HOME="$PROJECT_DIR"
COMPILER_DIR="$PROJECT_DIR/compiler"
SCRATCH_DIR="$PROJECT_DIR/scratch"
SESSION="$SCRATCH_DIR/repl_session.nex"
SESSION_BAK="$SCRATCH_DIR/repl_session.bak"
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

if [ ! -x "$NEXPREP" ]; then gcc -O2 "$PROJECT_DIR/tools/nexprep.c" -o "$NEXPREP"; fi
if [ ! -x "$NEXELF" ] && [ -f "$PROJECT_DIR/tools/nexelf.c" ]; then
    gcc -O2 "$PROJECT_DIR/tools/nexelf.c" -o "$NEXELF"
fi

banner() {
    echo "=================================================="
    echo " NEXREPL v1.0.0 - NEXUS Interactive Session"
    echo " Compiler: self-hosted nexc (0% C# / 0% .NET)"
    echo " stdlib: ON   |  :help for commands  |  :quit exits"
    echo "=================================================="
}

session_compile() {
    local full="$SCRATCH_DIR/repl_full.nex"
    if [ "$STDLIB" = 1 ]; then
        echo "include \"stdlib/nstdlib.nex\"" > "$full"
    else
        : > "$full"
    fi
    cat "$SESSION" >> "$full"

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

is_bare_expr() {
    local s="$1"
    case "$s" in
        *$'\n'*) return 1 ;;
        let\ *|local\ *|var\ *|const\ *|fn\ *|call\ *|if\ *|if_f\ *|while\ *|while_f\ *|for\ *|match\ *|struct\ *|enum\ *|impl\ *|print\ *|print\(*|print_str\ *|print_float\ *|print_raw\ *|print_raw\(*|read\ *|store*|file_*|include\ *|import\ *|assert_*|exit*|return*)
            return 1 ;;
    esac
    if printf '%s' "$s" | grep -Eq '[^=!<>]=[^=]|\+\+|--'; then
        return 1
    fi
    return 0
}

handle_command() {
    case "$BUFFER" in
        ":help")
            sed -n '3,16p' "$SCRIPT_DIR/nexrepl.sh" | sed 's/^# \{0,1\}//'
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

    LINE="$(echo "$LINE" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
    [ -z "$LINE" ] && continue

    if [ "$DEPTH" -eq 0 ]; then
        case "$LINE" in
            :*) BUFFER="$LINE"; handle_command; BUFFER=""; continue ;;
        esac
    fi

    BUFFER="$BUFFER$LINE"
    local_opens=$(printf '%s' "$LINE" | tr -cd '{' | wc -c)
    local_closes=$(printf '%s' "$LINE" | tr -cd '}' | wc -c)
    DEPTH=$(( DEPTH + local_opens - local_closes ))
    if [ "$DEPTH" -lt 0 ]; then DEPTH=0; fi
    if [ "$DEPTH" -gt 0 ]; then
        BUFFER="$BUFFER"$'\n'
        continue
    fi

    cp "$SESSION" "$SESSION_BAK"
    if is_bare_expr "$BUFFER"; then
        echo "print($BUFFER)" >> "$SESSION"
        if session_compile; then
            show_delta
            BUFFER=""
            continue
        fi
        cp "$SESSION_BAK" "$SESSION"
    fi

    echo "$BUFFER" >> "$SESSION"
    BUFFER=""
    if session_compile; then
        show_delta
    else
        echo "[-] rejected (compilation failed)"
        cp "$SESSION_BAK" "$SESSION"
        session_compile > /dev/null 2>&1
    fi
done
