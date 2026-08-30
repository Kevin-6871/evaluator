# Build-And-Pack.ps1
# PowerShell script to build QT project and pack dependencies into rele/ using objdump.

# -------------------- 0️⃣ Configuration --------------------
$QtToolchain = "C:\Qt\Tools\llvm-mingw1706_64\bin"
$QtBase      = "C:\Qt\6.11.2\llvm-mingw_64\bin"
$QtPlugins   = "C:\Qt\6.11.2\llvm-mingw_64\plugins"
$Env:PATH    = "$QtToolchain;$QtBase;$Env:PATH"

# Use llvm-objdump.exe from toolchain
$objdump = "$QtToolchain\llvm-objdump.exe"
if (-not (Test-Path $objdump)) {
    $objdump = "$QtToolchain\objdump"
    if (-not (Test-Path $objdump)) {
        throw "objdump not found in $QtToolchain"
    }
}

# -------------------- 1️⃣ Clean previous builds --------------------
Write-Host "Cleaning previous build and rele folders..."
if (Test-Path "build_release") { Remove-Item -Recurse -Force "build_release" }
if (Test-Path "rele") { Remove-Item -Recurse -Force "rele" }

# -------------------- 2️⃣ Build release --------------------
$BuildDir = "build_release"
New-Item -ItemType Directory -Path $BuildDir | Out-Null
Push-Location $BuildDir

Write-Host "Running qmake..."
&qmake ..\QT.pro -spec win32-clang-g++ "CONFIG+=release" "CONFIG+=qtquickcompiler"
if ($LASTEXITCODE -ne 0) { throw "qmake failed" }

Write-Host "Building with mingw32-make..."
& mingw32-make -j4
if ($LASTEXITCODE -ne 0) { throw "mingw32-make failed" }

$ExePath = Join-Path (Get-Location) "release\QT.exe"
if (-not (Test-Path $ExePath)) { throw "Executable not found at $ExePath" }
Write-Host "Build succeeded: $ExePath"
Pop-Location

# -------------------- 3️⃣ Prepare rele folder and copy exe --------------------
$ReleaseDir = "rele"
New-Item -ItemType Directory -Path $ReleaseDir | Out-Null
Copy-Item -Path $ExePath -Destination $ReleaseDir -Force
Set-Location $ReleaseDir
Write-Host "Copied QT.exe to $ReleaseDir"

# -------------------- 4️⃣ Extract DLL dependencies via objdump --------------------
Write-Host "Extracting DLL dependencies from QT.exe..."
if (-not (Test-Path $objdump)) { throw "objdump not found at $objdump" }

$imports = & $objdump -p QT.exe | Select-String -Pattern "DLL Name"
$dllNames = $imports | ForEach-Object {
    if ($_.Line -match "DLL Name:\s+(\S+)") { $matches[1] }
} | Where-Object { $_ }
Write-Host "Detected DLLs:"
$dllNames

# -------------------- 5️⃣ Define system DLLs (Windows provided) --------------------
$systemDlls = @(
    "KERNEL32.dll","USER32.dll","GDI32.dll","ADVAPI32.dll","SHELL32.dll",
    "WS2_32.dll","WINMM.dll","PSAPI.dll","DWMAPI.dll",
    "VERSION.dll","SECUR32.dll","RPCRT4.dll","OLE32.dll","OLEAUT32.dll",
    "IMM32.dll","WINHTTP.dll","URLMON.dll","NETAPI32.dll","IPHLPAPI.dll",
    "USERENV.dll","SETUPAPI.dll","CFGMGR32.dll","DEVOBJ.dll",
    "UxTheme.dll","DWrite.dll","DXGI.dll","D3D11.dll","D3D12.dll"
)
# Add api-ms-win-crt-* DLLs (provided by Windows)
$crtDlls = Get-ChildItem -Path "$env:WINDIR\System32" -Filter "api-ms-win-crt-*.dll" -ErrorAction SilentlyContinue | Select-Object -Expand Name
$systemDlls += $crtDlls

