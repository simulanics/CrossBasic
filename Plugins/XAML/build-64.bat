:: Compile the manifest into a resource
windres XAML.rc -O coff -o XAML.res
::C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsx86_amd64.bat
:: @call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_x86 %*
:: clang++ -std=c++20 -MD -m64 -shared -fno-delayed-template-parsing -o XAML.dll XAML.cpp XAML.res -lwindowsapp -lcomctl32 -ldwmapi -lgdi32 -luser32 -luxtheme -lole32


@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 goto :fail

rem --- Clean old outputs that may be the wrong arch ---
del /q XAML.obj 2>nul
:: del /q XAML.res 2>nul
del /q XAML.dll 2>nul
del /q XAML.lib 2>nul
del /q XAML.exp 2>nul

rem --- If you have a .rc file, compile it here ---
rem rc /nologo /fo XAML.res XAML.rc
rem if errorlevel 1 goto :fail

rem --- Force clang-cl to x64 target, and force linker machine x64 ---
clang-cl /nologo --target=x86_64-pc-windows-msvc /std:c++20 /MD /EHsc /LD ^
  XAML.cpp XAML.res ^
  /link /MACHINE:X64 /OUT:XAML.dll ^
  WindowsApp.lib comctl32.lib dwmapi.lib gdi32.lib user32.lib uxtheme.lib ole32.lib

if errorlevel 1 goto :fail

echo.
echo Build OK: XAML.dll
goto :done

:fail
echo.
echo Build FAILED (errorlevel=%ERRORLEVEL%)
:done
endlocal
