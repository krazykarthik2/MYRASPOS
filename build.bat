@echo ON
set REAL_FLAG=
set DEBUG_FLAG=
set LINKER_SCRIPT=linkers\linker.ld
set IS_REAL=0
set IS_DEBUG=0

:loop
if "%~1"=="" goto endloop
if /I "%~1"=="--real" (
    set REAL_FLAG=-DREAL
    set LINKER_SCRIPT=linkers\linker_pi.ld
    set IS_REAL=1
)
if /I "%~1"=="--debug" (
    set DEBUG_FLAG=-DDEBUG
    set IS_DEBUG=1
)
shift
goto loop
:endloop

echo Building with REAL_FLAG=%REAL_FLAG%
echo Building with DEBUG_FLAG=%DEBUG_FLAG%
echo Using LINKER_SCRIPT=%LINKER_SCRIPT%

if /I "%REAL_FLAG%"=="-DREAL" (
    @REM Real build: skip disk.img (make_real_disk.py runs after linking)
    echo Skipping disk.img for real hardware build...
) else (
    @REM Sim build: recreate disk.img for QEMU
    echo Creating disk.img...
    taskkill /F /IM qemu-system-aarch64.exe /T 2>nul
    python make_disk.py
    if %ERRORLEVEL% neq 0 (
        echo.
        echo *************** ERROR ***************
        echo  Failed to create disk.img!
        echo  Make sure QEMU is closed.
        echo *************************************
        exit /b %ERRORLEVEL%
    )
)

del /F /Q temp\objects\*.o
del /F /Q temp\elfs\*.elf
del /F /Q temp\binaries\*.img
del /F /Q temp\maps\*.map

set GCC=aarch64\aarch64-none-elf-gcc.bat
set C_FLAGS=-ffixed-x18 -fno-builtin -fno-merge-constants -fno-common -mgeneral-regs-only -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a53 -march=armv8-a -mabi=lp64 -Wall -Wextra -Wmissing-prototypes -Ikernel -DLODEPNG_NO_COMPILE_ALLOCATORS -DLODEPNG_NO_COMPILE_DISK %REAL_FLAG% %DEBUG_FLAG%

