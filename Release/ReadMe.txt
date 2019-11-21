netstat -ano| find "192.168.0.13:10240" /c
32722


taskkill /f /im clienttest.exe


链接数到16359多的时候，会无法再连入。
但kill后，仍然可以，反复试过很多次。