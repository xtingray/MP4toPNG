/usr/bin/cc -v cutter.c -o cutter -L/usr/local/ffmpeg/lib -Wl,-rpath,/usr/local/ffmpeg/lib -lavcodec -lavformat -lavutil -lswscale -lpng 
