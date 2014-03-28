 #include <stdio.h>
 #include <stdlib.h>
 #include <vlc/vlc.h>
 
 int main(int argc, char* argv[])
 {
     libvlc_instance_t * inst;
     libvlc_media_player_t *mp;
     libvlc_media_t *m;
     char const *vlc_argv[] = {

        //"--no-audio", // Don't play audio.
        //"--no-xlib", // Don't use Xlib.
        // Apply a video filter.
        //"--video-filter", "sepia",
        //"--sepia-intensity=200"
        "-v",
    };
    int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);
     /* Load the VLC engine */
     inst = libvlc_new (vlc_argc, vlc_argv);
  
     /* Create a new item */
    // m = libvlc_media_new_location (inst, "http://mycool.movie.com/test.mov");
     m = libvlc_media_new_path (inst, "/media/fergus/HD-LSU2/Sync/Heather/homeland.s03e07.720p.hdtv.x264-killers.mkv");
        
     /* Create a media player playing environement */
     mp = libvlc_media_player_new_from_media (m);
     
     /* No need to keep the media now */
     libvlc_media_release (m);
 
 #if 0

    /* This is a non working code that show how to hooks into a window,
      * if we have a window around */
      libvlc_media_player_set_xwindow (mp, xid);
     /* or on windows */
      libvlc_media_player_set_hwnd (mp, hwnd);
     /* or on mac os */
      libvlc_media_player_set_nsobject (mp, view);
  #endif
 
     /* play the media_player */
     libvlc_media_player_play (mp);
    
     sleep (10); /* Let it play a bit */
    
     /* Stop playing */
     libvlc_media_player_stop (mp);
 
     /* Free the media_player */
     libvlc_media_player_release (mp);
 
     libvlc_release (inst);
 
     return 0;
 }

