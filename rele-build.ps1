# rele-build.ps1
# 用于使用 windeployqt 构建和打包 Qt 应用程序的 PowerShell 脚本。
# 适用于 PowerShell 5.1+ 和 7.x。
# 支持 MSVC 和 MinGW 工具链。
# 接受显式的 QtToolchainDir 和 BuildToolsRoot 参数。

[CmdletBinding()]
param(
    [string]$QtToolchainDir,      # 例如：C:\Qt\6.11.2\msvc2022_64
    [string]$BuildToolsRoot,      # 例如：C:\BuildTools（用于 MSVC）
    [switch]$VerboseLog           # 开启详细日志（显示外部命令输出）
)

# 进度控制函数（无装饰符号，使用方括号标签）
function Write-Info {
    param([string]$Message, [string]$Color = "Cyan")
    Write-Host "[信息] $Message" -ForegroundColor $Color
}

function Write-Success {
    param([string]$Message)
    Write-Host "[成功] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[警告] $Message" -ForegroundColor Yellow
}

function Write-ErrorMsg {
    param([string]$Message)
    Write-Host "[错误] $Message" -ForegroundColor Red
}

# 静默执行外部命令，但保留错误
function Invoke-Silent {
    param(
        [scriptblock]$ScriptBlock,
        [string]$ErrorMessage = "命令执行失败"
    )
    if ($VerboseLog) {
        & $ScriptBlock
        if ($LASTEXITCODE -ne 0) {
            Write-ErrorMsg "$ErrorMessage (退出代码: $LASTEXITCODE)"
            exit $LASTEXITCODE
        }
    } else {
        try {
            & $ScriptBlock 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Write-ErrorMsg "$ErrorMessage (退出代码: $LASTEXITCODE)"
                exit $LASTEXITCODE
            }
        } catch {
            Write-ErrorMsg "$ErrorMessage : $_"
            exit 1
        }
    }
}

Write-Info "PowerShell 版本: $($PSVersionTable.PSVersion.ToString())"

# ==========================================================
# 第0->1步：定位 Qt 工具链目录
# ==========================================================

Set-Location -Path "C:\Users\shaog\Desktop\mycpp\QT"

function Find-QtToolchainDir {
    if ($QtToolchainDir) {
        Write-Info "使用参数指定的 Qt 工具链目录: $QtToolchainDir" -Color "Green"
        if (Test-Path "$QtToolchainDir\bin\qmake.exe") {
            return $QtToolchainDir
        } else {
            Write-Warn "指定的工具链目录中未找到 qmake.exe"
        }
    }

    if ($env:QT_TOOLCHAIN_DIR) {
        Write-Info "使用环境变量 QT_TOOLCHAIN_DIR: $env:QT_TOOLCHAIN_DIR" -Color "Green"
        if (Test-Path "$env:QT_TOOLCHAIN_DIR\bin\qmake.exe") {
            return $env:QT_TOOLCHAIN_DIR
        } else {
            Write-Warn "QT_TOOLCHAIN_DIR 中未找到 qmake.exe"
        }
    }

    # 自动检测：扫描常见 Qt 安装路径
    $commonBases = @("C:\Qt", "D:\Qt", "$env:ProgramFiles\Qt", "$env:ProgramFiles(x86)\Qt")
    foreach ($base in $commonBases) {
        if (-not (Test-Path $base)) { continue }
        $versionDirs = Get-ChildItem -Path $base -Directory -ErrorAction SilentlyContinue |
                       Where-Object { $_.Name -match '^\d+\.\d+' } |
                       Sort-Object Name -Descending
        foreach ($verDir in $versionDirs) {
            $tcDirs = Get-ChildItem -Path $verDir.FullName -Directory -ErrorAction SilentlyContinue |
                      Where-Object { $_ -is [System.IO.DirectoryInfo] } |
                      Sort-Object {
                          if ($_.Name -match '^msvc') { 0 } else { 1 }
                      }
            foreach ($tcDir in $tcDirs) {
                if (Test-Path "$($tcDir.FullName)\bin\qmake.exe") {
                    Write-Info "自动检测到 Qt 工具链: $($tcDir.FullName)" -Color "Green"
                    return $tcDir.FullName
                }
            }
        }
    }

    # 最后尝试从 PATH 中推断
    $qmakeCmd = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if ($qmakeCmd) {
        $qmakePath = $qmakeCmd.Source
        $inferredDir = Split-Path (Split-Path $qmakePath -Parent) -Parent
        if (Test-Path "$inferredDir\bin\qmake.exe") {
            Write-Info "从 PATH 推断出 Qt 工具链: $inferredDir" -Color "Green"
            return $inferredDir
        }
    }

    Write-ErrorMsg "无法定位 Qt 工具链目录，请通过 -QtToolchainDir 参数指定。"
    exit 1
}

