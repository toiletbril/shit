# Shit

Revolutionary command line interpreter of a custom scripting language, or an
interactive shell based on
[toiletline](https://github.com/toiletbril/toiletline) written in C++17. This
program was made as a late april fools joke and everything is written
exclusively from scratch, so use it at your own risk.

There will be more information soon™ (when I feel like it).

## Building

```sh
$ export MODE=rel/dbg/cosmo
$ make -j14
$ ./shit --help
```

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
