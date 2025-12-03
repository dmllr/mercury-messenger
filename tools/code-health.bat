@echo off
setlocal

:: --- Configuration ---
set "DOCKER_IMAGE_NAME=project-mercury-devenv"
:: Get project root from git
for /f "tokens=* delims=" %%i in ('git rev-parse --show-toplevel') do set "PROJECT_ROOT=%%i"
set "DOCKERFILE_PATH=%PROJECT_ROOT%\devenv\Dockerfile"

:: --- Main Script ---
if "%~1"=="" (
    echo Usage: %0 {format^|tidy^|all}
    exit /b 1
)

call :build_docker_image_if_needed
if %errorlevel% neq 0 exit /b %errorlevel%

:loop_commands
if "%~1"=="" goto :eof

if /i "%~1"=="format" (
    call :run_format
) else if /i "%~1"=="tidy" (
    call :run_tidy
) else if /i "%~1"=="all" (
    call :run_format
    call :run_tidy
) else (
    echo WARN: Unknown command: %~1. Skipping.
    echo Usage: %0 {format^|tidy^|all}
)

shift
goto :loop_commands

goto :eof


:: --- Subroutines ---

:build_docker_image_if_needed
    echo INFO: Checking for Docker image '%DOCKER_IMAGE_NAME%'...
    docker images -q "%DOCKER_IMAGE_NAME%" | findstr . >nul
    if %errorlevel% neq 0 (
        echo INFO: Docker image not found. Building it now...
        docker build -t "%DOCKER_IMAGE_NAME%" -f "%DOCKERFILE_PATH%" "%PROJECT_ROOT%"
        if %errorlevel% neq 0 (
            echo ERROR: Docker build failed.
            exit /b 1
        )
        echo SUCCESS: Docker image built.
    ) else (
        echo INFO: Docker image found.
    )
exit /b 0

:run_in_docker
    echo INFO: Running in Docker: %*
    rem Set a writable global git config inside the project dir to avoid permission errors with //.gitconfig,
    rem then add the workspace as a safe directory to fix the 'dubious ownership' error.
    rem The 'exec "$@"' part ensures that the original command is then executed.
    docker run --rm -it ^
        -v "%PROJECT_ROOT%:/work" ^
        -w /work ^
        "%DOCKER_IMAGE_NAME%" ^
        bash -c "export GIT_CONFIG_GLOBAL=/work/.gitconfig.tmp && git config --global --add safe.directory /work && exec \"$@\"" bash %*
exit /b %errorlevel%

:run_format
    echo INFO: Running clang-format...
    call :run_in_docker bash -c "git ls-files -z -- src tests ^| grep -z -E \"\\.(cpp^|hpp^|h)$\" ^| while IFS= read -r -d '' file; do echo \"Formatting $$file...\"; clang-format-18 -i \"$$file\"; done"
    if %errorlevel% neq 0 (
        echo ERROR: clang-format failed.
        exit /b 1
    )
    echo SUCCESS: clang-format finished.
exit /b 0

:run_tidy
    echo INFO: Running clang-tidy...
    set "build_dir=build-tidy"
    
    echo INFO: Step 1: Generating compile_commands.json...
    call :run_in_docker bash -c "cmake -S . -B %build_dir% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null"
    if %errorlevel% neq 0 (
        echo ERROR: CMake failed to generate compile commands.
        exit /b 1
    )
    echo SUCCESS: compile_commands.json generated in '%build_dir%'.

    echo INFO: Step 2: Running clang-tidy on the codebase...
    call :run_in_docker bash -c "run-clang-tidy-18 -p %build_dir% -j $(nproc) -fix"
    if %errorlevel% neq 0 (
        echo WARN: clang-tidy reported issues. Please review the changes or re-run to confirm fixes.
    )
    echo SUCCESS: clang-tidy finished.
exit /b 0