$ToolchainDir = Find-QtToolchainDir
if (-not $ToolchainDir) { exit 1 }

# 从工具链目录推导所有其他 Qt 路径（内部使用）
$QtBinDir = Join-Path $ToolchainDir "bin"
$QtPluginsDir = Join-Path $ToolchainDir "plugins"
$QtVersionDir = Split-Path $ToolchainDir -Parent
$QtRootDir = Split-Path $QtVersionDir -Parent

# 验证推导的路径
if (-not (Test-Path $QtBinDir)) {
    Write-ErrorMsg "未找到 Qt bin 目录: $QtBinDir"
    exit 1
}
if (-not (Test-Path $QtPluginsDir)) {
    Write-ErrorMsg "未找到 Qt plugins 目录: $QtPluginsDir"
    exit 1
}
if (-not (Test-Path $QtVersionDir)) {
    Write-ErrorMsg "未找到 Qt 版本目录: $QtVersionDir"
    exit 1
}
if (-not (Test-Path $QtRootDir)) {
    Write-ErrorMsg "未找到 Qt 根目录: $QtRootDir"
    exit 1
}

# 确定工具链类型
$toolchainName = (Split-Path $ToolchainDir -Leaf)
$isMingwToolchain = $toolchainName -match "mingw|llvm"

# ==========================================================
# 第0->2步：初始化 MSVC 构建环境（非 MinGW）
# ==========================================================
if (-not $isMingwToolchain) {
    Write-Info "正在初始化 MSVC 构建环境..."

    if (-not $BuildToolsRoot) {
        $BuildToolsRoot = $env:BUILD_TOOLS_ROOT
        if (-not $BuildToolsRoot) {
            $BuildToolsRoot = "C:\BuildTools"
            Write-Warn "使用默认 BuildTools 目录: $BuildToolsRoot"
        }
    }

    $devcmdPath = Join-Path $BuildToolsRoot "devcmd.ps1"
    if (Test-Path $devcmdPath) {
        Write-Info "正在从 $devcmdPath 加载 MSVC 环境..."
        if ($VerboseLog) {
            & $devcmdPath -InstallPath $BuildToolsRoot
        } else {
            & $devcmdPath -InstallPath $BuildToolsRoot 2>&1 | Out-Null
        }
    } else {
        $vcvarsPath = Join-Path $BuildToolsRoot "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $vcvarsPath) {
            Write-Info "正在从 $vcvarsPath 加载 MSVC 环境..."
            $tempBat = [System.IO.Path]::GetTempFileName() + ".bat"
            @"
@echo off
call "$vcvarsPath"
set
"@ | Out-File -FilePath $tempBat -Encoding ASCII
            $output = cmd /c $tempBat
            Remove-Item $tempBat
            $output -split "`r`n" | ForEach-Object {
                if ($_ -match '^([^=]+)=(.*)$') {
                    [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
                }
            }
        } else {
            Write-ErrorMsg "未找到 devcmd.ps1 或 vcvars64.bat，请确保已安装 MSVC 构建工具。"
            exit 1
        }
    }

    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        Write-ErrorMsg "环境初始化后仍找不到 cl.exe，请检查 MSVC 安装。"
        exit 1
    } else {
        Write-Success "MSVC 环境初始化成功。"
    }
} else {
    # MinGW：定位 MinGW
    Write-Info "检测到 MinGW 工具链，正在定位 MinGW..."
    $MinGWBinDir = $null
    $toolsDir = Join-Path $QtRootDir "Tools"
    if (Test-Path $toolsDir) {
        $mingwDirs = Get-ChildItem -Path $toolsDir -Directory | Where-Object { $_.Name -match "mingw" }
        foreach ($dir in $mingwDirs) {
            $binPath = Join-Path $dir.FullName "bin"
            if (Test-Path "$binPath\mingw32-make.exe") {
                $MinGWBinDir = $binPath
                break
            }
        }
    }
    if (-not $MinGWBinDir) {
        $makeCmd = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue
        if ($makeCmd) { $MinGWBinDir = Split-Path $makeCmd.Source -Parent }
    }
    if ($MinGWBinDir) {
        Write-Success "找到 MinGW: $MinGWBinDir"
    } else {
        Write-Warn "未能定位 MinGW，请确保 mingw32-make 在 PATH 中。"
    }
}

