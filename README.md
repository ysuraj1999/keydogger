# Keydogger
*Keylogger but funnier*

![Demo](./demo.gif)

> Keydogger is a tiny text expander written in C

## Prerequisites
- `gcc`
- `make`
- membership in the `input` group

```bash
git clone https://github.com/jarusll/keydogger.git
cd keydogger
```

## Configuration

Set `KEYDOGGER_KEYBOARD` to one or more keyboard device paths, separated by commas:

```bash
export KEYDOGGER_KEYBOARD=/dev/input/event0,/dev/input/event15
```

Use `evtest` to identify your keyboard device paths.

At least one listed keyboard must open successfully. Missing entries are
reported as warnings and skipped.

## Installation

Install for the current user (no root required)
```bash
make clean install-user
```
This installs the binary to `~/.local/bin/keydogger`.

## Testing
You can test out sample expansions from the local `keydoggerrc` file by
```bash
make clean build
./keydogger debug
```

## Usage

### Configuring expansions
Set trigger and expansion values in `~/keydoggerrc`. Expansion definitions follow the format `trigger=expansion`.

Triggers and expansions must be printable ASCII. Entries containing any other characters are skipped with a warning.

Here are some sample expansions you might like
```
@hello=Hello, World!
@program=Keydogger
:love:=❤️
@gg=Good game folks, see you tomorrow
@@=jarusll@the-mail.net
:ai=explain in short and simple terms

@backup=rsync -avz ~/important_files/ ysuraj1999@homeserver:backups/
```

### Commands
```bash
export KEYDOGGER_KEYBOARD=/dev/input/event0,/dev/input/event15

keydogger start
keydogger status
keydogger stop

# Restart if you want to switch keyboards or reload expansions
keydogger restart
```

## TODO
### v1
- [x] Daemonize it
- [x] Use `/dev/uinput` to emit events
- [x] Read trigger & expansions from `.keydoggerrc`
### v2
- [x] Unicode support (removed in favor of ASCII-only)
### v2.1
- [x] feat: Keyboard path as environment Variable `KEYDOGGER_KEYBOARD`
### v2.2
- [x] fix: Send keyup for all trigger keys to fix keypresses for characters common in trigger & expansion

### v3
- [ ] Throttle writes with Async IO
- [x] Ability to do ascii only
- [ ] Keychords for actions ie enable/disable, reload

## Acknowledgements
- [emisilve86](https://github.com/emisilve86/Keylogger-Daemon-Linux) for their code
- [Espanso](https://github.com/federico-terzi/espanso) for the `1000` microseconds of delay between events
- [ydotool](https://github.com/ReimuNotMoe/ydotool) for direct mapping of `character code` to `Linux key code`

## Contributing
Patches are preffered over PR because this is a mirror of my personal git server.
