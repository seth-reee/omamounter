# omamounter

omamounter is a lightweight native Qt 6 Widgets application for managing NFS
and SMB/CIFS shares on Omarchy Linux. The GUI is an ordinary unprivileged C++
application. Two small QtCore-only helpers perform narrowly scoped privileged
configuration and mount control through fixed Polkit actions.

## Runtime requirements

Omarchy already provides Qt 6, Polkit, and Secret Service support. Protocol
tools are installed only when needed:

```bash
sudo pacman -S nfs-utils cifs-utils smbclient
```

No Python, PySide, Rust, Cargo, or third-party application runtime is required.

## Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The development executable is `build/omamounter`. Privileged Apply and
Mount/Unmount actions require package installation because Polkit uses fixed
helper paths under `/usr/lib/omamounter`.

## Arch package

Create `omamounter-0.2.0.tar.gz`, place it beside the PKGBUILD, then run:

```bash
cd packaging
makepkg -si
```

The package installs `/usr/bin/omamounter`, two helpers under
`/usr/lib/omamounter`, the desktop launcher, and the fixed Polkit policy.

## Usage

First launch imports the editable reference configuration for `sakuya.weeb`,
its fallback IP, and the nine NFS shares. Open **Settings** to edit servers and
shares, test connectivity, discover exports/shares, enter SMB credentials, and
choose an automatic mounting mode:

- `disabled`: manually controlled from the main window.
- `boot`: mounted when the machine boots.
- `access`: mounted by systemd when its local directory is accessed.

Use **Preview / apply system configuration** to review generated units. Apply
requires administrator authorization. Afterwards Mount/Unmount operations are
passwordless but restricted to share IDs in the root-owned manifest.

SMB passwords are stored through Secret Service, never in normal JSON or
process arguments. Apply transfers them to the helper over standard input;
boot credentials are stored root-only.

## Troubleshooting

- NFSv4 servers may not expose exports through `showmount`; manual entry is
  always available.
- If a hostname does not resolve, omamounter uses its configured fallback IP
  for connection testing and discovery.
- Apply must be completed once before main-window Mount/Unmount actions work.
- Configuration is stored below `$XDG_CONFIG_HOME/omamounter` or
  `~/.config/omamounter`.
