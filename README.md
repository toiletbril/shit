# shit

[![Shit is faster than Bash at least by 5 times](https://github.com/toiletbril/shit/actions/workflows/ci.yml/badge.svg)](https://github.com/toiletbril/shit/actions/workflows/ci.yml)

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
and analyze it. If a command that cannot resolve, a malformed glob/redirection,
which a normal shell will half-assedly run and leave you with a mess, gets
caught up front and the run stops. The same stage carries most of shellcheck
built in as warnings. Every error or warning at stage is called a diagnostic.

`--mood`, short `-M`, selects the mood, one of `shit`, `bash`, or `sh`. The
default is `shit`. If the binary is named or symlinked to `sh`, `dash`, or
`bash`, it picks the matching mood and disables diagnostics. `set --mood`
changes the mood at runtime. `-W` keeps the diagnostics but turns every error
into a warning and lets the run continue to stay compatible.

`-I` is mimicry. With it on, a script whose shebang names `sh`, `dash`, `bash`
runs inside **shit** in the matching mood rather than launching the real shell
for more speed.

`--init-moods`, short `-L`, lists which startup files to load, one flavor per
name, comma separated or by repeating the flag, and defaults to the value of
`--mood`. The `shit` flavor reads `/etc/shitrc` and `~/.shitrc`, the `bash`
flavor reads the bash rc and its completion, and the `sh` flavor reads the file
named by `ENV`. So `--init-moods=shit,bash` runs a strict shit prompt that still
loads the whole bash setup. `set --init-moods` reloads the listed flavors into a
live session. The startup files always source lenient, so an unset variable or
an unmatched glob in a profile never aborts the login.

`SHIT_FLAGS` sets your defaults once. Put the recommended `-W -I
--init-moods=shit,bash` in it and every **shit** starts that way, while a flag
on the command line still wins.

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
- and more

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
- [ ] Arbitrary precision numeric expressions.
- [ ] Cross-platform replacement for most common Unix programs which Windows
      does not have.
