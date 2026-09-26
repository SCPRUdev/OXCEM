param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ tools not found.' }
$vc = (Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion = (Get-ChildItem (Join-Path $sdk 'Lib') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$output = Join-Path $repo 'obj/sprite-overlay-tests/named-actions'
New-Item -ItemType Directory -Path $output -Force | Out-Null
Push-Location $repo
try {
    if (!$SkipBuild) {
        & (Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe') src/OpenXcom.2010.vcxproj /m /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 /v:minimal
        if ($LASTEXITCODE) { throw 'Engine build failed.' }
    }
    $includes = @("$vc/include", "$sdk/Include/$sdkVersion/ucrt", "$sdk/Include/$sdkVersion/shared", "$sdk/Include/$sdkVersion/um", "$repo/deps_vs/include", "$repo/deps_vs/include/SDL", "$repo/libs/rapidyaml")
    $compile = @('/nologo', '/std:c++17', '/EHsc', '/MD', '/O2', '/utf-8', '/DWIN32', '/D_CONSOLE', '/D_CRT_SECURE_NO_DEPRECATE', '/c', "$PSScriptRoot/named-actions.cpp", "/Fo$output/named-actions.obj")
    foreach ($include in $includes) { $compile += "/I$include" }
    & "$vc/bin/Hostx64/x86/cl.exe" @compile
    if ($LASTEXITCODE) { throw 'Test compilation failed.' }
    $objects = Get-ChildItem "$repo/obj/Win32/Release" -Recurse -Filter '*.obj' | Where-Object { $_.Name -ne 'main.obj' }
    $link = @('/NOLOGO', '/SUBSYSTEM:CONSOLE', '/MACHINE:X86', '/LTCG', "/OUT:`"$output/named-actions.exe`"", "`"$output/named-actions.obj`"")
    foreach ($object in $objects) { $link += "`"$($object.FullName)`"" }
    foreach ($lib in @("$vc/lib/x86", "$sdk/Lib/$sdkVersion/ucrt/x86", "$sdk/Lib/$sdkVersion/um/x86", "$repo/deps_vs/lib/Win32")) { $link += "/LIBPATH:`"$lib`"" }
    $link += @('SDL_image.lib', 'SDL_mixer.lib', 'SDL_gfx.lib', 'SDL.lib', 'opengl32.lib', 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'winspool.lib', 'comdlg32.lib', 'advapi32.lib', 'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'odbc32.lib', 'odbccp32.lib')
    [IO.File]::WriteAllLines("$output/link.rsp", $link)
    & "$vc/bin/Hostx64/x86/link.exe" "@$output/link.rsp"
    if ($LASTEXITCODE) { throw 'Test linking failed.' }
    $oldPath = $env:PATH
    try {
        $env:PATH = "$repo/deps_vs/lib/Win32;$env:PATH"
        Remove-Item -LiteralPath "$output/API.log" -ErrorAction SilentlyContinue
        & "$output/named-actions.exe" "$output/API.log"
        if ($LASTEXITCODE) { throw "Regression tests failed: $LASTEXITCODE" }
    } finally { $env:PATH = $oldPath }
} finally { Pop-Location }
