# ai_passport

监控服务关闭
Stop-ScheduledTask -TaskName 'FoloToy PcMon Bridge'
监控服务打开
跑`install_autostart.ps1` 并`Start-ScheduledTask -TaskName 'FoloToy PcMon Bridge'` 。