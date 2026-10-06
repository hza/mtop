# ktop

A tiny `top` for macOS that does one thing well: find a CPU or memory hog and kill it.

Single C file, uses only system ncurses and native macOS APIs (`libproc`, `sysctl`, Mach host stats) — no dependencies, no shelling out to `ps`.

```
🔥 CPU 27% | 🧠 MEM 13.2G/32G | 📈 LOAD 2.1 1.9 1.7 | ⏱️  REFRESH 1s

  PID       CPU%   MEM%  NAME
──────────────────────────────────────────────────────────────
▶ 82509      6.5    2.1  dbeaver
  456        6.2    1.3  WindowManager
  521        6.1    0.9  Terminal
  ...
──────────────────────────────────────────────────────────────
  CMD  /Applications/DBeaver.app/Contents/MacOS/dbeaver
  ARGS -vmargs -Xmx2g
  CWD  /Users/me

[←/→] refresh time  [k] kill  [s] sort cpu  [m] sort mem  [/] filter  [q] quit
```

## Features

- Top 16 processes by CPU or memory
- System CPU %, memory used/total, load average
- Command, arguments and working directory of the selected process, word-wrapped
- Selected process stays on screen (pinned to the last row) even if it drops out of the top 16
- Name filter, adjustable refresh interval (0.25s – 10s)

## Build & install

```sh
make              # build ./ktop
make install      # copy to /usr/local/bin (override with PREFIX=...)
make uninstall
```

Requires Xcode Command Line Tools (`xcode-select --install`).

## Keys

| Key               | Action                                   |
|-------------------|------------------------------------------|
| `↑` / `↓`         | Move selection                           |
| `←` / `→`         | Faster / slower refresh                  |
| `k` / `Backspace` | Kill selected process (`SIGTERM`)        |
| `s`               | Sort by CPU                              |
| `m`               | Sort by memory                           |
| `/`               | Filter by name (Enter to keep, Esc to clear) |
| `q`               | Quit                                     |

## Notes

Without `sudo`, details of other users' and system processes may be unavailable, and only your own processes can be killed.

## License

[MIT](LICENSE)
