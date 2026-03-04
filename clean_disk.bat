@echo off
del disk.img
echo Error Level: %errorlevel%
if exist disk.img (
    echo File still exists
) else (
    echo File deleted
)