# ==========================================================
# 环境变量设置（将 Qt 和工具链路径添加到 PATH）
# ==========================================================
$pathsToAdd = @()
if (-not ($env:PATH -split ';' -contains $QtBinDir)) { $pathsToAdd += $QtBinDir }
if ($isMingwToolchain -and $MinGWBinDir -and (-not ($env:PATH -split ';' -contains $MinGWBinDir))) {
    $pathsToAdd += $MinGWBinDir
}
if ($pathsToAdd.Count -gt 0) {
    $env:PATH = ($pathsToAdd + $env:PATH -join ';')
    Write-Info "已将工具链目录添加到 PATH" -Color "Cyan"
}

# ==========================================================
# 配置
# ==========================================================
$ProjectFile = "..\QT.pro"
$ExeName = "QT.exe"
$BuildDir = "build_release"
$ReleaseDir = Join-Path (Get-Location) "rele"

# ==========================================================
# 第1步：清理之前的构建和发布目录
# ==========================================================
Write-Info "正在清理之前的构建和发布目录..."
try {
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
    if (Test-Path $ReleaseDir) { Remove-Item -Recurse -Force $ReleaseDir }
} catch {
    Write-Warn "无法完全清理某些目录，它们可能正在使用或受保护。"
}

# ==========================================================
# 第2步：构建应用程序
# ==========================================================
Write-Info "正在创建构建目录..."
if (Test-Path $BuildDir -PathType Leaf) { Remove-Item $BuildDir -Force }
New-Item -ItemType Directory -Path $BuildDir | Out-Null
Push-Location $BuildDir

try {
    Write-Info "正在运行 qmake..."
    Invoke-Silent -ScriptBlock { & $QtBinDir\qmake $ProjectFile "CONFIG+=release" "CONFIG+=qtquickcompiler" } -ErrorMessage "qmake 失败"

    Write-Info "正在构建..."
    if ($isMingwToolchain) {
        Invoke-Silent -ScriptBlock { & mingw32-make -j4 } -ErrorMessage "mingw32-make 失败"
    } else {
        $jomPath = Join-Path $QtBinDir "jom.exe"
        $nmakePath = (Get-Command nmake -ErrorAction SilentlyContinue).Source
        if (Test-Path $jomPath) {
            Write-Info "使用 jom 进行构建..." -Color "Cyan"
            Invoke-Silent -ScriptBlock { & $jomPath /J4 } -ErrorMessage "jom 失败"
        } elseif ($nmakePath) {
            Write-Info "使用 nmake ($nmakePath) 进行构建..." -Color "Cyan"
            Invoke-Silent -ScriptBlock { & $nmakePath } -ErrorMessage "nmake 失败"
        } else {
            Write-ErrorMsg "未找到 jom.exe（Qt bin 中）或 nmake.exe（PATH 中）。"
            exit 1
        }
    }

    $ExePath = Join-Path (Get-Location) "release\$ExeName"
    if (-not (Test-Path $ExePath)) {
        Write-ErrorMsg "未找到可执行文件: $ExePath"
        exit 1
    }
    Write-Success "构建成功: $ExePath"
}
catch {
    Write-ErrorMsg "构建过程中出错: $($_.Exception.Message)"
    Pop-Location
    exit 1
}
finally {
    Pop-Location
}

