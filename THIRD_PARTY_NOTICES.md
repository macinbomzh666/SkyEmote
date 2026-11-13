# Third-party notices

SkyEmote's original source is MIT licensed. The DLL links CommonLibSSE-NG;
that combined binary is subject to GPL-3.0-or-later with CommonLib's modding
and linking exceptions. Preserve the MIT grant for the original SkyEmote files.

| Dependency | Source/version selection | Terms |
| --- | --- | --- |
| CommonLibSSE-NG | [8ffc2b7](https://github.com/alandtse/CommonLibSSE-NG/tree/8ffc2b74af015f837de53cc02b0cb470a15678e4) | [GPL](licenses/CommonLibSSE-NG-GPL-3.0.txt), [exceptions](licenses/CommonLibSSE-NG-EXCEPTIONS.txt), [original MIT notices](licenses/CommonLibSSE-NG-original-MIT.txt). |
| OpenVR headers | CommonLib submodule 60eb187801956ad277f1cae6680e3a410ee0873b | [BSD-3-Clause](licenses/OpenVR.txt); VR runtime support is disabled. |
| spdlog | 1.16.0 | [MIT](licenses/spdlog.txt) |
| DirectXTK | February 2024 | [MIT](licenses/directxtk.txt) |
| DirectXMath | February 2024 | [MIT](licenses/directxmath.txt) |
| rapidcsv | 8.92 | [BSD-3-Clause](licenses/rapidcsv.txt) |
| nlohmann/json | 3.12.0, checksum-pinned single header | [MIT](licenses/nlohmann-json.txt) |
| Dear ImGui | 1.92.9, checksum-pinned source archive | [MIT](licenses/imgui.txt) |
| Sovngarde Light | Unmodified font by [mjorka](https://mjorka.itch.io/sovngarde) | [SIL OFL 1.1](licenses/Sovngarde-OFL-1.1.txt), no Reserved Font Name |

The Linux build uses spdlog's standard-library formatting backend. The package
includes the license texts listed above. A binary release must also provide its
corresponding source and build material under the applicable dependency terms.
