param([switch]$Convert, [int]$Workers=4)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$root=Join-Path $project 'libraries/official'
New-Item -ItemType Directory -Path $root -Force | Out-Null
foreach($name in @('kicad-symbols','kicad-footprints','kicad-packages3d','kicad-templates')) {
    $destination=Join-Path $root $name
    if(!(Test-Path -LiteralPath (Join-Path $destination '.git'))) {
        & git clone --depth 1 --single-branch "https://gitlab.com/kicad/libraries/$name.git" $destination
        if($LASTEXITCODE -ne 0){throw "Download failed: $name"}
    }
    & git -C $destination rev-parse HEAD
    if($LASTEXITCODE -ne 0){throw "Invalid repository: $name"}
}
& (Join-Path $PSScriptRoot "build-symbol-cache.ps1")
if($Convert) {
    $cli=Join-Path $env:ProgramFiles 'KiCad/10.0/bin/kicad-cli.exe'
    if(!(Test-Path -LiteralPath $cli)){throw 'Set the installed CLI path in this script before conversion.'}
    $script=Join-Path $PSScriptRoot 'convert-official-models.ps1'
    for($worker=0;$worker -lt $Workers;$worker++) {
        Start-Process powershell -WindowStyle Hidden -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$script`" -Root `"$root`" -Cli `"$cli`" -Worker $worker -Workers $Workers"
    }
}