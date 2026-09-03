@echo off
rem ==========================================================
rem Release build script for QT project (使用windeployqt自动检测依赖)
rem 自动化编译和打包应用程序及其所有依赖，不硬编码任何DLL或插件路径
rem ==========================================================

rem ==========================================================
rem 配置区域 - 根据实际情况修改这些路径
rem ==========================================================
set "QtBinDir=C:\Qt\6.11.2\llvm-mingw_64\bin"
set "MinGWBinDir=C:\Qt\Tools\llvm-mingw1706_64\bin"
set "ProjectFile=..\QT.pro"          rem 相对于脚本目录
set "ExeName=QT.exe"
set "BuildDir=build_release"
set "ReleaseDir=rele"

rem ==========================================================
rem 设置环境变量
rem ==========================================================
set "PATH=%MinGWBinDir%;%QtBinDir%;%PATH%"

rem 导出 Qt 基目录（bin 目录的上一级）
for %%I in ("%QtBinDir%..") do set "QtBaseDir=%%~fI"
rem Qt 插件目录
set "QtPluginsDir=%QtBaseDir%\plugins"

rem ==========================================================
rem 清理以前的编译产物
rem ==========================================================
echo 正在清理以前的 %BuildDir% 和 %ReleaseDir% 文件夹...
if exist "%BuildDir%" rmdir /s /q "%BuildDir%"
if exist "%ReleaseDir%" rmdir /s /q "%ReleaseDir%"

rem ==========================================================
rem 编译应用程序
rem ==========================================================
echo 创建编译目录...
if not exist "%BuildDir%" mkdir "%BuildDir%"
pushd "%BuildDir%"

echo 正在运行 qmake...
qmake "%ProjectFile%" -spec win32-clang-g++ "CONFIG+=release" "CONFIG+=qtquickcompiler"
if errorlevel 1 (
    popd
    echo qmake 失败，退出代码 %errorlevel%
    exit /b %errorlevel%
)

echo 正在使用 mingw32-make 编译...
mingw32-make -j4
if errorlevel 1 (
    popd
    echo mingw32-make 失败，退出代码 %errorlevel%
    exit /b %errorlevel%
)

set "ExePath=%cd%\release\%ExeName%"
if not exist "%ExePath%" (
    popd
    echo 未找到可执行文件 %ExePath%
    exit /b 1
)
echo 编译成功：%ExePath%
popd

rem ==========================================================
rem 准备发布目录并复制可执行文件
rem ==========================================================
echo 正在准备发布目录...
if not exist "%ReleaseDir%" mkdir "%ReleaseDir%"
copy /y "%ExePath%" "%ReleaseDir%\"
echo 已复制 %ExeName% 到 %ReleaseDir%

rem ==========================================================
rem 使用 windeployqt 自动部署所有依赖（包括插件）
rem ==========================================================
echo 正在使用 windeployqt 自动检测并部署依赖...
rem 设置 QT_PLUGIN_PATH 使 windeployqt 能找到 Qt 插件
set "QT_PLUGIN_PATH=%QtPluginsDir%"
rem 使用 --qtpaths 参数指定 Qt 安装位置（避免使用已废弃的 --qtdir）
pushd "%ReleaseDir%"
"%QtBinDir%\windeployqt.exe" "%ExeName%" --release --compiler-runtime -no-translations --qtpaths "%QtBinDir%\qtpaths.exe"
if errorlevel 1 (
    popd
    echo windeployqt 失败，退出代码 %errorlevel%
    exit /b %errorlevel%
)
popd

rem ==========================================================
rem 可选：运行自测试以验证包装
rem ==========================================================
echo.
echo 正在运行自测试以验证包装...
set "TestArgs=..\testcases"
"%ReleaseDir%\%ExeName%" --selftest %TestArgs%
if errorlevel 1 (
    echo 警告: 自测试失败或超时，退出代码 %errorlevel%
    echo 这可能是正常的，如果测试用例未设置或需要交互。
) else (
    echo 自测试通过！
)

rem ==========================================================
rem 完成提示
rem ==========================================================
echo.
echo ===== 打包完成 =====
echo 发布文件夹位置：%cd%\%ReleaseDir%
echo 目录结构：
for /r "%ReleaseDir%" %%F in (*) do echo   %%F

echo.
echo 如需分发，请直接将 %ReleaseDir%\ 文件夹复制到目标机器，
echo 或使用以下命令打包为 ZIP（需要 PowerShell）：
echo     powershell -Command "Compress-Archive -Path '%ReleaseDir%\*' -DestinationPath '%ExeName%-win64.zip'"
echo.
endlocal