# DesktopVideo
桌面视频壁纸播放器
A video player that can make you wallpaper as video

渲染使用了vlc
Video rendering with libvlc(C)


It may be difficult to develop this program,you should open .sln file with Visual Studio,then you will see two project in the solution.
The UI client program written in C# is a Windows From Application.
The main controll program as server is a Windows Console Program via C++.You should add the sdk/lib dir and sdk/include dir to your C++ compile settings so that you can compile it correctly.And also you should copy plugin dir and other two dlls to you Output dir so that the server can run.
The client control server's life time,make the two projects Output dir to be the same.
Then start client,program can run.

if you just want to use it,go release.
