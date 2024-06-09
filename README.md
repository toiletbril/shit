# Shit

Man, my shell is shit.

Revolutionary command-line interpreter, or an interactive shell based on
[toiletline](https://github.com/toiletbril/toiletline) written in C++17. This
program was made as a late april fools joke and literally everything is written
from scratch, so use it at your own risk.

There will be more information soon™ (when I feel like it).

## Building

You need a C++17 compatible compiler (but currently only Clang is supported for
Windows), GNU Make, `rm`, `mkdir` to build the executable; `diff` to run tests;
`clang-format` (better 17 or newer) to format code.

The `MODE` variable controls build type:
* `rel` is the optimized build;
* `dbg` includes all symbols and Asan with Ubsan;
* `cosmo` is an optimized build which will try to use `cosmoc++` from the
  Cosmopolitan toolchain.

An example of the build process:
```bash
$ export MODE=rel/dbg/cosmo
$ make -j14
$ ./shit --help
```

Don't expect the tests to fully pass on Windows yet, since this project is
mainly developed by me on Linux :3

## ...

Is it usable?
- [x] Run programs.
- [x] Work on Linux and Windows.
- [x] Logical sequences. (`&&`, `||`, `;`)
- [x] Pipes.
- [ ] Shell expansion. (`*`, `~`, `!`)
- [ ] Environment variables.
- [ ] Numeric expressions.
- [ ] Background jobs.
- [ ] Redirections. (`>`/`<`)

Is it good?
- [ ] Scripting capabilities. (flow control keywords)
- [ ] Complex scripting capabilites.

Is is exceptional? 
- [ ] Arbitrary precision numeric expressions.
- [ ] Cross-platform replacement for most common Unix programs which Windows
      does not have.
