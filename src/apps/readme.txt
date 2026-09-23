n64forthos: a Forth operating system for the Nintendo 64

FILES
Your files live on the Controller Pak, the 32 KiB memory card in
the back of the controller, and stay there with the power off.
With no pak in the controller they go on a RAM disk instead, which
is gone at power off. The files that came with the cartridge are in
ROM: you can read them, run them and edit them, and saving one puts
your own copy on the pak, which then runs in its place. Delete your
copy and the original comes back.

A pak that has game saves on it has to be formatted before it can
hold files, and formatting erases the saves. FORMAT says so first.

AT THE PROMPT
  DIR                 list the files, and the room left
  CAT NAME            show a file
  EDIT NAME           edit a file (a new name makes a new file)
  INCLUDE NAME        run a file: compile it, line by line
  RUN NAME            run it as the desktop would: in a window
                      if it is an app, at the prompt if not
  DEL NAME            delete a file
  REN OLD NEW         rename one
  COPY FROM TO        copy one
  FORMAT              format the Controller Pak (asks first)
  MEM                 memory and disk space
  HELP                a short list of all this
  WORDS               every word the system knows

THE EDITOR
With a keyboard: type, the arrows move, ^S saves, ^R saves and
runs, Esc leaves. ^K cuts a line (again for more), ^U puts them
back where the cursor is, ^F finds and ^G finds the next. With a controller: the on-screen
keyboard types, the C buttons move the cursor, START saves, L
leaves. R shows or hides the on-screen keyboard.

STOPPING A PROGRAM
Esc or ^C on a keyboard, or START and Z together on the
controller, stops whatever is running and comes back to the
prompt. (A compiled loop that calls nothing at all, not even
a variable, cannot hear it.)

AT BOOT
If the pak holds a file called BOOT.FTH, it is run at startup,
before the desktop appears. Put your own words in it.

FOR PROGRAMS
  S" NAME" FILE?        ( -- size vol true | false )
  addr max S" NAME" LOAD-FILE   ( -- n ior )
  addr n S" NAME" SAVE-FILE     ( -- ior )
  S" NAME" DELETE-FILE          ( -- ior )
  S" OLD" S" NEW" RENAME-FILE   ( -- ior )
  #FILES  FILE#  DISK-FREE  KEY  INKEY  ACCEPT  MS
  hz ms BEEP     queue a note; it plays while you carry on
  QUIET  SOUNDING?  n VOLUME
An ior of 0 means it worked; .IOR prints what any other means.

An app is a file that defines ROWS and ROW (and, if it animates,
NEXT): it opens in a window with its source beside it. One that
defines FRAME owns the whole screen. TASKS.FTH runs three apps
at once, each in a window of the window manager. SKETCH.FTH is a small app
that keeps its picture in a file; MUSIC.FTH plays a tune in the
background; HELLO.FTH is a place to start.
