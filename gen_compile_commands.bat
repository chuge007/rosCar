@echo off
rem Regenerate compile_commands.json for clangd after qmake re-generates the Makefiles.
rem Run "build_workbench.bat" / "build_host.bat" first so the Makefiles exist.
python "%~dp0gen_compile_commands.py"
