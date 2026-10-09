# Makes the players' download from build\lander.exe (run build-windows.ps1 first):
#   build\package\Lander rewrite\        Lander.exe, How to play.txt, licences and the disc folder
#   build\lander-rewrite-windows-x64.zip  that folder zipped (what a release offers)
#   powershell -ExecutionPolicy Bypass -File tools\package-windows.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$pkg = Join-Path $root 'build\package'
$out = Join-Path $pkg 'Lander rewrite'
$zip = Join-Path $root 'build\lander-rewrite-windows-x64.zip'
$disc = Join-Path $out 'Put your Lander disc image here'   # main.c DISC_FOLDER

if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg }
New-Item -ItemType Directory -Force $disc | Out-Null
Copy-Item (Join-Path $root 'build\lander.exe') (Join-Path $out 'Lander.exe')
Copy-Item (Join-Path $root 'dist\How to play.txt') $out
Copy-Item (Join-Path $root 'dist\disc-folder.txt') (Join-Path $disc 'Put your Lander disc image here.txt')
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $out 'LICENSE.txt')
Copy-Item (Join-Path $root 'THIRD_PARTY_NOTICES.md') (Join-Path $out 'THIRD_PARTY_NOTICES.txt')

if (Test-Path $zip) { Remove-Item -Force $zip }
# entry names with '/' (Compress-Archive of Windows PowerShell writes '\', which other unzippers mangle)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::Open($zip, 'Create')
Get-ChildItem $out -Recurse -File | ForEach-Object {
	$name = 'Lander rewrite/' + $_.FullName.Substring($out.Length + 1).Replace('\', '/')
	[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, $_.FullName, $name, 'Optimal') | Out-Null
}
$z.Dispose()
Write-Host "packaged $out"
Write-Host "zipped   $zip"
