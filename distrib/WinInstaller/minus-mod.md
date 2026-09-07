# AviSynthMinus x86 + x64 mod installer

`avisynth-minus-mod.iss` is a new core-only overlay, not a renamed upstream
installer. The old `avisynth+.iss` and ARM64 installer remain untouched.

## Build locally

Requires Inno Setup 7 and **modern Windows x86 and x64 CI artifacts from the same run**.
Select an explicit successful Actions run, verify its commit and download
`avisynth-windows-vs2026-x64` and `avisynth-windows-vs2026-x86`. A branch/dev binary stays a dev package; tagging
later does not change embedded metadata in an earlier artifact.

```powershell
gh run view RUN_ID --repo HomeOfAviSynthPlusEvolution/AviSynthMinus --json headSha,status,conclusion,jobs
gh run download RUN_ID --repo HomeOfAviSynthPlusEvolution/AviSynthMinus --name avisynth-windows-vs2026-x64 --dir build/packages/SELECTED_RUN/download/x64
gh run download RUN_ID --repo HomeOfAviSynthPlusEvolution/AviSynthMinus --name avisynth-windows-vs2026-x86 --dir build/packages/SELECTED_RUN/download/x86
./distrib/WinInstaller/build-minus-mod.ps1 -CoreDll PATH_TO_X64_DLL -CoreDllX86 PATH_TO_X86_DLL -OutputDir build/packages/SELECTED_RUN/output -RunId RUN_ID -SourceCommit VERIFIED_COMMIT_SHA
```

The build wrapper validates the PE architecture and Minus identity, uses the
DLL's version resources, refuses to overwrite an existing EXE, and emits a
SHA-256 sidecar and a provenance manifest with the supplied run ID/commit,
artifact name/hash and packaging-source hashes. Verify the supplied run ID and
commit before building; the wrapper does not query GitHub. Do not distribute the compiler smoke
test package or mix XP/ARM64 artifacts into this installer.

Setup itself uses `SetupArchitecture=x64`, not just x64 installation mode.
All helper calls (including uninstall) use `ExecWithNativeSysDir` to launch
native Windows PowerShell. The wrapper also checks the generated EXE's AMD64
PE machine type to guard against accidentally reverting to a 32-bit installer.

## Scope and policy

- Native x64 Windows 10+, official AviSynth+ 3.7.5 with x86 and/or x64 installed.
  Update both installed components, or only the single installed component;
  do not add missing official components. No 32-bit Windows, XP or ARM64 installer support.
  Presence requires the official uninstall entry, installed uninstaller,
  matching architecture-specific AviSynth registry location, and a recognized core. File
  metadata identifies a compatible core; it is **not proof of publisher authenticity**.
- Fixed management directory: `%ProgramFiles%\AviSynthMinus Mod`. Separate
  AppId/uninstall entry, unchanged from the original x64-only mod installer.
  Replace only `%SystemRoot%\System32\AviSynth.dll` (x64) and
  `%SystemRoot%\SysWOW64\AviSynth.dll` (x86), when their components are installed.
  Preserve the original x64 `state.json` and backups in the management root;
  x86 has its own `x86/state.json`, journal and backups. No migration of x64 state is needed.
- No plugins, SDK, scripts, associations, upstream registry edits or runtime
  installers. Check imports and the required VC runtime for the selected build.
- An official core is backed up byte-for-byte. A manually installed Minus core
  requires interactive confirmation and is backed up as Minus. This includes
  the earlier `[3.7.5]` Minus version-string format. Silent manual takeover fails.
- A managed upgrade preserves the initial baseline; reinstall after removal
  starts a new baseline. External replacement while active blocks an upgrade.
- Removal restores only while the official base path and installed core hash
  still match, and the backup hash is valid. An externally changed/deleted core
  or missing base is left untouched. A damaged required backup blocks removal.
- Backup DLLs, README and state are deliberately not owned by Inno's uninstall
  log and remain after removal. There is no recursive cleanup. The system DLL
  is never an Inno `[Files]` entry, so uninstall cannot delete the restored DLL.

## Failure handling

The helper stages a copy alongside the system DLL, checks hashes, records
`pending.json`, and uses `File.Replace` with a retained previous DLL. A failed
manifest commit rolls back the DLL if it is still our replacement. A crash or
uncertain rollback leaves the journal and blocks further automatic changes.
Do not just delete the journal: compare its before/after hashes, retained DLLs
and state first. There is no automatic crash-recovery UI in this initial version.

Both selected components are preflighted before replacement, including consent
for any manual Minus baseline. Each architecture commits independently; this is
not a single atomic transaction spanning both DLLs. If the second write fails,
the completed first component remains installed and tracked, and Setup reports
failure with the completed-component list. Retry preserves its initial backup.
Uninstall checks both restore plans first and aborts management-file removal on
failure; retry safely skips already restored components. One externally changed
component can be left untouched while the other is restored.

Setup installs management files first, then applies the core transaction. If
that transaction fails, Setup explicitly shows failure and returns exit code 1;
management files/uninstall entry are retained for retry or recovery, rather than
claiming a successful core installation. Uninstall aborts before deleting its
management files if restoration fails. Close all AviSynth consumers first;
no forced process termination, reboot replacement or deferred restore is used.

An official reinstall at exactly the same location with exactly identical
core bytes cannot be distinguished from the original installation. Remove
the mod before upgrading/uninstalling official AviSynth+.

## Verification

Run tests in Windows PowerShell 5.1 (the same host used by Setup):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File distrib/WinInstaller/test-minus-mod.ps1 -OfficialDll PATH_TO_OFFICIAL_X64_DLL -MinusDll PATH_TO_MINUS_X64_DLL -MinusDllX86 PATH_TO_MINUS_X86_DLL
```

Tests use uniquely named temporary directories and mock base detection; no
registry writes, DLL loading or real system-core changes occur. They cover
install/upgrade/reinstall, both backup kinds, restoration, missing base,
external replacement, corrupt backup, interrupted transaction, locked core
and injected manifest-write failure. Dual tests cover mixed official/manual
baselines, x86-only/x64-only detection, legacy x64 upgrade, preflight failures,
second-component failure, retry, and independent restoration. Fixtures are retained for inspection.

Before public distribution, test the compiled installer in a disposable Windows
VM: no official install, x86-only install, official x64 install, manual Minus
takeover, managed upgrade, core in use, tampered backup, external replacement,
official removal, normal/silent uninstall. Filesystem tests and successful
compilation do **not** verify Inno UI, registry discovery, UAC, actual DLL loader
compatibility or install/uninstall integration. The initial package is unsigned.
