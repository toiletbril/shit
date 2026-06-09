# Shit

Man, my shell is shit™

Also the fastest POSIX-compatible shell there is, even faster than `dash`.

This software initially was made as a late april fools joke and literally
everything is written from scratch in a heavily macro-modified C++ dialect I
can actually stand, and is compiled with `-nostdlib++` :3

There's no guarantee that this project will come any close to being finished or
will not instantly break your computer upon the first start, so use it at your
own risk. 

The goal is to be a native, interactive, `bash`-compatible shell ~~without any
bells and whistles~~, that can be used interchangeably on Windows and Linux and
offer an amazing shell experience on Windows to avoid having to tolerate
PowerShell or some other atrocious crossplatform shells, while being fucking
faster than all of them--and offer basic replacements of most common coreutils
commands like `mkdir`, `rm`, `cat` and others as shell builtins.

## Development

`staging` is the development branch. It may be broken at any time. `master` is
more stable and should usually pass all tests.

You need Clang 18 and later; GNU Make, some coreutils: `rm`, `mkdir` to build
the executable; `cat`, `diff`, `printf` to run the tests; `clang-format`,
`clang-tidy` (better 18 or newer) to check the code.

The `MODE` variable controls build type:
* `rel` is an optimized build;
* `prof` is an optimized build with debug symbols for profiling;
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

## ...

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
- [ ] `bash`-compatible.
- [ ] Most of shellcheck built-in as warnings.
- [ ] Arbitrary precision numeric expressions.
- [ ] Cross-platform replacement for most common Unix programs which Windows
      does not have.
- [ ] Own bells and whistles.
