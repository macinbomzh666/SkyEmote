# SkyEmote

An SKSE emote menu with a bundled vanilla catalogue, favorites and optional
animation packs. Press `[` during gameplay to open it.

See [settings and usage](docs/settings.md), the [pack format](docs/packs.md)
and [third-party notices](THIRD_PARTY_NOTICES.md).

## Build on Fedora 44

Run from this repository's directory. The paths below use `~/skyemote` as an
example location for the external MSVC toolchain and Wine prefix; adjust them
to where you installed those tools.

```bash
sudo dnf install -y cmake ninja-build git python3 wine binutils

export PATH="$HOME/skyemote/tools/msvc/bin/x64:$PATH"
export WINEPREFIX="$HOME/skyemote/tools/wine-prefix"
export WINEDEBUG=-all

bash tools/check.sh
```

The output is `build/linux/SkyEmote.zip`. Install it with a mod manager.
It contains `SKSE/Plugins/SkyEmote.dll` and license notices.

The local pipeline builds dependencies, runs functional tests through Wine,
builds the plugin and verifies the ZIP. It stops on failure. To rerun only the
functional tests after configuration:

```bash
cmake --build --preset checks
ctest --preset checks
```

Our toolchain was MSVC 19.51.36260.0 / Windows SDK 10.0.26100.0 through
[msvc-wine](https://github.com/mstorsjo/msvc-wine), Wine 11.20, CMake 4.3.0 and
Ninja 1.13.2. CMake downloads checksum-pinned dependency sources and installs the
Windows x64 libraries into `build/prefix`, using the dynamic MSVC CRT and
standard-library formatting. Existing build outputs are reused on subsequent
runs. CommonLib and Dear ImGui are built from pinned sources during the plugin build.