call %GCC% %C_FLAGS% -c boot\start.S -o temp\objects\start.o 
call %GCC% %C_FLAGS% -c kernel\vectors.S -o temp\objects\vectors.o
call %GCC% %C_FLAGS% -c kernel\swtch.S -o temp\objects\swtch.o
call %GCC% %C_FLAGS% -c kernel\kernel.c -o temp\objects\kernel.o
call %GCC% %C_FLAGS% -c kernel\uart.c -o temp\objects\uart.o
call %GCC% %C_FLAGS% -c kernel\palloc.c -o temp\objects\palloc.o
call %GCC% %C_FLAGS% -c kernel\kmalloc.c -o temp\objects\kmalloc.o
call %GCC% %C_FLAGS% -c kernel\ramfs.c -o temp\objects\ramfs.o
call %GCC% %C_FLAGS% -c kernel\lib.c -o temp\objects\lib.o
call %GCC% %C_FLAGS% -c kernel\syscall.c -o temp\objects\syscall.o
call %GCC% %C_FLAGS% -c kernel\timer.c -o temp\objects\timer.o
call %GCC% %C_FLAGS% -c kernel\irq.c -o temp\objects\irq.o
call %GCC% %C_FLAGS% -c kernel\framebuffer.c -o temp\objects\framebuffer.o
call %GCC% %C_FLAGS% -c kernel\virtio.c -o temp\objects\virtio.o
call %GCC% %C_FLAGS% -c kernel\rpi_fx.c -o temp\objects\rpi_fx.o
call %GCC% %C_FLAGS% -c kernel\dma.c -o temp\objects\dma.o
call %GCC% %C_FLAGS% -c kernel\emmc.c -o temp\objects\emmc.o
call %GCC% %C_FLAGS% -c kernel\emmc_clock.c -o temp\objects\emmc_clock.o
call %GCC% %C_FLAGS% -c kernel\usb.c -o temp\objects\usb.o
call %GCC% %C_FLAGS% -c kernel\mmu.c -o temp\objects\mmu.o
call %GCC% %C_FLAGS% -c kernel\diskfs.c -o temp\objects\diskfs.o
call %GCC% %C_FLAGS% -c kernel\init.c -o temp\objects\init.o
call %GCC% %C_FLAGS% -c kernel\programs.c -o temp\objects\programs.o
call %GCC% %C_FLAGS% -c kernel\commands\echo.c -o temp\objects\echo.o
call %GCC% %C_FLAGS% -c kernel\commands\help.c -o temp\objects\help.o
call %GCC% %C_FLAGS% -c kernel\commands\touch.c -o temp\objects\touch.o
call %GCC% %C_FLAGS% -c kernel\write.c -o temp\objects\write.o
call %GCC% %C_FLAGS% -c kernel\commands\cat.c -o temp\objects\cat.o
call %GCC% %C_FLAGS% -c kernel\commands\ls.c -o temp\objects\ls.o
call %GCC% %C_FLAGS% -c kernel\commands\rm.c -o temp\objects\rm.o
call %GCC% %C_FLAGS% -c kernel\commands\mkdir.c -o temp\objects\mkdir.o
call %GCC% %C_FLAGS% -c kernel\commands\rmdir.c -o temp\objects\rmdir.o
call %GCC% %C_FLAGS% -c kernel\commands\cp.c -o temp\objects\cp.o
call %GCC% %C_FLAGS% -c kernel\commands\mv.c -o temp\objects\mv.o
call %GCC% %C_FLAGS% -c kernel\commands\grep.c -o temp\objects\grep.o
call %GCC% %C_FLAGS% -c kernel\commands\head.c -o temp\objects\head.o
call %GCC% %C_FLAGS% -c kernel\commands\tail.c -o temp\objects\tail.o
call %GCC% %C_FLAGS% -c kernel\commands\more.c -o temp\objects\more.o
call %GCC% %C_FLAGS% -c kernel\commands\tree.c -o temp\objects\tree.o
call %GCC% %C_FLAGS% -c kernel\shell.c -o temp\objects\shell.o
call %GCC% %C_FLAGS% -c kernel\sched.c -o temp\objects\sched.o
call %GCC% %C_FLAGS% -c kernel\panic.c -o temp\objects\panic.o
call %GCC% %C_FLAGS% -c kernel\service.c -o temp\objects\service.o
call %GCC% %C_FLAGS% -c kernel\glob.c -o temp\objects\glob.o
call %GCC% %C_FLAGS% -c kernel\pty.c -o temp\objects\pty.o
call %GCC% %C_FLAGS% -c kernel\input.c -o temp\objects\input.o
call %GCC% %C_FLAGS% -c kernel\wm.c -o temp\objects\wm.o
call %GCC% %C_FLAGS% -c kernel\apps\terminal_app.c -o temp\objects\terminal_app.o
call %GCC% %C_FLAGS% -c kernel\apps\myra_app.c -o temp\objects\myra_app.o
call %GCC% %C_FLAGS% -c kernel\apps\calculator_app.c -o temp\objects\calculator_app.o
call %GCC% %C_FLAGS% -c kernel\apps\files_app.c -o temp\objects\files_app.o
call %GCC% %C_FLAGS% -c kernel\cursor.c -o temp\objects\cursor.o
call %GCC% %C_FLAGS% -c kernel\apps\keyboard_tester_app.c -o temp\objects\keyboard_tester_app.o
call %GCC% %C_FLAGS% -c kernel\apps\editor_app.c -o temp\objects\editor_app.o
call %GCC% %C_FLAGS% -c kernel\commands\edit.c -o temp\objects\edit.o
call %GCC% %C_FLAGS% -c kernel\files.c -o temp\objects\files.o
call %GCC% %C_FLAGS% -c kernel\lodepng.c -o temp\objects\lodepng.o
call %GCC% %C_FLAGS% -c kernel\lodepng_glue.c -o temp\objects\lodepng_glue.o
call %GCC% %C_FLAGS% -c kernel\image.c -o temp\objects\image.o
call %GCC% %C_FLAGS% -c kernel\apps\image_viewer.c -o temp\objects\image_viewer.o
call %GCC% %C_FLAGS% -c kernel\debug_overlay.c -o temp\objects\debug_overlay.o

call %GCC% %C_FLAGS% -c kernel\commands\view.c -o temp\objects\view.o
call %GCC% %C_FLAGS% -c kernel\commands\clear.c -o temp\objects\clear.o
call %GCC% %C_FLAGS% -c kernel\commands\ps.c -o temp\objects\ps.o
call %GCC% %C_FLAGS% -c kernel\commands\sleep.c -o temp\objects\sleep.o
call %GCC% %C_FLAGS% -c kernel\commands\wait.c -o temp\objects\wait.o
call %GCC% %C_FLAGS% -c kernel\commands\kill.c -o temp\objects\kill.o
call %GCC% %C_FLAGS% -c kernel\commands\ramfs_tools.c -o temp\objects\ramfs_tools.o
call %GCC% %C_FLAGS% -c kernel\commands\systemctl.c -o temp\objects\systemctl.o
call %GCC% %C_FLAGS% -c kernel\commands\free.c -o temp\objects\free.o




