@ECHO OFF
SET PLAIN_MAKEFILE=makefiles\makefile-plainstarter
REM SAVE PATH
SET PLAIN_PREVIOUS_PATH=%Path%
CALL "%~dp0..\PLAIN-TOOLCHAIN-X86-64\cmd-env-64.bat"
set PATH=%PATH%;%~dp0\third-party\bin
REM CALL GNU MAKE
cd "%~dp0"
make --no-builtin-rules -f "%PLAIN_MAKEFILE%" -j4 all -C .
REM RESTORE PATH
SET Path=%PLAIN_PREVIOUS_PATH%
