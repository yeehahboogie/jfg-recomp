# Contributing

Bug reports, playtesting notes and pull requests are welcome.

## Reporting a problem

Open an issue with:

- what happened and what you expected, and where in the game (level name, or what you were doing);
- your distribution, graphics card and driver version;
- the commit you built (`git rev-parse --short HEAD`);
- whether the original game does the same on a console or an accurate emulator, if you can check.

If the game crashes, run `jet-force-gemini` from a terminal and attach what it prints: the port prints a crash report
with a backtrace.

## Changing the code

- See [BUILDING.md](BUILDING.md) for the build and the tests. Run `scripts/test-hooks.sh` and the romtool tests
  before sending a pull request.
- **Never commit anything taken from the game:** the ROM, files generated from it (`generated/`, `build/`), saves,
  memory dumps, screenshots, or data copied out of it. Tests use made-up data.
- Keep a change to one thing, and say in the pull request how you checked it.
