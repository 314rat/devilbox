@echo on
echo Starting minimal build...

echo Step 1: Checking for utils directory...
if not exist "utils" (
  echo ERROR: utils directory not found!
  goto end
)
echo utils directory exists.

echo Step 2: Listing files in utils directory...
dir /b utils\*.c
echo Files listed.

echo Step 3: Killing existing process...
taskkill /F /IM DevilboxManager.exe 2>nul
echo Process killed or not running.

echo Step 4: Compiling...
g++ main.cpp utils\*.c -Iutils -o DevilboxManager.exe -lole32 -lcomctl32 -lshell32 -lgdi32 -lcomdlg32 -mwindows

if %ERRORLEVEL% NEQ 0 (
  echo Compilation failed with error code %ERRORLEVEL%
  echo Trying without -mwindows flag for error output...
  g++ main.cpp utils\*.c -Iutils -o DevilboxManager.exe -lole32 -lcomctl32 -lshell32 -lgdi32 -lcomdlg32
)

if exist DevilboxManager.exe (
  echo DevilboxManager.exe was created successfully.
  echo Starting application...
  start "" DevilboxManager.exe
) else (
  echo ERROR: DevilboxManager.exe was not created.
)

:end
echo Script completed.
pause