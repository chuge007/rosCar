@echo off
SetLocal EnableDelayedExpansion
(set PATH=D:\qt\5.12.4\msvc2017_64\bin;!PATH!)
if defined QT_PLUGIN_PATH (
    set QT_PLUGIN_PATH=D:\qt\5.12.4\msvc2017_64\plugins;!QT_PLUGIN_PATH!
) else (
    set QT_PLUGIN_PATH=D:\qt\5.12.4\msvc2017_64\plugins
)
%*
EndLocal
