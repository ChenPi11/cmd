@echo off
rem The batch script the mcpp workflow runs on every row that runs a program.
rem Its parts are levels, each after a line "-- L<n>": L0 the interpreter alone,
rem L1 files, L2 a program it starts. check.sh compares each part with its part of
rem expected.txt: a row must pass every part up to its floor (floor.txt); a part
rem above the floor is reported, so a capability a row gains shows by itself.
setlocal enabledelayedexpansion
echo -- L0
set N=0
:loop
set /a N+=1
if !N! lss 3 goto loop
echo N=!N!
call :sub hi there
echo errorlevel=%errorlevel%
for %%f in (a b c) do echo item %%f
set /a X=6*7
echo X=%X%
echo -- L1
md okt
echo x> okt\f1.txt
copy okt\f1.txt okt\f2.txt >nul
ren okt\f2.txt f3.txt
type okt\f3.txt
dir /b okt
del okt\f1.txt okt\f3.txt
rd okt
if not exist okt echo okt removed
echo -- L2
rem A program the interpreter starts, through COMSPEC: itself (on Windows the system's).
"%COMSPEC%" /c exit 3
echo child=%errorlevel%
echo done
endlocal
goto :eof
:sub
echo sub args %1 %2
exit /b 5
