SoundFix
========

SoundFix identifies the song in your recorded video, looks it up on YouTube, 
downloads a high-quality version, synchronizes it with your recorded audio 
and puts it back into the video you recorded.

Typical usage: 

[![SoundFix usage](/usage-preview.png?raw=true)](http://www.youtube.com/watch?v=AVIHpaNQLS0)

Latest build: http://patraulea.com/files/soundfix-160303.zip

FFmpeg and youtube-dl are used for video downloading and conversion. Get them
from the project websites below and put the ffmpeg.exe and youtube-dl.exe in
the tools/ directory:

- http://ffmpeg.zeranoe.com/builds/
- http://rg3.github.io/youtube-dl/

Windows only at the moment but Qt is used for the UI and it should compile on
Linux/Mac as well.
