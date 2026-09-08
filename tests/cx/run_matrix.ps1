param(
  [string]$MsvcBuild = "$PSScriptRoot/../../build/cx-sdk",
  [string]$GccBuild = "$PSScriptRoot/../../build/cx-sdk-gcc",
  [Parameter(Mandatory = $true)]
  [string]$GccRuntime
)
$ErrorActionPreference = 'Stop'
$msvc = (Resolve-Path "$MsvcBuild/tests/cx/Release").Path
$gcc = (Resolve-Path "$GccBuild/tests/cx").Path
$keys = @('PATH', 'AVS_CX_SMOKE_DUAL_PATH', 'AVS_CX_SDK_PATH', 'AVS_CX_C_PATH', 'AVS_CX_STACKED_PATH', 'AVS_CX_OTHER_DUAL_PATH', 'AVS_CX_NESTED_PATH')
$saved = @{}
foreach ($key in $keys) { $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
try {
  $env:PATH = "$GccRuntime;$env:PATH"
  foreach ($core in @($msvc, $gcc)) {
    $other = if ($core -eq $msvc) { $gcc } else { $msvc }
    $env:AVS_CX_OTHER_DUAL_PATH = "$other/cx_smoke_dual.dll"
    foreach ($plugin in @($msvc, $gcc)) {
      Write-Host "Core: $core; plugins: $plugin"
      $env:AVS_CX_SMOKE_DUAL_PATH = "$plugin/cx_smoke_dual.dll"
      $env:AVS_CX_SDK_PATH = $env:AVS_CX_SMOKE_DUAL_PATH
      $env:AVS_CX_C_PATH = "$plugin/cx_smoke_c.dll"
      $env:AVS_CX_STACKED_PATH = "$plugin/cx_convert_stacked.dll"
      $env:AVS_CX_NESTED_PATH = "$plugin/cx_smoke_nested.dll"
      $buildDir = if ($core -eq $msvc) { $MsvcBuild } else { $GccBuild }
      & ctest --test-dir $buildDir -C Release --output-on-failure -R '^cx\.'
      if ($LASTEXITCODE -ne 0) { throw "CX matrix failed: core=$core plugin=$plugin" }
    }
  }
} finally {
  foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
}
