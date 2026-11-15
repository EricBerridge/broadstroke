# Builds broadstroke.slnx with MSBuild and, with -Test, runs the tests.
#
#   .\build.ps1                          Release
#   .\build.ps1 -Test                    Release, then the tests
#   .\build.ps1 -Configuration Debug     Debug (slow; for debugging)
#   .\build.ps1 -Configuration Profile   Release's code with symbols, for the profiler
#   .\build.ps1 -Rebuild                 from scratch
#
# The programs are written to build\<Configuration>\, the studio window with
# the files of the Windows App SDK to build\<Configuration>\studio\.
param(
  [ValidateSet('Release', 'Debug', 'Profile')]
  [string]$Configuration = 'Release',
  [switch]$Test,
  [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# MSBuild from a developer prompt, or else from the newest Visual Studio.
$msbuild = $null
$found = Get-Command msbuild.exe -ErrorAction SilentlyContinue
if ($found) { $msbuild = $found.Source }
if (-not $msbuild) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild `
      -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
  }
}
if (-not $msbuild) {
  Write-Error 'MSBuild was not found. Install Visual Studio or its Build Tools with the C++ workload.'
  exit 1
}

$solution = Join-Path $root 'broadstroke.slnx'

# The studio window is built on the Windows App SDK, which comes as NuGet
# packages (src\studio\packages.config). They are fetched into packages\ the
# first time; after that this step finds them there and needs no network.
& $msbuild $solution -nologo -v:quiet -t:Restore -p:RestorePackagesConfig=true `
  "-p:Configuration=$Configuration" -p:Platform=x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
& $msbuild $solution -m -nologo -v:minimal "-t:$target" "-p:Configuration=$Configuration" -p:Platform=x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
  & (Join-Path $root "build\$Configuration\broadstroke_tests.exe")
  exit $LASTEXITCODE
}
