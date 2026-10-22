# Keydogger
*Keylogger but funnier*

![Demo](./demo.gif)

> Keydogger is a tiny text expander written in C

> [!WARNING]
> This is a privileged application. Hence it NEEDS sudo privileges. I have tried to keep the codebase small to reduce the footprint

## Prerequisites
- `gcc`
- `make`

```bash
git clone https://github.com/jarusll/keydogger.git
cd keydogger
```

## Configuration

Set the environment `KEYDOGGER_KEYBOARD` as the path to your keyboard device.
It should be of the pattern `/dev/input/eventX`. X will be different for your system. You can use `evtest` to find out your keyboard device path.

## Installation

To install, run the following as sudo
```bash
make clean install
```

## Testing
You can test out sample expansions from the local `keydoggerrc` file by
```bash
make clean build
sudo ./keydogger debug
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
@install=sudo package-manager install -y
@update=sudo package-manager update -y
@restart=sudo keydogger stop && sudo keydogger start
@backup=rsync -avz ~/important_files/ jarusll@homeserver:backups/
```

### Commands
```bash
# -E flag to preseve env variables if you've already set KEYDOGGER_KEYBOARD
sudo -E keydogger start
# Here's another way to pass env variables
sudo env KEYDOGGER_KEYBOARD=/dev/input/event<N> keydogger status

sudo keydogger status

sudo keydogger stop

# Restart if you want to switch keyboards or reload expansions
sudo -E keydogger restart
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
