@echo off
setlocal

:: Compile server.cpp with metadata
g++ ./CrossBasic-IDE/server.cpp -static-libgcc -static-libstdc++ -O3 -std=c++17 -s -static -l:libboost_system-mgw14-mt-s-x64-1_87.a -l:libboost_thread-mgw14-mt-s-x64-1_87.a -lws2_32 -lwsock32 -pthread -lz -o server.exe 2> error.log

:: Check if compilation was successful
if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed! Check error.log for details.
    type error.log
    exit /b %ERRORLEVEL%
)

:: Ensure the release directory exists
if not exist release-64 mkdir release-64

:: Move the compiled executable to the release directory
move /Y server.exe release-64\

:: Copy the IDE web folder to the release directory
xcopy "CrossBasic-IDE\www\*" "release-64\www\" /E /I /Y

:: Dump DLL dependencies using objdump
echo DLL dependencies:
objdump -p release-64\server.exe | findstr /R "DLL"

echo CrossBasic app server Built Successfully.
exit /b 0
