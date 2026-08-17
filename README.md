# wayland-ssh-askpass

Native Wayland SSH askpass utility built with `wayland-client`, Cairo, Pango, and xkbcommon.

## Features

- Centered password prompt for Wayland compositors
- Uses layer-shell when available, with xdg-shell fallback support
- Accepts the SSH prompt string as a command-line argument
- Supports `Enter`, `Escape`, and `Backspace`
- Masks password input on screen
- Times out after 30 seconds
- Prints the password to `stdout` for `ssh` and clears the password buffer before exit

## Build

```bash
make
```

## Usage

```bash
export SSH_ASKPASS=/usr/bin/wayland-ssh-askpass
export SSH_ASKPASS_REQUIRE=force

ssh-add ~/.ssh/id_rsa
```

SSH invokes the program with the prompt text automatically, for example:

```bash
wayland-ssh-askpass "Enter passphrase for key /home/user/.ssh/id_rsa:"
```

## Install

```bash
sudo make install
```
