@echo off
setlocal

rem 仅清理本工程的Keil输出目录，避免调用目录变化时误删其他文件。
for %%D in ("%~dp0Objects" "%~dp0Listings") do (
    if exist "%%~D" (
        pushd "%%~D"
        for %%E in (bak ddk edk lst lnp mpf mpj obj omf plg rpt tmp __i crf o d axf tra dep iex htm sct map hex uvopt dbgconf scvd) do (
            del /s /q "*.%%E" >nul 2>&1
        )
        del /s /q "*.uvgui.*" >nul 2>&1
        del /s /q "*.build_log.htm" >nul 2>&1
        popd
    )
)

rem 调试日志只允许删除工程目录内的固定文件。
del /q "%~dp0JLinkLog.txt" >nul 2>&1

endlocal
exit /b 0
