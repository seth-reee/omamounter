<h1><img src="resources/tether.png" width="56" alt="Tether icon" align="absmiddle"> Tether</h1>

A lightweight desktop application for managing NFS and SMB/CIFS network shares
on [Omarchy](https://omarchy.org/).

## Features

- Manage multiple NFS and SMB servers from one interface
- Discover available exports and shares
- Mount or unmount individual selections or all configured shares
- Prefer hostnames with an optional fallback IP address
- Mount shares at boot or automatically on first access
- Follow the active Omarchy light or dark theme
- Store SMB passwords securely with Secret Service
- Keep network and mount operations off the interface thread

## Installation

Install the protocol tools you plan to use:

```bash
sudo pacman -S nfs-utils cifs-utils smbclient
```

Download the latest Omarchy package from [Releases](https://github.com/seth-reee/Tether/releases) and install it with:

```bash
sudo pacman -U tether-*.pkg.tar.zst
```

The package recipe supports Omarchy on x86_64 and ARM64, using Arch Linux and Arch Linux ARM respectively. Use a package whose filename matches your machine's architecture. To build from a tagged release on either architecture, install `base-devel`, `cmake`, `ninja`, and `qt6-base`, then run `makepkg -s` in `packaging/`.

**ARM64 status:** The ARM64 package built and passed automated tests under QEMU, but remains untested on a real ARM64 Omarchy desktop. Network mounting and authorization need a real-system check.

The Tether package replaces omamounter. Tether stores its configuration in a
new location, so add your servers and shares again after upgrading.

For manual installation, extract the binary tarball and follow its
`INSTALL.txt`. It includes the application and required authorization helpers;
Qt and protocol tools are system dependencies.

After installation, launch **Tether** from the application menu or run:

```bash
tether
```

## Usage

On first launch, choose **Add server**. No servers or shares are preconfigured;
the default mount root is `~/Mount`. Existing saved settings are preserved on upgrade.

Open **Settings** to add more servers, choose NFS or SMB, test the connection, and
discover its shares. Shares can also be added manually when discovery is not
available.

Each share supports one of three mounting modes:

- **Manual** — mount and unmount it manually from the main window
- **At boot** — mount it automatically when the system starts
- **On first access** — connect when its local directory is first accessed

Use **Save & Apply** after changing shares or automatic
mounting preferences. Applying system configuration requires administrator
authorization. Routine mount and unmount operations do not.

Unit details are available under **Advanced: preview system configuration**.
If installation fails, Tether attempts to restore the previous configuration
files. Shares stopped during recovery may need mounting again. If a server is
offline after installation, the configuration stays saved so mounting can be retried.

## Security

The graphical application never runs as root. Privileged operations are
limited to validated Tether configuration and shares previously approved
by the user.

SMB passwords are kept out of the application configuration and command-line
arguments. They are stored with Secret Service and installed for boot mounting
as root-readable credentials.

## Troubleshooting

- NFSv4 servers do not always support export discovery. Add the export path
  manually when it does not appear.
- Confirm that the required protocol tools are installed and that the server
  permits access from this machine.
- Apply the system configuration once before using the main-window mount and
  unmount controls.
- User configuration is stored in `~/.config/tether/config.json` unless
  `XDG_CONFIG_HOME` is set.

## License

Tether is available under the [MIT License](LICENSE).
