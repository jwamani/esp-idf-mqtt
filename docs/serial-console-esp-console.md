# The Serial Console: Why and How We Moved to `esp_console`

> Companion to the System Operation Report. This document covers requirement
> **2.5 (Serial Interface and Management Menus)**: how the console was built
> first, why it was rebuilt on `esp_console`, and how each piece works.

---

## 1. The problem with the first console

The first version of the management console was a hand-written FreeRTOS task:

1. It called `getchar()` in a loop, buffering one character at a time.
2. When Enter arrived it matched the buffer against commands with
   `strcmp` (`schedule set`, `override on`, ...) and dispatched by hand.
3. It printed the `CoCIS> ` prompt itself after every command.

It *worked* — commands were received, parsed and executed — but in practice
it did not behave like a serial terminal, for two independent reasons:

- **No echo.** `getchar()` reads raw bytes from the console UART and echoes
  nothing. On a desktop OS the kernel's terminal driver echoes keystrokes;
  an ESP32 has no such layer unless the program provides it. So every typed
  character was invisible until the command's *output* appeared.
- **One shared stream.** The console and ESP-IDF logging (`ESP_LOGI` from
  WiFi, MQTT, room messages...) both write to the same UART0. While the user
  held the prompt, periodic log lines from other tasks were printed at the
  cursor position, burying both the prompt and the (invisible) input.

The root cause in both cases was the same: we had reinvented a *terminal
line-discipline layer* (echo, editing, redraw, stream discipline) and
rebuilt only part of it. That is exactly what a mature terminal library
solves — so we did not.

## 2. Options considered and the choice

| Option | What it is | Why rejected/chosen |
|---|---|---|
| **A. Patch the hand-written loop** | echo chars + backspace + a `loglevel` command | ~15 lines; would have fixed the symptoms but kept a hand-built parser with no history or completion |
| **B. `esp_console` (IDF's console component)** | line editing + history + Tab completion (linenoise) + argc/argv command registration + REPL task | **chosen** — official Espressif component, ships inside ESP-IDF v6 (no new dependency), removes our entire parser |
| **C. Split streams** | logs on UART1, console on UART0 | cleanest on real hardware, but Wokwi only displays UART0 — the visible log stream would disappear |

Option B is not forced through a magic switch: we verified that
`linenoise` (the line editor underneath) needs a terminal that supports
**ANSI escape sequences**, and that Wokwi's serial monitor renders them —
first with a throwaway spike (§3), then in the real firmware.

## 3. The spike: prove it works before migrating

We built a minimal one-file project (`console-spike`) that that does nothing
but start the REPL with two dummy commands (`hello`, `ping`) and the
`CoCIS> ` prompt. Failures there cost minutes instead of entangling the
member's module.

Verified in Wokwi's serial monitor:

- the prompt renders as `CoCIS> ` (no `[6n` escape garbage, the classic
  symptom of a terminal that cannot do escape sequences),
- typing is echoed, Backspace redraws correctly,
- Tab completes command names, Up/Down walk the history.

Only then was the real CLI migrated.

## 4. The new console design

### 4.1 The REPL owns the terminal

`management_start_cli()` no longer creates a reader task and no longer
prints a prompt. It calls:

```c
esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
repl_config.prompt = "CoCIS> ";
repl_config.max_history_len = 10;

esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

esp_console_new_repl_uart(&uart_config, &repl_config, &repl);  // driver + linenoise + REPL task
esp_console_register_help_command();                            // free 'help' command
...                                                              // register our commands
esp_console_start_repl(repl);
```

From that point, echo, line editing, history and the prompt are the
component's job. We register commands; we never again read characters.

### 4.2 Commands become registered handlers

Each command is a small plain-C function `int cmd_x(int argc, char **argv)`
and one table entry (`name`, `help`, `hint`, `func`). Dispatch is via
`esp_console_run`, which splits the line into argv (handling spaces and
quoting). The command set:

| Command | Behaviour |
|---|---|
| `status` | built-in block (times, open-now, admin, override) **+ application extras** (§4.4) |
| `login` | prompts for a PIN and **masks it** (§4.3) |
| `logout` | clears the admin session |
| `override on/off` | emergency override, admin-only like before |
| `schedule show/set` | show or change the access window (`schedule set 08:00 14:30`) |
| `logs info/warn` | runtime log discipline (§4.5) |
| `help` | generated automatically from the registered commands |

The previous single-string parser (`strncmp(command, "schedule set ", 13)`)
is gone; argument parsing is per-command with explicit `argc` checks, and
usage is printed on bad calls.

### 4.3 The masked PIN (Requirement 2.6)

With `esp_console`, a PIN typed as an argument would be *echoed* and stored
in the command history — unacceptable. So `login` takes no argument:
its handler temporarily takes over reading from the raw stream itself
(the REPL thread is blocked inside the handler during this), printing one
`*` per character and supporting Backspace. The PIN is therefore never
echoed and never lands in the REPL history. The attempt-counter, lockout
and session-timeout logic around the PIN is unchanged from the original
implementation.

### 4.4 The status extension hook (Requirement 2.5)

Requirement 2.5 asks the serial interface to provide *access records,
current building occupancy, room occupancy status and lighting status*.
The console lives in a self-contained component, while occupancy and the
room table live in the hub firmware — a component must not reach into its
consumer's internals. The seam is one registration hook:

```c
// application side (hub firmware)
management_register_status_ext(print_hub_status_ext);
```

`print_hub_status_ext()` prints *who is inside right now* (from the
occupancy module) and *each room's occupied/light/online state* (from the
room table). The console calls it at the end of `status` if something is
registered — the component stays dependency-free and the hub decides what
"full status" means. Access records remain on the SD card
(`access_log.csv`), by design.

