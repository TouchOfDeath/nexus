# NEXUS Language Support for VS Code

Syntax highlighting, bracket matching and auto-indentation for **.nex** files
- the NEXUS native self-hosting systems language.

## Features

- Full keyword highlighting: `let`, `if`/`else if`/`else`, `while`, `fn`,
  `call`, `return`, `struct`, `include`/`import`
- Built-in operations: `print`, `print_str`, `read`, `alloc`, `load`,
  `store`, `len`, `abs`, `file_open`, `file_create`, `file_read`,
  `file_write`, `file_close`
- Standard library functions (`nx_*`) highlighted in their own color
- Struct member access (`pt1.x`, `Point.size`) with special size-constant tint
- Strings, comments (`#` and `;`), integer literals, operators
- Auto-indentation on `{`, auto-close for braces and strings

## Install (from this folder)

Option A - copy into your extensions directory:

    cp -r editors/vscode-nexus ~/.vscode/extensions/vscode-nexus

Option B - package as VSIX (requires Node.js):

    cd editors/vscode-nexus
    npx @vscode/vsce package
    code --install-extension vscode-nexus-1.0.0.vsix

Then reload VS Code and open any `.nex` file.
