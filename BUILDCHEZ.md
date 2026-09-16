# Building Chez Scheme as PIC (for plugin linking)

How to produce a position-independent Chez kernel that can be statically
linked into a Hyprland plugin `.so`. Verified 2026-09-16 with Chez 10.5.0-pre
(git main) on this machine.

## Build

```sh
cd ~/GITE/ChezScheme            # git clone of Chez Scheme
git submodule update --init stex zuo
CFLAGS="-fPIC -O2" ./configure --threads
make -j$(nproc)
```

- `CFLAGS` is respected by Chez's configure — no patching needed. Chez's own
  build already uses `-fPIC -shared` for its foreign objects, so PIC is an
  anticipated configuration.
- `--threads` matches the ta6le ABI (what Arch's package and our boot files use).
- `make kernel` compiles the C objects but the zuo build does NOT combine
  them into a single `kernel.o` (that was the old build system). Two options,
  equivalent — we verified both:
  - link the individual `ta6le/c/*.o` directly into the plugin (what
    `~/GITE/chez-pic/libplugin.so` does), or
  - make a drop-in combined object: `ld -r -o kernel.o ta6le/c/*.o` minus
    `main.o` (adds zlib via `-lz`, not embedded like the old kernel.o had)

## Artifacts we consume

```
ta6le/c/*.o                     # PIC kernel objects — link ALL of these
ta6le/lz4/lib/lz4.o             # except main.o (has main()) — exclude it
ta6le/boot/ta6le/petite.boot    # boot files MUST come from this same build
ta6le/boot/ta6le/scheme.boot
ta6le/boot/ta6le/scheme.h       # + equates.h for compilation
```

A copy of the working set lives in `~/GITE/chez-pic/` (objects + boot files +
headers + `libplugin.so`, the proof-of-concept).

## Linking into a plugin

```sh
gcc -shared -fPIC -o myplugin.so plugin.c -I. \
    $(ls *.o | grep -v plugin) \
    -lpthread -lm -ldl -lrt -lcurses -llz4 -lz
```

- Use `-I.` with a local `scheme.h` copy (`#include <scheme.h>` ignores the cwd).
- `CrashReporter::createAndSaveCrash(int)` is an exported symbol of the running
  Hyprland — a plugin can call it to restore crash reporting after Chez
  displaces the signal handlers (re-install SIGSEGV/SIGABRT after `Sbuild_heap`).

## Gotchas

- **Boot files must match the kernel build.** Version/config mismatch fails at
  startup: "petite.boot is for Version X; need Version Y". Never mix the
  system package's boot files with a self-built kernel.
- The packaged `scheme.h` is read-only (`r--r--r--`) — `chmod u+w` before
  overwriting a copy.
- Chez installs its own SIGSEGV/SIGINT/SIGPIPE/SIGILL/SIGFPE/SIGBUS handlers
  during `Sscheme_init` (unconditionally, `schsig.c: init_signal_handlers`) —
  displacing the host's. Re-install the host's handlers after Chez init.
- `dlopen` the plugin with `RTLD_NOW`; unload requires quiescing all scheme
  callbacks first, then `Sscheme_deinit` before `dlclose`.
