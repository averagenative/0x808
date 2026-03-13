# Dependency Audit

Last audited: 2026-03-12

| Dependency | Version | License | Known Issues |
|---|---|---|---|
| cJSON | 1.7.19 | MIT | None known |
| dr_wav | 0.14.5 | Public Domain / MIT-0 | None known |
| dr_mp3 | 0.7.3 | Public Domain / MIT-0 | None known |
| dr_flac | 0.13.3 | Public Domain / MIT-0 | None known |
| Nuklear | 4.12.2 (header says 1.32.0) | Public Domain | None known |
| nuklear_sdl_gl3 | 1.32.0 | Public Domain | None known |
| miniaudio | 0.11.25 | Public Domain / MIT-0 | None known |
| TinySoundFont (tsf.h) | 0.9 | MIT | None known |
| SDL2 | 2.30.12 | zlib | None known |
| shine (MP3 encoder) | unversioned (vendored source) | LGPL-2.0 (originally from libshine) | License is LGPL; used as source, not linked as shared lib. Review if distributing binaries. |
| CPLUG | unversioned (2024) | MIT / Public Domain (Unlicense) | None known |
| glad | 2.0.8 (generated) | (WTFPL / CC0-1.0) AND Apache-2.0 | None known; generated loader, not a runtime dependency |
| gl3_loader.h | N/A (project-local) | Project code | Not a third-party dependency |

## Version Source Details

- **cJSON**: `CJSON_VERSION_MAJOR/MINOR/PATCH` in `cJSON.h` = 1.7.19
- **dr_wav**: `DRWAV_VERSION_MAJOR/MINOR/REVISION` in `dr_wav.h` = 0.14.5
- **dr_mp3**: `DRMP3_VERSION_MAJOR/MINOR/REVISION` in `dr_mp3.h` = 0.7.3
- **dr_flac**: `DRFLAC_VERSION_MAJOR/MINOR/REVISION` in `dr_flac.h` = 0.13.3
- **Nuklear**: Header comment says "Nuklear - 1.32.0 - public domain" in `nuklear_sdl_gl3.h`. Main `nuklear.h` has no explicit version define. File is ~31K lines consistent with v4.12.x from the Immediate-Mode-UI/Nuklear repo.
- **miniaudio**: `MA_VERSION_MAJOR/MINOR/REVISION` in `miniaudio.h` = 0.11.25
- **TinySoundFont**: Header comment says "TinySoundFont - v0.9" in `tsf.h`
- **SDL2**: Directory name `SDL2-2.30.12`
- **shine**: No version string found. Vendored from the libshine/shine MP3 encoder project.
- **CPLUG**: No version define. LICENSE copyright 2024, from github.com/Tremus/CPLUG.
- **glad**: Generated header says "glad 2.0.8" with generation date 2026-03-11.

## CVE Review Summary

| Dependency | CVE Search Notes |
|---|---|
| cJSON 1.7.19 | Latest stable as of audit date. No known unpatched CVEs. |
| dr_libs (wav/mp3/flac) | Single-header libraries by David Reid. No CVE database entries found for these versions. |
| Nuklear | Immediate-mode GUI, no network surface. No known CVEs. |
| miniaudio 0.11.25 | No known CVEs for this version. |
| TinySoundFont 0.9 | No known CVEs. Parses SoundFont files; ensure trusted input. |
| SDL2 2.30.12 | Check SDL security advisories. 2.30.x is a recent release. No critical CVEs known for 2.30.12. |
| shine | Legacy MP3 encoder. No CVE database entries. LGPL license requires attention for binary distribution. |
| CPLUG | Plugin framework. No CVE database entries. |
| glad 2.0.8 | Generated OpenGL loader. No security surface. |

## License Compatibility

All dependencies use permissive licenses (MIT, Public Domain, MIT-0, zlib) compatible with most project licenses, with one exception:

- **shine**: LGPL-2.0. If distributing closed-source binaries that include shine, it must be linked as a shared library or the project source must be made available. Since shine is compiled directly into the binary, this needs attention if the project is distributed under a proprietary license.

## Update Policy

- Check for updates quarterly (next check: 2026-06-12)
- Prioritize security patches over feature updates
- Test all build targets (`sequencer_standalone`, `sequencer_gui`, `engine_render_test`, `export_test`, `project_test`, `fm_synth_test`) after updating any dependency
- For single-header libraries (dr_libs, nuklear, miniaudio, tsf), update by replacing the header file
- For SDL2, update the vendored directory and verify CMake integration
- For shine, check upstream libshine for any fixes
- Document version changes in this file when updating
