# mtop

A tiny `top` for macOS (Mac top) that does one thing well: find a CPU or memory hog and kill it.

Single C file, uses only system ncurses and native macOS APIs (`libproc`, `sysctl`, Mach host stats) — no dependencies, no shelling out to `ps`.

```
🔥 CPU 27% | 🧠 MEM 13.2G/32G | 📈 LOAD 2.1 1.9 1.7 | ⏱️  REFRESH 1s

  PID       CPU%       MEM  NAME
──────────────────────────────────────────────────────────────
▶ 82509      6.5   682.3M  dbeaver
  456        6.2   421.0M  WindowManager
  521        6.1   290.4M  Terminal
  ...
──────────────────────────────────────────────────────────────
  CMD  /Applications/DBeaver.app/Contents/MacOS/dbeaver
  ARGS -vmargs -Xmx2g
  CWD  /Users/me

[←/→] refresh time  [space] pause  [tab] details  [k] kill  [c] sort cpu  [m] sort mem  [/] filter  [q] quit
```

## Features

- As many top processes as fit the terminal (bottom 16 rows are reserved for details), by CPU or memory
- System CPU %, memory used/total, load average
- Command, arguments and working directory of the selected process, word-wrapped
- Selected process stays on screen (pinned to the last row) even if it drops off the list
- Name filter, adjustable refresh interval (0.25s – 10s)

## Build & install

```sh
make              # build ./mtop
make install      # copy to /usr/local/bin (override with PREFIX=...)
make uninstall
```

Requires Xcode Command Line Tools (`xcode-select --install`).

## Keys

| Key               | Action                                   |
|-------------------|------------------------------------------|
| `↑` / `↓`         | Move selection                           |
| `fn`+`↑` / `↓`    | Page up / down (list or details pane)    |
| `←` / `→`         | Faster / slower refresh                  |
| `Space`           | Pause / resume updates                   |
| `Tab`             | Focus details pane; `↑`/`↓` then scroll it |
| `k`               | Kill selected process (`SIGTERM`)        |
| `c`               | Sort by CPU                              |
| `m`               | Sort by memory                           |
| `/`               | Filter by name (Enter to keep, Esc to clear) |
| `q`               | Quit                                     |

## Notes

Without `sudo`, details of other users' and system processes may be unavailable, and only your own processes can be killed.

## License

[MIT](LICENSE)
