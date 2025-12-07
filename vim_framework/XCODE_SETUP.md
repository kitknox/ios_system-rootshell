# Xcode Setup Guide for vim.framework

This guide walks through creating the vim.framework target in Xcode.

## Step 1: Create New Framework Target

1. Open `ios_system.xcodeproj` in Xcode
2. File → New → Target
3. Select: iOS → Framework
4. Product Name: `vim`
5. Bundle Identifier: `com.holzschu.vim`
6. Language: Objective-C (to support mixed C/ObjC in ios_vim.c)

## Step 2: Configure Build Settings

Select the `vim` target and configure:

### General
- Deployment Target: iOS 14.0
- Supported Destinations: iPhone, iPad, Mac (Designed for iPad), Apple Vision

### Build Settings

**Architectures:**
- Build Active Architecture Only: No (for Release)
- Supported Platforms: iphoneos iphonesimulator maccatalyst xros xrsimulator

**Search Paths:**
- Header Search Paths: `$(SRCROOT)/vim/src` `$(SRCROOT)/ios_system`
- Framework Search Paths: `$(BUILT_PRODUCTS_DIR)`

**Apple Clang - Preprocessing:**
- Preprocessor Macros:
  - Debug: `DEBUG=1 HAVE_CONFIG_H=1 FEAT_NORMAL=1 IOS_SYSTEM=1`
  - Release: `HAVE_CONFIG_H=1 FEAT_NORMAL=1 IOS_SYSTEM=1`

**Apple Clang - Warnings:**
- Other Warning Flags: `-Wno-shorten-64-to-32 -Wno-implicit-function-declaration -Wno-incompatible-pointer-types`

**Linking:**
- Other Linker Flags: (none needed)

## Step 3: Add Source Files

Add these files from `vim/src/` to the target (121 files):

```
alloc.c, arabic.c, arglist.c, autocmd.c, beval.c, blob.c, blowfish.c,
buffer.c, bufwrite.c, change.c, charset.c, cindent.c, clientserver.c,
clipboard.c, cmdexpand.c, cmdhist.c, crypt.c, crypt_zip.c, debugger.c,
dict.c, diff.c, digraph.c, drawline.c, drawscreen.c, edit.c, eval.c,
evalbuffer.c, evalfunc.c, evalvars.c, evalwindow.c, ex_cmds.c, ex_cmds2.c,
ex_docmd.c, ex_eval.c, ex_getln.c, fileio.c, filepath.c, findfile.c,
float.c, fold.c, fuzzy.c, gc.c, getchar.c, hardcopy.c, hashtab.c, help.c,
highlight.c, indent.c, insexpand.c, ios_vim.c, json.c, linematch.c, list.c,
locale.c, logfile.c, main.c, map.c, mark.c, match.c, mbyte.c, memfile.c,
memline.c, menu.c, message.c, misc1.c, misc2.c, mouse.c, move.c, normal.c,
ops.c, option.c, optionstr.c, os_mac_conv.c, os_unix.c, popupmenu.c,
popupwin.c, profiler.c, quickfix.c, regexp.c, regexp_bt.c, regexp_nfa.c,
register.c, screen.c, scriptfile.c, search.c, session.c, sha256.c, sign.c,
sound.c, spell.c, spellfile.c, spellsuggest.c, strings.c, syntax.c,
tabpanel.c, tag.c, term.c, termlib.c, testing.c, textformat.c, textobject.c,
textprop.c, time.c, tuple.c, typval.c, ui.c, undo.c, usercmd.c, userfunc.c,
version.c, vim9class.c, vim9cmds.c, vim9compile.c, vim9execute.c,
vim9expr.c, vim9generics.c, vim9instr.c, vim9script.c, vim9type.c,
viminfo.c, window.c
```

**To add files efficiently:**
1. Right-click on the vim target in the Project Navigator
2. Select "Add Files to ios_system..."
3. Navigate to `vim/src/`
4. Select all the files listed above
5. Ensure "Add to targets: vim" is checked
6. Click "Add"

**DO NOT include these files:**
- `gui*.c` - GUI code
- `os_win*.c`, `os_mswin.c`, `os_vms*.c`, `os_amiga.c`, `os_qnx.c` - Other platforms
- `if_*.c` - Language bindings
- `terminal.c`, `channel.c`, `job.c`, `pty.c` - Job/terminal features
- `netbeans.c`, `nbdebug.c` - Netbeans integration
- `*_test.c` - Test files
- `dosinst.c`, `uninstall.c`, `vimrun.c` - Windows installer
- `xdiff/*` - External diff library (optional, can add if needed)
- `wayland.c` - Wayland support

## Step 4: Add Framework Dependencies

Build Phases → Link Binary With Libraries:
1. Click "+"
2. Add `ios_system.framework` (from the project)
3. Add `libncurses.tbd` (from iOS SDK)

## Step 5: Add Public Header

Build Phases → Headers:
1. Move `vim_framework/vim.h` to "Public"

Or copy the content:
```objc
// vim.h - Public header for vim.framework
#ifndef VIM_FRAMEWORK_H
#define VIM_FRAMEWORK_H

#include <Foundation/Foundation.h>

__attribute__((visibility("default")))
int vim_main(int argc, char **argv);

#endif
```

## Step 6: Add Info.plist

1. Copy `vim_framework/Info.plist` to the vim target
2. Set in Build Settings:
   - Info.plist File: `vim_framework/Info.plist`

## Step 7: Add Runtime Bundle

Build Phases → Copy Bundle Resources:
1. Click "+"
2. Add `vim_framework/VimRuntime.bundle`

If not created yet:
```bash
cd vim_framework
./prepare_runtime.sh
```

## Step 8: Configure Scheme

1. Edit Scheme for `vim` target
2. Run → Build Configuration: Release (for testing)
3. Archive → Build Configuration: Release

## Step 9: Verify Build

1. Select `vim` scheme
2. Select a simulator destination
3. Build (⌘B)

Expected warnings (can be ignored):
- Some deprecation warnings for older APIs
- Pointer signedness warnings

Expected output:
- `vim.framework` in DerivedData

## Step 10: Build XCFramework

After the Xcode target is configured:

```bash
swift run --package-path xcfs build vim
```

Output: `.build/vim/vim.xcframework.zip`

## Troubleshooting

### "ios_vim.c not found"
Ensure ios_vim.c exists in `vim/src/`. It was created during the integration process.

### "ios_compat.h not found"
Ensure ios_compat.h exists in `vim/src/`. Check Header Search Paths include `$(SRCROOT)/vim/src`.

### "ios_error.h not found"
Add `$(SRCROOT)/ios_system` to Header Search Paths.

### "Undefined symbol: ios_system"
Ensure ios_system.framework is linked in Build Phases.

### "Multiple definitions" errors
Check that you haven't accidentally included excluded files (gui*.c, terminal.c, etc.)

### Build succeeds but vim crashes
1. Check VIMRUNTIME is set correctly
2. Verify VimRuntime.bundle is included in app
3. Check HOME is set to a writable directory
