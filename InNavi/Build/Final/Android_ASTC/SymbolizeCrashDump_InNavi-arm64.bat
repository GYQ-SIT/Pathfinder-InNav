@echo off
IF %1.==. GOTO NoArgs
if "%NDKROOT%"=="" GOTO NoNDK
setlocal

%NDKROOT%\ndk-stack.cmd -sym InNavi_Symbols_v5/InNaviarm64 -dump "%1" > InNavi_SymbolizedCallStackOutput.txt

goto:eof


:NoArgs
echo.
echo Required argument missing, pass a dump of adb crash log. (SymboliseCallStackDump C:\adbcrashlog.txt)
pause

goto:eof

:NoNDK
echo.
echo Unable to locate local NDK location.
pause
