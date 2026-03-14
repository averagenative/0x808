# Code Signing for 0x808

## Problem

Windows 11 SmartScreen blocks unsigned executables with "Windows protected your PC" warnings. Users must click "More info" > "Run anyway" to proceed. This hurts trust and adoption.

## Solution: SignPath.io (Free for Open Source)

[SignPath Foundation](https://signpath.org/) provides **free EV code signing certificates** for open-source projects. EV certificates give **immediate SmartScreen trust** — no warnings, no reputation building period.

### Requirements (0x808 qualifies)

- [x] OSI-approved open-source license (MIT)
- [x] No proprietary/closed-source components
- [x] Actively maintained with published releases
- [x] Source code publicly available on GitHub
- [x] Builds reproducible from source (CMake + NSIS scripts in repo)

### How It Works

1. **Apply**: Submit the project at [signpath.org](https://signpath.org/)
2. **Verification**: SignPath reviews the GitHub repo to confirm it's legitimate OSS
3. **Certificate**: They provision an EV code signing certificate stored on their HSM (you never handle the private key)
4. **Signing**: For each release, submit the binary — SignPath verifies it matches a build from your source code, then signs it
5. **Distribution**: Signed binaries are trusted by Windows SmartScreen immediately

### Application Steps

1. Visit https://signpath.org/
2. Click "Apply" or contact them via the open source community page
3. Provide:
   - GitHub repo URL: `https://github.com/averagenative/0x808`
   - License: MIT
   - Description: Drum machine & synth workstation
   - Current release: v1.0.0
4. Wait for approval (typically a few days)
5. Integrate signing into the release workflow

### What Gets Signed

- `0x808-{version}-windows-x64-setup.exe` (NSIS installer)
- `0x808.exe` (standalone binary inside the installer/zip)
- `0x808.dll` (VST3 plugin)
- `0x808.clap` (CLAP plugin)

### References

- [SignPath Foundation](https://signpath.org/)
- [SignPath Open Source Community](https://signpath.io/solutions/open-source-community)
- [SignPath Foundation Terms](https://signpath.org/terms.html)
- [SignPath Knowledge Base](https://signpath.io/knowledge-base/introduction)

## Alternative Options

| Option | Cost | SmartScreen Trust |
|--------|------|-------------------|
| **SignPath.io (OSS)** | Free | Immediate (EV cert) |
| OV Certificate (Sectigo, etc.) | ~$200-400/year | Builds over time with downloads |
| EV Certificate (purchased) | ~$350-500/year + hardware token | Immediate |
| Self-signed | Free | None (worse than unsigned) |
| No signing | Free | Warning on first run |

## Current Status

- [ ] Apply to SignPath Foundation
- [ ] Receive approval and certificate
- [ ] Sign v1.0.0 installer and binaries
- [ ] Update release with signed artifacts
- [ ] Document signing in release packaging script
