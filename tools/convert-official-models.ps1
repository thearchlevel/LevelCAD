param([string]$Root, [string]$Cli, [int]$Worker=0, [int]$Workers=1)
$ErrorActionPreference='Stop'
$models=Join-Path $Root 'kicad-packages3d'
$cache=Join-Path $Root 'mesh-cache'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$enc=New-Object System.Text.UTF8Encoding($false)
$failures=@()
$done=0
$categoryIndex=0
foreach($directory in Get-ChildItem -LiteralPath $models -Directory) {
 if(($categoryIndex++ % $Workers) -ne $Worker){continue}
 $files=@(Get-ChildItem -LiteralPath $directory.FullName -Filter '*.step')
 if(!$files.Count){continue}
 $destination=Join-Path $cache $directory.Name
 New-Item -ItemType Directory -Path $destination -Force | Out-Null
 for($start=0;$start -lt $files.Count;$start+=30) {
  $batch=@($files | Select-Object -Skip $start -First 30 | Where-Object { !(Test-Path -LiteralPath (Join-Path $destination ($_.BaseName+'.wrl'))) })
  if(!$batch.Count){continue}
  $text='(kicad_pcb (version 20240108) (generator "pcbnew") (general (thickness 1.6)) (paper "A4") (layers (0 "F.Cu" signal) (31 "B.Cu" signal) (44 "Edge.Cuts" user))'
  foreach($file in $batch) {
   $path=$file.FullName.Replace('\','/')
   $text+='(footprint "model" (layer "F.Cu") (at 0 0) (model "'+$path+'" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0))))'
  }
  $text+='(gr_rect (start -10 -10) (end 10 10) (stroke (width 0.05) (type default)) (fill none) (layer "Edge.Cuts")))'
  $board=Join-Path $cache ("batch-"+$Worker+".kicad_pcb")
  [IO.File]::WriteAllText($board,$text,$enc)
  & $Cli pcb export vrml --force --units mm --models-dir $destination -o (Join-Path $cache ("batch-"+$Worker+".wrl")) $board 2>&1 | Out-File -LiteralPath (Join-Path $cache ("conversion-"+$Worker+".log")) -Append -Encoding utf8
  foreach($file in $batch) {
   if(Test-Path -LiteralPath (Join-Path $destination ($file.BaseName+'.wrl'))){$done++}
   else {$failures+=$file.FullName}
  }
  [IO.File]::WriteAllText((Join-Path $cache ("progress-"+$Worker+".json")),(@{converted=$done;lastCategory=$directory.Name;finished=$false;failures=$failures}|ConvertTo-Json -Depth 4),$enc)
 }
}
[IO.File]::WriteAllText((Join-Path $cache ("progress-"+$Worker+".json")),(@{converted=$done;finished=$true;failures=$failures}|ConvertTo-Json -Depth 4),$enc)