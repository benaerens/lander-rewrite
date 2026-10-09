# Builds the Windows programs with zig (lander/tools/zig-*/zig.exe or zig on PATH).
#   powershell -ExecutionPolicy Bypass -File build-windows.ps1
# Output: build\lander.exe (the game) and build\lander-viewer.exe (level viewer)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$zig = Get-ChildItem "$root\..\tools", "$root\..\..\tools" -Directory -Filter 'zig-*' -ErrorAction SilentlyContinue |
	Select-Object -First 1 | ForEach-Object { "$($_.FullName)\zig.exe" }
if (-not $zig) { $zig = 'zig' }

$core = @('types', 'render', 'fs', 'texfile', 'png', 'mixer', 'terrain', 'model', 'mission', 'level', 'zones',
	'input', 'discimage', 'pe') | ForEach-Object { "$root\src\$_.c" }
# every .c of the game (new modules are picked up automatically)
$game = @(Get-ChildItem "$root\src\game\*.c", "$root\src\game\missions\*.c", "$root\src\fe\*.c" -ErrorAction SilentlyContinue |
	ForEach-Object { $_.FullName })
New-Item -ItemType Directory -Force "$root\build" | Out-Null
$flags = @('-target', 'x86_64-windows-gnu', '-std=c11', '-O2', '-Wall', '-Wno-unused-parameter')

& $zig cc @flags -o "$root\build\lander-viewer.exe" "$root\src\viewer.c" "$root\src\platform_win32.c" @core `
	-lgdi32 -luser32 -lwinmm -lcomdlg32 -lshell32
if ($LASTEXITCODE -ne 0) { throw "viewer build failed" }
Write-Host "built $root\build\lander-viewer.exe"

& $zig cc @flags "-I$root\src" "-I$root\src\game" -DHUD_WITH_REWRITE_RENDER -o "$root\build\lander.exe" `
	"$root\src\platform_win32.c" @core @game "$root\src\win\lander.rc" -lgdi32 -luser32 -lwinmm -lcomdlg32 -lshell32
if ($LASTEXITCODE -ne 0) { throw "game build failed" }
Write-Host "built $root\build\lander.exe"