# ==========================================================
# 第3步：准备发布目录并复制可执行文件
# ==========================================================
Write-Info "正在准备发布目录..."
New-Item -ItemType Directory -Path $ReleaseDir -Force | Out-Null
Copy-Item -Path $ExePath -Destination $ReleaseDir -Force
Write-Success "已复制 '$ExeName' 到 '$ReleaseDir'"

# ==========================================================
# 第4步：使用 windeployqt 直接部署到 rele 目录
# ==========================================================
Write-Info "正在运行 windeployqt 部署依赖..."

$env:QT_PLUGIN_PATH = $QtPluginsDir
$env:QT_QPA_PLATFORM_PLUGIN_PATH = $QtPluginsDir
$env:QT_DEBUG_PLUGINS = 1
Write-Info "已设置 QT_PLUGIN_PATH = $env:QT_PLUGIN_PATH"
Write-Info "已设置 QT_QPA_PLATFORM_PLUGIN_PATH = $env:QT_QPA_PLATFORM_PLUGIN_PATH"

Push-Location $ReleaseDir
try {
    if ($VerboseLog) {
        & "$QtBinDir\windeployqt.exe" $ExeName --release --compiler-runtime -no-translations --dir .
        $windeployqtSuccess = ($LASTEXITCODE -eq 0)
    } else {
        $output = & "$QtBinDir\windeployqt.exe" $ExeName --release --compiler-runtime -no-translations --dir . 2>&1
        $windeployqtSuccess = ($LASTEXITCODE -eq 0)
        if (-not $windeployqtSuccess) {
            Write-ErrorMsg "windeployqt 执行失败，错误输出："
            $output | ForEach-Object { Write-Host $_ -ForegroundColor Red }
        }
    }
    if ($windeployqtSuccess) {
        Write-Success "windeployqt 部署成功。"
    } else {
        # 手动复制依赖（备用方案）
        Write-Warn "windeployqt 部署失败，将尝试手动复制依赖。"
        Write-Info "正在手动复制关键 Qt DLL..."
        $qtDlls = @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "Qt6Qml.dll", "Qt6Quick.dll")
        foreach ($dll in $qtDlls) {
            $src = Join-Path $QtBinDir $dll
            $dest = Join-Path $ReleaseDir $dll
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination $dest -Force
                Write-Success "  已复制 $dll"
            }
        }
        $platformSrc = Join-Path $QtPluginsDir "platforms\qwindows.dll"
        $platformDest = Join-Path $ReleaseDir "platforms\qwindows.dll"
        if (Test-Path $platformSrc) {
            New-Item -ItemType Directory -Path (Split-Path $platformDest -Parent) -Force | Out-Null
            Copy-Item -Path $platformSrc -Destination $platformDest -Force
            Write-Success "  已复制 platforms/qwindows.dll"
        }
        if ($isMingwToolchain -and $MinGWBinDir) {
            $mingwDlls = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
            foreach ($dll in $mingwDlls) {
                $src = Join-Path $MinGWBinDir $dll
                $dest = Join-Path $ReleaseDir $dll
                if (Test-Path $src) {
                    Copy-Item -Path $src -Destination $dest -Force
                    Write-Success "  已复制 $dll"
                }
            }
        }
        Write-Success "手动复制完成。"
    }
} finally {
    Pop-Location
}

# ==========================================================
# 第5步：清理构建目录
# ==========================================================
Write-Info "正在清理构建目录 $BuildDir ..."
if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
    Write-Success "已删除 $BuildDir"
}

# ==========================================================
# 完成信息（仅输出发布目录）
# ==========================================================
Write-Info "===== 打包完成 =====" -Color "Cyan"
Write-Success "发布目录: $ReleaseDir"