### 4.5 Log discipline: the `logs` command

Console and logs still share UART0 — that one is physics, not software.
What we control is volume. `logs warn` sets the global runtime level with
`esp_log_level_set("*", ESP_LOG_WARN)`, so during a session the periodic
INFO noise from MQTT/WiFi/room statuses stops (the SD card keeps logging
regardless; only the *serial* channel is quieted). `logs info` restores
verbose output for debugging. Default during a demo: `logs warn`.

### 4.6 The bridge into the access logic

The decision engine asks two questions (`schedule_is_open()`,
`override_active()`) — interfaces, not implementations. A half-page bridge
(`management_bridge.c`) maps them onto the management component's accessors:
the access logic never learns where the schedule lives.

## 5. Files touched

| File | Change |
|---|---|
| `components/management/management.c` | old CLI task + parser removed; REPL + command handlers added; status/schedule reads take the schedule mutex |
| `components/management/management.h` | added `management_register_status_ext()` |
| `components/management/CMakeLists.txt` | `PRIV_REQUIRES console` |
| `main/management_bridge.c` | new: schedule/override glue for the access logic |
| `main/hub_status.c/.h` | new: the extra status block (occupancy + rooms) |
| `main/app_main.c` | init: management, register status extension, start REPL; `stubs.c` no longer compiled |
| `main/CMakeLists.txt` | `management` dependency; `management_bridge.c`, `hub_status.c` added; `stubs.c` removed |
| `sdkconfig.defaults` | `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` — the console component pushed the app past the default 1 MB app partition (build error: `app partition is too small for binary`, 0x105c90 > 0x100000) |

`console-spike/` remains in the tree as the demonstration project for the
REPL itself.

## 6. What we traded away (stated honestly)

- The PIN is masked while typing, but the fact that authentication happens
  on a shared, unencrypted UART is unchanged — same as before, same as
  documented in the report's limitations.
- Logs cannot be physically separated on Wokwi; `logs warn` is a policy,
  not an isolation mechanism. On real hardware a second UART would be the
  proper isolation and the code is organised so that port costs nothing.
- The console is *another consumer* of the same shared-terminal file
  descriptor; that is fine because the REPL thread and the logging calls
  serialise at the UART driver TX ring buffer.
