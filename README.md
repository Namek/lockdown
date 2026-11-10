# lockdown
A system tray app to force your Windows workstation to become locked after a period of inactivity.

Sometimes Windows 11 fails to lock even if you set the screensaver timeout and check the lock box. This app is an alternative to the screensaver timeout. It can monitor button presses on multiple controllers, including the PS5 DualSense over USB or Bluetooth. You can connect and disconnect controllers while the app runs.

This screenshot shows the lockdown system tray icon (on the left) and the lockdown menu. The default timeout is 20 minutes and is settable as a command-line parameter if you want a different value. Hovering over the lock icon shows the current remaining time (in minutes and seconds) before the machine locks.

![Lockdown](https://raw.githubusercontent.com/bluescan/lockdown/master/Screenshots/LockdownScreenshot.png)


# usage

Consider running as a scheduled task on logon.

![Lockdown](https://raw.githubusercontent.com/bluescan/lockdown/master/Screenshots/LockdownTaskTriggers.png)


Run lockdown.exe -h to see the input and timeout options. The default 20-minute countdown resets on key presses, mouse button clicks, mouse movement beyond a threshold, and gamepad button presses. Gamepad stick movement and other analog input are ignored. When selecting inputs explicitly, use -p to include gamepad buttons or -kp for keyboard and gamepad buttons.

![Lockdown](https://raw.githubusercontent.com/bluescan/lockdown/master/Screenshots/LockdownTaskActions.png)

Do not terminate the task.

![Lockdown](https://raw.githubusercontent.com/bluescan/lockdown/master/Screenshots/LockdownTaskSettings.png)
