# shit

[![Shit is faster than Bash at least by 3 times](https://github.com/toiletbril/shit/actions/workflows/ci.yml/badge.svg)](https://github.com/toiletbril/shit/actions/workflows/ci.yml)

Man, my shell is **shit**™ (built on top of
[toiletline](https://github.com/toiletbril/toiletline))

Have you even seen warnings from software that uses Bison as it's parsing
engine? Did you encounter any the coreutils' error messages? Perhaps spent a
day debugging a Bash script?

Shit is the fastest cross-platform Bash and POSIX-compatible shell there is,
even faster than `dash`, with the most friendly UX and errors, and opinionated
interactive experience.

## Three shells in a trenchcoat

**shit** runs in three modes (called moods). The default mood is **shit** being
itself, a strict superset of Bash, with the analysis and optimization stages
enabled.

Before a single command runs, **shit** walks the whole parsed tree to optimize
and analyze it. Default mood prohibits non-deterministic globs, substituions,
variables or anything else that will make the shell behave unexpectedly. Every
error or warning at that stage is called a diagnostic.

`--mood`, short `-M`, selects the mood, one of `shit`, `bash`, or `sh`. The
default is `shit`. Binary symlinked as `sh`, `dash`, or `bash` will pick the
matching mood and disable diagnostics. `set --mood` changes the mood at
runtime. `-W` keeps the diagnostics but turns every error into a warning.

`-I` is mimicry. **shit** will detect `sh`, `dash`, `bash` shebangs and run the
script inside itself in the matching mood rather than launching the real shell
to keep speed and diagnostics.

`--init-moods`/`-L` accepts a comma-separated list of moods to steal and use
init files from. It defaults to the value of `--mood`.

The `shit` mood reads `/etc/shitrc` and `~/.shitrc`, the `bash` flavor reads
the bash rc and its completion, and `sh` reads the file named by `ENV`. So
`--init-moods=shit,bash` runs a strict shit prompt that still loads the whole
bash setup. `set --init-moods` reloads the listed flavors into a live session.
The startup files from non-default mood always turn all errors to warnings.

`SHIT_FLAGS` environment variable may be used to specify flags. The recommended
is `-W -I --init-moods=shit,bash`. Flag on the command line still wins.

When encountering broken flags or arguments in `SHIT_FLAGS`, or supplied when
launched the binary, **shit** acting as a login shell will skip the rc chain,
and give you a rescue session to fix the config from.

## Additional bull**shit**

Modern interactive mode, heavily inspired by
[fish](https://github.com/fish-shell/fish-shell), with syntax highlighting.
sensible word-jumps and controls, full UTF-8 support, wide character (CJK and
emoji) width handling, multiline editing, history search and persistent
history.

**shit** also has more than 50 builtins, each with `--help`. That includes
every builtin from `bash` and POSIX standard, with the addition of:
- `z` -- a port of [zoxide](https://github.com/ajeetdsouza/zoxide)
- `bench` -- built-in benchmark infrastructure inspired by Performance
  Optimizer Observation Platform ([poop](https://github.com/andrewrk/poop) for
  short)
- `calc` -- an arithmetic evaluator that prints in 128 bits in the default mood
- and more

It also bundles a busybox-style set of coreutilities behind the `shitbox`
builtin.

# Development

This software initially was made as a late april fools joke and literally
everything is written from scratch in a heavily macro-modified C++23 dialect I
can actually stand, and is compiled with `-nostdlib++` :3

`staging` is the development branch. It may be broken at any time. `master` is
more stable and should usually pass all tests.

You need Clang 18 and later; GNU Make, some coreutils: `rm`, `mkdir` to build
the executable; `cat`, `diff`, `printf` to run the tests; `clang-format`,
`clang-tidy` (better 18 or newer) to check the code.

The `MODE` variable controls build type:
* `rel` is an optimized build;
* `prof` is an optimized build with debug symbols for profiling;
* `coverage` is an optimized build with debug symbols for collecting coverage;
* `dbg` includes all symbols and Asan with Ubsan;
* `cosmo` is an optimized build which will try to use `cosmoc++` from the
  Cosmopolitan toolchain.
* `cosmo_dbg` is a debug Cosmopolitan build.

`$CXXFLAGS` environment variable can be used to append new flags to the build
commands.

An example of the excruciatingly complex build process:
```bash
$ export MODE=<rel/prof/dbg/cosmo>
$ make
$ ./shit --help
```

`make install` builds the release binary and installs it, the man page, and the
bash completion under `PREFIX`, which defaults to `/usr/local`. `DESTDIR` stages
the tree under a packaging root, `PREFIX` moves the install root, and
`INSTALL_MOOD_SYMLINKS="sh bash dash"` adds the mood symlinks next to the binary.
`make uninstall` removes everything the install placed.
```bash
$ make install PREFIX=/usr/local
$ make uninstall PREFIX=/usr/local
```

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

Is is exceptional? 
- [x] `bash`-compatible.
- [x] Most of shellcheck built-in as warnings.
- [x] Own bells and whistles.
- [x] Cross-platform replacement for most common Unix programs which Windows
      does not have.
- [ ] Arbitrary precision numeric expressions.
