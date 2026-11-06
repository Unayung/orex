# orex

![orex showing a Rex session with three panes](docs/screenshot.png)

A [Rex](https://superlogical.com) client for [Omarchy](https://omarchy.org). Rex
keeps terminal sessions running on a server, and its Mac app shows them. Rex has no
Linux client yet, so orex is one: it shows every session on the Rex server of this
machine, with its tabs and splits, and keeps them in step with whatever the Mac app
does to the same sessions.

The terminal is [libghostty-vt](https://ghostty.org), the core Rex itself uses, so
orex reads Rex's terminal snapshots directly and draws what the program in each pane
draws. Colors follow the Omarchy theme, and orex hands that palette to Rex, so
programs that ask the terminal for its colors get the ones you see.

## Use

```sh
rex new work        # if the server has no sessions yet
orex                # opens the first session
orex work           # opens the session labeled "work"
```

orex connects to `$REX_SERVER`, or to the local server's socket at
`/run/user/$UID/rex/server.sock`.

When orex shows a pane, it becomes the pane's owner, so the pane takes the size of
orex's window. The Mac app's view of that pane is resized until orex closes and gives
it back.

## Keys

| Key | Does |
|---|---|
| Ctrl+Shift+] / Ctrl+Shift+[ | Next / previous tab |
| Ctrl+Shift+Down / Ctrl+Shift+Up | Next / previous session |
| Ctrl+Shift+B | Show or hide the session list |
| Ctrl+Shift+C | Copy the selection |
| Ctrl+Shift+V, Shift+Insert | Paste the clipboard |
| Middle click | Paste the primary selection |
| Shift+PageUp / Shift+PageDown | Scroll back / forward a page |
| Shift+drag | Select, even when the program uses the mouse |

Click a pane to focus it. Everything else goes to the program in the pane.

With more than one pane, each has a handle on its top edge. Drag it onto
another pane: near an edge puts it on that side, the middle swaps the two.
Panes move between the layout and floating windows the same way.

## Build

orex needs Qt 6 (base, declarative, wayland) and the `ghostty` package, which
installs libghostty-vt.

```sh
bin/build           # build/orex
bin/install         # builds the package and installs it, so orex is in the launcher
```

## Not yet

Floating layers, Kitty images, rectangular selection, a scrollbar, connecting to
Rex servers on other machines, and resuming a stream after a dropped connection
(orex reconnects from scratch when the server restarts).

## License

MIT. See [LICENSE](LICENSE).