if "%IS_REAL%"=="1" (
    echo Linking for REAL hardware...
    call "aarch64\aarch64-none-elf-ld.bat" temp\objects\start.o temp\objects\mmu.o temp\objects\diskfs.o temp\objects\vectors.o temp\objects\swtch.o temp\objects\kernel.o temp\objects\uart.o temp\objects\palloc.o temp\objects\kmalloc.o temp\objects\ramfs.o temp\objects\lib.o temp\objects\syscall.o temp\objects\timer.o temp\objects\irq.o temp\objects\framebuffer.o temp\objects\virtio.o temp\objects\rpi_fx.o temp\objects\dma.o temp\objects\emmc.o temp\objects\emmc_clock.o temp\objects\usb.o temp\objects\init.o temp\objects\programs.o temp\objects\echo.o temp\objects\help.o temp\objects\touch.o temp\objects\write.o temp\objects\cat.o temp\objects\ls.o temp\objects\rm.o temp\objects\mkdir.o temp\objects\rmdir.o temp\objects\cp.o temp\objects\mv.o temp\objects\grep.o temp\objects\head.o temp\objects\tail.o temp\objects\more.o temp\objects\tree.o temp\objects\shell.o temp\objects\sched.o temp\objects\panic.o temp\objects\service.o temp\objects\glob.o temp\objects\pty.o temp\objects\input.o temp\objects\wm.o temp\objects\terminal_app.o temp\objects\myra_app.o temp\objects\calculator_app.o temp\objects\files_app.o temp\objects\cursor.o temp\objects\keyboard_tester_app.o temp\objects\editor_app.o temp\objects\edit.o temp\objects\files.o temp\objects\image.o temp\objects\image_viewer.o temp\objects\lodepng.o temp\objects\lodepng_glue.o temp\objects\view.o temp\objects\clear.o temp\objects\ps.o temp\objects\sleep.o temp\objects\wait.o temp\objects\kill.o temp\objects\ramfs_tools.o temp\objects\systemctl.o temp\objects\free.o temp\objects\debug_overlay.o -T %LINKER_SCRIPT% -o temp\elfs\kernel.elf -Map temp\maps\kernel.map
) else (
    echo Linking for SIMULATION...
    call "aarch64\aarch64-none-elf-ld.bat" temp\objects\start.o temp\objects\mmu.o temp\objects\diskfs.o temp\objects\vectors.o temp\objects\swtch.o temp\objects\kernel.o temp\objects\uart.o temp\objects\palloc.o temp\objects\kmalloc.o temp\objects\ramfs.o temp\objects\lib.o temp\objects\syscall.o temp\objects\timer.o temp\objects\irq.o temp\objects\framebuffer.o temp\objects\virtio.o temp\objects\rpi_fx.o temp\objects\dma.o temp\objects\emmc.o temp\objects\emmc_clock.o temp\objects\usb.o temp\objects\init.o temp\objects\programs.o temp\objects\echo.o temp\objects\help.o temp\objects\touch.o temp\objects\write.o temp\objects\cat.o temp\objects\ls.o temp\objects\rm.o temp\objects\mkdir.o temp\objects\rmdir.o temp\objects\cp.o temp\objects\mv.o temp\objects\grep.o temp\objects\head.o temp\objects\tail.o temp\objects\more.o temp\objects\tree.o temp\objects\shell.o temp\objects\sched.o temp\objects\panic.o temp\objects\service.o temp\objects\glob.o temp\objects\pty.o temp\objects\input.o temp\objects\wm.o temp\objects\terminal_app.o temp\objects\myra_app.o temp\objects\calculator_app.o temp\objects\files_app.o temp\objects\cursor.o temp\objects\keyboard_tester_app.o temp\objects\editor_app.o temp\objects\edit.o temp\objects\files.o temp\objects\image.o temp\objects\image_viewer.o temp\objects\lodepng.o temp\objects\lodepng_glue.o temp\objects\view.o temp\objects\clear.o temp\objects\ps.o temp\objects\sleep.o temp\objects\wait.o temp\objects\kill.o temp\objects\ramfs_tools.o temp\objects\systemctl.o temp\objects\free.o temp\objects\debug_overlay.o -T %LINKER_SCRIPT% -o temp\elfs\kernel.elf -Map temp\maps\kernel.map
)

call "aarch64\aarch64-none-elf-objcopy.bat" -O binary temp\elfs\kernel.elf temp\binaries\kernel8.img


if "%IS_REAL%"=="1" (
    python make_rpi_boot.py
    echo.
    echo ============================================
    echo  Kernel built. Launching SD card flasher...
    echo ============================================
    python make_real_disk.py
)
