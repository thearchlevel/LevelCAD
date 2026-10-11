$ErrorActionPreference='Stop'
$root=Join-Path (Split-Path -Parent $PSScriptRoot) 'libraries/official'
$cache=Join-Path $root 'symbol-cache'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$enc=New-Object System.Text.UTF8Encoding($false)
foreach($category in Get-ChildItem -LiteralPath (Join-Path $root 'kicad-symbols') -Directory -Filter '*.kicad_symdir'){
 $combined=New-Object System.Text.StringBuilder
 [void]$combined.Append('(kicad_symbol_lib ')
 foreach($file in Get-ChildItem -LiteralPath $category.FullName -File -Filter '*.kicad_sym'){
  $text=[IO.File]::ReadAllText($file.FullName)
  $begin=$text.IndexOf('(symbol ')
  $end=$text.LastIndexOf(')')
  if($begin -ge 0 -and $end -gt $begin){[void]$combined.Append($text.Substring($begin,$end-$begin))}
 }
 [void]$combined.Append(')')
 $name=$category.Name.Replace('.kicad_symdir','')+'.kicad_sym'
 $path=Join-Path $cache $name
 [IO.File]::WriteAllText($path,$combined.ToString(),$enc)
}