# -------------------- 6️⃣ Determine which DLLs we need to copy (exclude system and api-ms-win-crt-*) --------------------
$neededDlls = $dllNames | Where-Object {
    ($_ -notin $systemDlls) -and (-not ($_ -like "api-ms-win-crt-*.dll"))
}
Write-Host "`nNon-system DLLs that need to be copied:"
$neededDlls

# -------------------- 7️⃣ Copy needed DLLs from Qt base or toolchain --------------------
Write-Host "`nCopying required DLLs..."
foreach ($dll in $neededDlls) {
    $srcQt   = Join-Path $QtBase $dll
    $srcTool = Join-Path $QtToolchain $dll
    if (Test-Path $srcQt) {
        Copy-Item -Path $srcQt -Destination . -Force
        Write-Host "[COPY] $dll from Qt base"
    } elseif (Test-Path $srcTool) {
        Copy-Item -Path $srcTool -Destination . -Force
        Write-Host "[COPY] $dll from toolchain"
    } else {
        Write-Warning "Could not find $dll in Qt base or toolchain"
    }
}

# -------------------- 8️⃣ Copy required Qt plugins (preserve subfolders) --------------------
Write-Host "`nCopying Qt plugins..."
# Define plugin mappings
$plugins = @(
    @{ Folder = "platforms"; File = "qwindows.dll" },
    @{ Folder = "generic"; File = "qtuiotouchplugin.dll" },
    @{ Folder = "iconengines"; File = "qsvgicon.dll" },
    @{ Folder = "imageformats"; File = "qgif.dll" },
    @{ Folder = "imageformats"; File = "qico.dll" },
    @{ Folder = "imageformats"; File = "qjpeg.dll" },
    @{ Folder = "imageformats"; File = "qsvg.dll" },
    @{ Folder = "styles"; File = "qmodernwindowsstyle.dll" }
)
foreach ($p in $plugins) {
    $folder = $p.Folder
    $file   = $p.File
    $destFolder = Join-Path (Get-Location) $folder
    if (-not (Test-Path $destFolder)) { New-Item -ItemType Directory -Path $destFolder | Out-Null }
    $srcFile = Join-Path $QtPlugins $folder $file
    if (Test-Path $srcFile) {
        Copy-Item -Path $srcFile -Destination $destFolder -Force
        Write-Host "[COPY] $file to $folder"
    } else {
        Write-Warning "Plugin not found: $srcFile"
    }
}

# -------------------- 9️⃣ Verify dependencies again --------------------
Write-Host "`nVerifying final dependencies..."
$finalImports = & $objdump -p QT.exe | Select-String -Pattern "DLL Name"
$finalDlls = $finalImports | ForEach-Object {
    if ($_.Line -match "DLL Name:\s+(\S+)") { $matches[1] }
} | Where-Object { $_ }
Write-Host "All DLLs imported by QT.exe:"
$finalDlls

$unexpected = $finalDlls | Where-Object { $_ -notin $systemDlls -and $_ -notin $neededDlls -and -not ($_ -like "api-ms-win-crt-*.dll") }
if ($unexpected) {
    Write-Warning "Unexpected non-system DLLs still present: $unexpected"
} else {
    Write-Host "All non-system DLLs are accounted for."
}

# -------------------- 10️⃣ Run self-test (headless) --------------------
Write-Host "`nRunning self-test (--selftest)..."
$Env:QT_PLUGIN_PATH = "."
$testResult = & .\QT.exe --selftest ..\testcases 2>&1
$exitCode = $LASTEXITCODE
if (Test-Path "selftest_result.txt") {
    Write-Host "Self-test completed. Result:"
    Get-Content "selftest_result.txt"
} else {
    Write-Warning "No selftest_result.txt generated."
}
Write-Host "Exit code: $exitCode"

# -------------------- 11️⃣ Final summary --------------------
Write-Host "`n=== PACKAGING COMPLETE ==="
Write-Host "Release folder: $(Get-Location)"
Write-Host "Contents:"
Get-ChildItem -Recurse | Sort-Object
