adb root
adb remount
::md .\TODAY_jpgs 
adb pull /sdcard/DCIM/Camera/ C:\Users\52910\Desktop\Module_Test\photo


adb shell rm /sdcard/DCIM/Camera/*
adb shell rm /sdcard/DCIM/Camera/raw/*



ping 127.0.0.1 /n 2 >nul

