# Vim Integration Guide for ios_system

This document describes how to integrate vim into applications using the ios_system framework.

## Overview

Vim is integrated as a dynamic framework (`vim.framework`) that can be loaded and executed through ios_system's command dispatch system. The integration includes thread-local storage for concurrent execution support and iOS-specific adaptations.

## Building vim.framework

### Prerequisites

1. Xcode 15.0 or later
2. ios_system.xcodeproj with the vim target configured
3. VimRuntime.bundle prepared

### Preparing the Runtime Bundle

Run the prepare script to create VimRuntime.bundle:

```bash
cd vim_framework
./prepare_runtime.sh
```

This creates a ~20MB bundle containing:
- `syntax/` - Syntax highlighting definitions
- `colors/` - Color schemes
- `indent/` - Indentation rules
- `ftplugin/` - Filetype plugins
- Essential vim runtime files

### Creating the Xcode Target

1. Open `ios_system.xcodeproj` in Xcode
2. Create a new target: File → New → Target → Framework
3. Name: `vim`
4. Bundle Identifier: `com.holzschu.vim`

Configure Build Settings:
- Supported Platforms: iOS, iOS Simulator, Mac Catalyst, visionOS
- iOS Deployment Target: 14.0
- Header Search Paths: `$(SRCROOT)/vim/src`
- Other C Flags: `-DHAVE_CONFIG_H=1 -DFEAT_NORMAL=1 -DIOS_SYSTEM=1`
- Framework Search Paths: `$(BUILT_PRODUCTS_DIR)`

Add Source Files (from `vim/src/`):
```
alloc.c, arabic.c, arglist.c, autocmd.c, beval.c, blob.c, blowfish.c,
buffer.c, change.c, charset.c, cindent.c, clipboard.c, cmdhist.c,
crypt.c, crypt_zip.c, debugger.c, dict.c, diff.c, digraph.c,
drawline.c, drawscreen.c, edit.c, eval.c, evalbuffer.c, evalfunc.c,
evalvars.c, evalwindow.c, ex_cmds.c, ex_cmds2.c, ex_docmd.c,
ex_eval.c, ex_getln.c, fileio.c, filepath.c, findfile.c, float.c,
fold.c, getchar.c, hardcopy.c, hashtab.c, help.c, highlight.c,
indent.c, insexpand.c, ios_vim.c, json.c, list.c, main.c, map.c,
mark.c, match.c, mbyte.c, memfile.c, memline.c, menu.c, message.c,
misc1.c, misc2.c, mouse.c, move.c, normal.c, object.c, ops.c,
option.c, optionstr.c, os_mac_conv.c, os_unix.c, popupmenu.c,
profiler.c, quickfix.c, regexp.c, register.c, screen.c, scriptfile.c,
search.c, session.c, sha256.c, sign.c, spell.c, spellfile.c,
spellsuggest.c, strings.c, syntax.c, tag.c, term.c, testing.c,
textformat.c, textobject.c, textprop.c, time.c, typval.c, undo.c,
usercmd.c, userfunc.c, version.c, vim9*.c, viminfo.c, window.c
```

**Exclude** these files:
- `gui*.c` - GUI code
- `os_win*.c`, `os_mswin.c`, `os_vms*.c` - Other platforms
- `if_*.c` - Language bindings (Python, Ruby, etc.)
- `terminal.c`, `channel.c`, `job.c` - Job/terminal features
- `netbeans.c` - Netbeans integration

Link Binary With Libraries:
- `ios_system.framework`
- `libncurses.tbd`

Add Resources:
- Add `VimRuntime.bundle` to "Copy Bundle Resources" build phase

### Building the XCFramework

```bash
swift run --package-path xcfs build vim
```

Output: `.build/vim/vim.xcframework.zip`

## Integration in Host Applications

### Required Setup

1. Link `vim.framework` and `ios_system.framework`
2. Add `VimRuntime.bundle` to your app bundle
3. Initialize ios_system in your app

### Environment Variables

Set these before invoking vim:

```objc
// Required: writable location for .vimrc and swap files
setenv("HOME", documentsPath.UTF8String, 1);

// Optional: override runtime location (auto-detected from framework)
// setenv("VIMRUNTIME", runtimePath.UTF8String, 1);

// Recommended: terminal type for colors
setenv("TERM", "xterm-256color", 1);
```

### Basic Usage

```objc
#import <ios_system/ios_system.h>

// Execute vim
ios_system("vim myfile.txt");

// With options
ios_system("vim -R readonly.txt");    // Read-only mode
ios_system("vim +10 file.txt");       // Open at line 10
ios_system("vim -c 'set number' f.c"); // Execute command on start
```

### Vimscript Detection

Vimscripts can detect iOS:

```vim
if has('ios')
    " iOS-specific configuration
    set nobackup
    set noswapfile
endif

if has('ios_system')
    " Running under ios_system framework
endif
```

## Disabled Features

The following features are not available on iOS due to sandbox restrictions:

| Feature | Reason | Alternative |
|---------|--------|-------------|
| `:!command` | No fork/exec | Use ios_system commands |
| `:shell` | No interactive shell | N/A |
| `:make` | No external compiler | N/A |
| `:grep` (external) | No external grep | Use `:vimgrep` |
| `:terminal` | Requires PTY | N/A |
| Python/Ruby/Perl/Lua | No interpreters | Pure vimscript |
| Spell checking | No external aspell | Built-in if enabled |

## Working Features

Core vim functionality works correctly:

- All editing modes (normal, insert, visual, etc.)
- Syntax highlighting for 600+ languages
- Search and replace (`:s///`, `/pattern`)
- Multiple buffers, windows, and tabs
- Registers and macros
- Undo/redo with persistent undo
- Folding
- File operations (read, write, save as)
- Color schemes
- Custom key mappings
- Vimscript (pure, no external languages)
- Configuration via `.vimrc`

## Configuration Recommendations

Create `$HOME/.vimrc`:

```vim
" iOS-optimized vimrc
if has('ios')
    " Disable features that don't work on iOS
    set nobackup
    set nowritebackup
    set noswapfile

    " Use built-in commands instead of external
    set grepprg=internal

    " Enable syntax highlighting
    syntax on
    filetype plugin indent on

    " Good defaults for terminal editing
    set number
    set showcmd
    set wildmenu
    set incsearch
    set hlsearch
endif
```

## Troubleshooting

### "Shell commands not available on iOS"

This error appears when using `:!`, `:shell`, or other features requiring fork/exec. These are intentionally disabled. Use ios_system-compatible commands or built-in vim alternatives.

### Syntax highlighting not working

1. Ensure `VimRuntime.bundle` is in your app bundle
2. Check `$VIMRUNTIME` is set correctly
3. Run `:set runtimepath?` to verify paths

### Colors not displaying

Set `TERM=xterm-256color` before launching vim.

### .vimrc not loading

Ensure `$HOME` points to a writable directory where `.vimrc` exists.

## Version Information

- Vim version: 9.1
- ios_system integration: 1.0
- Minimum iOS: 14.0
