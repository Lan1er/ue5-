@echo off
rem 一键保存进度：把所有改动提交到本地仓库（方便随时回滚）
rem 用法：双击本文件，输入一句说明即可
cd /d "%~dp0.."
echo.
echo ==== Saving current progress to local git repo ====
echo.
git add -A
git status --short
echo.
set /p msg="Describe this change (Enter = use timestamp): "
if "%msg%"=="" set msg=save %date% %time%
git commit -m "%msg%"
echo.
echo ==== Recent history ====
git log --oneline -8
echo.
pause
