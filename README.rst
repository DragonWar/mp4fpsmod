=========
mp4fpsmod
=========

What is this?
-------------

You can use this tool to modify existing MP4 movie's fps.

Example
-------

Read foo.mp4, change fps to 25, and save to bar.mp4::

    mp4fpsmod -r 0:25 -o bar.mp4 foo.mp4

Read foo.mp4, change fps of first 300 frames to 30000/1001, next 600 frames to 24000/1001, and rest of the movie to 30000/1001 (producing VFR movie)::

    mp4fpsmod -r 300:30000/1001 -r 600:24000/1001 -r 0:30000/1001 -o bar.mp4 foo.mp4

To read timecodes from timecode_v2 file::

    mp4fpsmod -t timecode.txt foo.mp4 -o bar.mp4

Usage
-----

::

  -r nframes:fps          use this option to specify fps.
  -t timecode_v2_file     use this option to feed timecode v2 file.
  -x                      optimize timecode v2 entry(explained later)
  -o file                 output filename.

"nframes" is the number of frames, which "fps" is applied to. 0 as nframes means "rest of the movie".

You can use integer, or fraction value for "fps".

About timecode optimization
---------------------------

Consider timecode file like this::

  # timecode format v2
  0
  33
  67
  100
  133
  167
  200
    :

This is the example of timecode for 30000/1001 fps movie.  
In this case, each timeDelta of entries are 33, 34, 33, 33, 34, 33... 

When -x option specified, and timecodes are integer values, mp4fpsmod tries to divide timecodes into groups, whose entries have very near time delta. Then, average each group's time delta into one floating point value.

mp4fpsmod also tries to do further optimization when -x option specified.
If every timeDelta of frames looks like close enough to one of the well known NTSC or PAL rate, mp4fpsmod takes the latter and do the exact math, instead of floating point value.

You can control these behaviors by -x option. Without -x, literal values in the timecode file will be used.
