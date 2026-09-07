[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CoreDll,
    [Parameter(Mandatory)][string]$CoreDllX86,
    [Parameter(Mandatory)][string]$OutputDir,
    [string]$Iscc = 'C:\Programs\Inno Setup 7\ISCC.exe',
    [string]$RunId,
    [string]$SourceCommit
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'minus-mod.ps1')
$corePath = (Resolve-Path -LiteralPath $CoreDll).Path
$identity = Get-CoreIdentity $corePath
$x86Path = (Resolve-Path -LiteralPath $CoreDllX86).Path
$x86Identity = Get-CoreIdentity $x86Path 'x86'
if ($x86Identity.Kind -ne 'minus' -or $x86Identity.Version -ne $identity.Version) {
    throw 'Select matching x86 and x64 Minus artifacts from the same release run.'
}
if ($identity.Kind -ne 'minus') { throw 'Select the modern Windows x64 Minus CI artifact.' }
if ($identity.Version -notmatch '^\d+\.\d+\.\d+(?:[+.-][0-9A-Za-z+.-]+)?$') {
    throw "Unexpected DLL version: $($identity.Version)"
}
$outputPath = [IO.Path]::GetFullPath($OutputDir)
$name = "AviSynthMinus_$($identity.Version)_windows_x86-x64-mod.exe"
$exe = Join-Path $outputPath $name
if (Test-Path -LiteralPath $exe) { throw "Refusing to overwrite $exe" }
if (-not (Test-Path -LiteralPath $Iscc)) { throw "Inno Setup compiler not found: $Iscc" }
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
& $Iscc "/DCoreDll=$corePath" "/DCoreDllX86=$x86Path" "/O$outputPath" (Join-Path $PSScriptRoot 'avisynth-minus-mod.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed: $LASTEXITCODE" }
# Installation mode alone does not make Setup a native x64 process. Check the
# emitted executable as well, to catch accidental loss of SetupArchitecture.
$setupBytes = [IO.File]::ReadAllBytes($exe)
$peOffset = [BitConverter]::ToInt32($setupBytes, 0x3c)
if ([BitConverter]::ToUInt32($setupBytes, $peOffset) -ne 0x4550 -or
    [BitConverter]::ToUInt16($setupBytes, $peOffset + 4) -ne 0x8664) {
    throw 'The generated installer is not native AMD64. Do not distribute it.'
}
$hash = Get-Digest $exe
[IO.File]::WriteAllText((Join-Path $outputPath ($name + '.sha256')), "$hash  $name`n")
$sources = @{}
foreach ($file in @('avisynth-minus-mod.iss', 'minus-mod.ps1', 'minus-mod-info.txt', 'build-minus-mod.ps1', '..\gpl.txt')) {
    $sources[$file] = Get-Digest (Join-Path $PSScriptRoot $file)
}
$manifest = [ordered]@{
    Repository = 'HomeOfAviSynthPlusEvolution/AviSynthMinus'
    SourceCommit = $SourceCommit
    ActionsRunId = $RunId
    Artifacts = @('avisynth-windows-vs2026-x64', 'avisynth-windows-vs2026-x86')
    Version = $identity.Version
    NumericVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($corePath).FileMajorPart.ToString() + '.' +
        [Diagnostics.FileVersionInfo]::GetVersionInfo($corePath).FileMinorPart + '.' +
        [Diagnostics.FileVersionInfo]::GetVersionInfo($corePath).FileBuildPart + '.' +
        [Diagnostics.FileVersionInfo]::GetVersionInfo($corePath).FilePrivatePart
    PayloadSHA256 = $identity.Hash
    PayloadX86SHA256 = $x86Identity.Hash
    InstallerSHA256 = $hash
    PackagingSourceSHA256 = $sources
    CompilerVersion = (& $Iscc --version | Out-String).Trim()
    Scope = 'Core-only modern Windows x86+x64 mod on x64 Windows; unsigned; not integration-tested'
}
[IO.File]::WriteAllText((Join-Path $outputPath ($name + '.json')), ($manifest | ConvertTo-Json -Depth 5))
Write-Output $exe
