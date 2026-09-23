param([Parameter(Mandatory=$true)][string]$DllPath)
$ErrorActionPreference="Stop"
$DllPath=(Resolve-Path $DllPath).Path
$out=Join-Path $PWD "deps\mpv-import"
New-Item -ItemType Directory -Force $out|Out-Null
$dump=& dumpbin /nologo /exports "$DllPath"
if($LASTEXITCODE -ne 0){throw "dumpbin failed"}
$exports=New-Object System.Collections.Generic.List[string]
foreach($line in $dump){if($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)\s*$'){$name=$Matches[1];if($name -and $name -notmatch '='){$exports.Add($name)}}}
if($exports.Count -eq 0){throw "No DLL exports detected."}
$def=Join-Path $out "mpv.def"
@("LIBRARY `"libmpv-2.dll`"","EXPORTS")+$exports|Set-Content -Encoding ascii $def
& lib /nologo /def:"$def" /machine:x64 /out:"$out\mpv.lib"
if($LASTEXITCODE -ne 0){throw "lib.exe failed"}
