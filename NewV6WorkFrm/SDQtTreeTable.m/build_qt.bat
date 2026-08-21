@echo off
setlocal
call "D:\VS2015\VC\bin\amd64\vcvars64.bat"

set QT_DIR=C:\Qt\Qt5.9.0\5.9\msvc2015_64
set MODULE_DIR=%~dp0
set OBJ_DIR=%MODULE_DIR%Objects\win_b64

if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"

cl.exe /nologo /c /EHsc /O2 /MD /utf-8 ^
    /I"%QT_DIR%\include" ^
    /I"%QT_DIR%\include\QtCore" ^
    /I"%QT_DIR%\include\QtGui" ^
    /I"%QT_DIR%\include\QtWidgets" ^
    /I"%MODULE_DIR%LocalInterfaces" ^
    /I"%MODULE_DIR%..\PublicInterfaces" ^
    /I"%MODULE_DIR%qt_src" ^
    /Fo"%OBJ_DIR%\TreeTableWidget.obj" ^
    "%MODULE_DIR%qt_src\TreeTableWidget.cpp"
if errorlevel 1 exit /b 1

cl.exe /nologo /c /EHsc /O2 /MD /utf-8 ^
    /I"%QT_DIR%\include" ^
    /I"%QT_DIR%\include\QtCore" ^
    /I"%QT_DIR%\include\QtGui" ^
    /I"%QT_DIR%\include\QtWidgets" ^
    /I"%MODULE_DIR%LocalInterfaces" ^
    /I"%MODULE_DIR%..\PublicInterfaces" ^
    /I"%MODULE_DIR%qt_src" ^
    /Fo"%OBJ_DIR%\QtTreeTableBridge.obj" ^
    "%MODULE_DIR%qt_src\QtTreeTableBridge.cpp"
if errorlevel 1 exit /b 1

lib.exe /NOLOGO /OUT:"%OBJ_DIR%\SDQtTreeTable.lib" /MACHINE:X64 ^
    "%OBJ_DIR%\TreeTableWidget.obj" ^
    "%OBJ_DIR%\QtTreeTableBridge.obj"
if errorlevel 1 exit /b 1

echo Qt tree table library build completed.
endlocal
