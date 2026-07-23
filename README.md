# shit

[![Shit is at least 3 times faster than Bash](https://github.com/toiletbril/shit/actions/workflows/ci.yml/badge.svg)](https://github.com/toiletbril/shit/actions/workflows/ci.yml)

Man, my shell is **shit** (built on top of
[toiletline](https://github.com/toiletbril/toiletline))

Have you ever seen warnings from software that uses Bison as its parsing
engine? Did you encounter any of the coreutils' error messages? Have you spent a
day debugging a Bash script?

**Shit** is a cross-platform shell compatible with Bash and POSIX. It runs at
least 3 times faster than Bash. The UX, the errors, and the interactive
experience are opinionated.

**Shit** is designed to work without absolutely no config whatsoever. **shit**'s
Linux binary is static. **shit** also works without coreutils, using it's own
shitbox utilities instead. All so you can copy shit to any machine and enjoy the
shell.

**The project is in early stage**. There's no gurantee that it will not blow
you computer up upon the first start. Bug reports are greatly appreciated.

## Three shells in a trenchcoat

[See the manpage](docs/shit.1) for fuller explanation.

**shit** runs in three modes (called moods). The default mood is **shit** being
itself, a strict superset of Bash, with the analysis and optimization stages
enabled.

Before a single command runs, **shit** walks the whole parsed tree to optimize
and analyze it. Default mood prohibits non-deterministic globs, substitutions,
variables, and any other source of unexpected shell behavior. Every error or
warning at that stage is called a diagnostic.

`--mood`, short `-M`, selects the mood, one of `shit`, `bash`, or `sh`. The
default is `shit`. A binary symlinked as `sh`, `dash`, or `bash` will pick the
matching mood and disable diagnostics. `set --mood` changes the mood at
runtime. `-W` demotes lenient diagnostics to warnings. `-WW` demotes strict
diagnostics too and reports runtime warnings in every mood.

Source backtraces are omitted when direct input has no eval or source
indirection. `--no-traces` suppresses every source backtrace. The primary
message, source caret, and note remain visible.

`-I` is mimicry. **shit** will detect `sh`, `dash`, `bash` shebangs and run the
script inside itself in the matching mood. The in-process run keeps speed and
diagnostics.

`--init-moods`/`-L` accepts a comma-separated list of moods to steal and use
init files from. It defaults to the value of `--mood`.

The `SHIT_FLAGS` environment variable may be used to specify flags. The
recommended set is `-W -I --init-moods=shit,bash`. A flag on the command line
still wins.

When encountering broken flags or arguments in `SHIT_FLAGS`, or supplied when
the binary is launched, **shit** acting as a login shell will skip the rc chain,
and give you a rescue session to fix the config from.

## Additional bull**shit**

The interactive mode is modern and heavily inspired by
[fish](https://github.com/fish-shell/fish-shell). It has syntax highlighting,
sensible word-jumps and controls, full UTF-8 support, wide character (CJK and
emoji) width handling, multiline editing, history search, and persistent
history.

**shit** has more than 50 builtins, each with `--help`. That includes
every builtin from `bash` and POSIX standard, with the addition of:
- `z`, a port of [zoxide](https://github.com/ajeetdsouza/zoxide)
- `bench`, built-in benchmark infrastructure inspired by Performance
  Optimizer Observation Platform ([poop](https://github.com/andrewrk/poop) for
  short)
- `assimilate`, a transactional installer for an SSH target
- and more

It also bundles a busybox-style set of little coreutilities behind the
`shitbox` builtin, such as:
- `cp`, `mv`, `ln`, `rm` and other fileutils
- `find` and `grep`
- `killall`, `pkill`, `ps`, `timeout` and `nproc`
- minimal `calc` and `make`
- and more

# Development

This software was initially made as a late April Fools joke, and everything is
written from scratch in a heavily macro-modified C++23 dialect I can actually
stand, and is compiled with `-nostdlib++`. **shit**'s only dependency is the
libc.

`staging` is the development branch. It may be broken at any time. `master` is
more stable and should usually pass all tests.

You need Clang 18 or later and GNU Make. Building the executable needs the
coreutils `rm` and `mkdir`. Running the tests needs `cat`, `diff`, and `printf`.
Checking the code needs `clang-format` and `clang-tidy`, ideally 18 or newer.

The `MODE` variable controls build type:
* `rel` is an optimized build.
* `prof` is an optimized build with debug symbols for profiling.
* `cov` is an optimized build with debug symbols for collecting coverage.
* `dbg` includes all symbols and Asan with Ubsan.
* `cosmo` is an optimized build that uses `cosmoc++` from the Cosmopolitan
  toolchain.
* `cosmo_dbg` is a debug Cosmopolitan build.

`TARGET` defaults to the host and accepts `Linux`, `Windows_NT`, or `Darwin`.
A non-Windows host cross-compiles `TARGET=Windows_NT` with MinGW. A non-Darwin
host cross-compiles `TARGET=Darwin ARCH=arm64` with osxcross. Linux is a native
target.

The `$CXXFLAGS` environment variable can be used to append new flags to the
build commands.

An example of the build process is shown below. Make uses every logical CPU and
shares the bounded job pool with recursive builds.
```bash
$ make MODE=<rel/prof/dbg/cov/cosmo/cosmo_dbg>
$ make MODE=rel TARGET=Windows_NT
$ make MODE=rel TARGET=Darwin ARCH=arm64
$ ./shit --help
```

Or use Zig. Zig only supports `dbg` and `rel`:
```bash
$ zig build --release=fast
$ ./zig-out/bin/shit --help
```

And install:
```bash
$ export PREFIX=/usr/local
$ make install
$ make uninstall
```

The running binary can install itself on an SSH target with `assimilate
user@host`.

## Roadmap

Is it usable?
- [x] Run programs.
- [x] Work on Linux and Windows.
- [x] Logical sequences. (`&&`, `||`, `;`)
- [x] Pipes.
- [x] Redirections. (`>`/`<`)
- [x] Shell expansion. (`?`, `[...]`, `*`, `~`)
- [x] Escaping.
- [x] Environment variables.
- [x] Numeric expressions.

Is it good?
- [x] Background jobs.
- [x] Scripting capabilities. (flow control keywords)
- [x] Blocks and functions.
- [x] `sh`-compatible.

Is it exceptional?
- [x] `bash`-compatible.
- [x] Most of shellcheck built-in as warnings.
- [x] Own bells and whistles.
- [x] Cross-platform replacement for common Unix programs absent on Windows.
- [ ] Arbitrary precision numeric expressions.
