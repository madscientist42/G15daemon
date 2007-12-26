/*
        This file is part of g15daemon.

        g15daemon is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation; either version 2 of the License, or
        (at your option) any later version.

        g15daemon is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with g15daemon; if not, write to the Free Software
        Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

        (c) 2006-2007 Mike Lampard 

        $Revision$ -  $Date$ $Author$

        This daemon listens on localhost port 15550 for client connections,
        and arbitrates LCD display.  Allows for multiple simultaneous clients.
        Client screens can be cycled through by pressing the 'L1' key.

        This is a macro recorder and playback utility for the G15 and g15daemon.
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pwd.h>
#include <pthread.h>
#include <sys/time.h>
#include <config.h>
#include <X11/Xlib.h>
#ifdef HAVE_X11_EXTENSIONS_XTEST_H
#include <X11/extensions/XTest.h>
#endif
#include <X11/XF86keysym.h>

#include <g15daemon_client.h>
#include <libg15.h>
#include <libg15render.h>
#include "config.h"

#define  XK_MISCELLANY 
#define XK_LATIN1
#define XK_LATIN2
#include <X11/keysymdef.h>
        
int g15screen_fd;
int config_fd = 0;
g15canvas *canvas;

static Display *dpy;
static Window root_win;

pthread_mutex_t x11mutex;

int leaving = 0;
int display_timeout=500;
int have_xtest = False;

unsigned char recstring[1024];

static unsigned int mled_state = G15_LED_M1;
static int mkey_state = 0;
static int recording = 0;

typedef struct keypress_s {
    unsigned long keycode;
    unsigned long time_ms;
    unsigned char pressed;
    unsigned long modifiers;
    unsigned int mouse_x;
    unsigned int mouse_y;
    unsigned int buttons;
}keypress_t;


#define MAX_KEYSTEPS 1024

typedef struct keysequence_s {
    keypress_t recorded_keypress[MAX_KEYSTEPS];
    unsigned int record_steps;
} keysequence_t;

typedef struct gkeys_s{
    unsigned int recorded;
    keysequence_t keysequence;
}gkeys_t;

typedef struct mstates_s {
    gkeys_t gkeys[18];
}mstates_t; 

mstates_t *mstates[3];

struct current_recording {
    keypress_t recorded_keypress[MAX_KEYSTEPS];
}current_recording;

unsigned int rec_index=0;

const char *gkeystring[] = { "G1","G2","G3","G4","G5","G6","G7","G8","G9","G10","G11","G12","G13","G14","G15","G16","G17","G18","Unknown" };

const int gkeycodes[] = { 177,152,190,208,129,130,231,209,210,136,220,143,246,251,137,138,133,183 };

const int mmedia_codes[] = {164, 162, 144, 153, 174, 176};

const long mmedia_defaults[] = {
    XF86XK_AudioStop,
    XF86XK_AudioPlay,
    XF86XK_AudioPrev,
    XF86XK_AudioNext,
    XF86XK_AudioLowerVolume,
    XF86XK_AudioRaiseVolume
};
/* because this is an X11 client, we can work around the kernel limitations on key numbers */
const long gkeydefaults[] = {
    /* M1 palette */
    XF86XK_Launch4,
    XF86XK_Launch5,
    XF86XK_Launch6,
    XF86XK_Launch7,
    XF86XK_Launch8,
    XF86XK_Launch9,
    XF86XK_LaunchA,
    XF86XK_LaunchB, 
    XF86XK_LaunchC,  
    XF86XK_LaunchD, 
    XF86XK_LaunchE, 
    XF86XK_LaunchF, 
    XF86XK_iTouch, 
    XF86XK_Calculater, 
    XF86XK_Support, 
    XF86XK_Word, 
    XF86XK_Messenger, 
    XF86XK_WebCam,
    /* M2 palette */
    XK_F13,
    XK_F14,
    XK_F15,
    XK_F16,
    XK_F17,
    XK_F18,
    XK_F19,
    XK_F20,
    XK_F21,
    XK_F22,
    XK_F23,
    XK_F24,
    XK_F25,
    XK_F26,
    XK_F27,
    XK_F28,
    XK_F29,
    XK_F30,
    /* M3 palette */
    XK_Tcedilla,
    XK_racute,
    XK_abreve,
    XK_lacute,
    XK_cacute,
    XK_ccaron,
    XK_eogonek,
    XK_ecaron,
    XK_dcaron,
    XK_dstroke,
    XK_nacute,
    XK_ncaron,
    XK_odoubleacute,
    XK_udoubleacute,
    XK_rcaron,
    XK_uring,
    XK_scaron,
    XK_abovedot
};

int map_gkey(keystate){
    int retval = -1;
    switch(keystate){
        case G15_KEY_G1:  retval = 0;   break;
        case G15_KEY_G2:  retval = 1;   break;
        case G15_KEY_G3:  retval = 2;   break;
        case G15_KEY_G4:  retval = 3;   break;
        case G15_KEY_G5:  retval = 4;   break;
        case G15_KEY_G6:  retval = 5;   break;
        case G15_KEY_G7:  retval = 6;   break;
        case G15_KEY_G8:  retval = 7;   break;
        case G15_KEY_G9:  retval = 8;   break;
        case G15_KEY_G10: retval = 9;   break;
        case G15_KEY_G11: retval = 10;   break;
        case G15_KEY_G12: retval = 11;   break;
        case G15_KEY_G13: retval = 12;   break;
        case G15_KEY_G14: retval = 13;   break;
        case G15_KEY_G15: retval = 14;   break;
        case G15_KEY_G16: retval = 15;   break;
        case G15_KEY_G17: retval = 16;   break;
        case G15_KEY_G18: retval = 17;   break;
    }
    return retval;
}

void fake_keyevent(int keycode,int keydown,unsigned long modifiers){
  if(have_xtest && !recording) { 
    #ifdef HAVE_X11_EXTENSIONS_XTEST_H        
    pthread_mutex_lock(&x11mutex);
       XTestFakeKeyEvent(dpy, keycode,keydown, CurrentTime);
    pthread_mutex_unlock(&x11mutex);
    usleep(1500);
     #endif        
  } else {
    XKeyEvent event;
    Window current_focus;
    int dummy = 0;
    int key = 0;

    pthread_mutex_lock(&x11mutex);
      XGetInputFocus(dpy,&current_focus, &dummy);
      key = XKeycodeToKeysym(dpy,keycode,0);
      if(keydown)
        event.type=KeyPress;
      else
        event.type=KeyRelease;

      event.keycode = keycode;
      event.serial = 0;
      event.send_event = False;
      event.display = dpy;
      event.x = event.y = event.x_root = event.y_root = 0;
      event.time = CurrentTime;
      event.same_screen = True;
      event.subwindow = None;
      event.window = current_focus;
      event.root = root_win;
      event.state = modifiers;
      XSendEvent(dpy,current_focus,False,0,(XEvent*)&event);
      XSync(dpy,False);
    pthread_mutex_unlock(&x11mutex);
  }
}

void record_complete(unsigned long keystate)
{
    char tmpstr[1024];
    int gkey = map_gkey(keystate);

    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,mled_state);

    if(!rec_index) // nothing recorded - delete prior recording
        memset(mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress,0,sizeof(keysequence_t));
    else
        memcpy(mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress, &current_recording, sizeof(keysequence_t));

    mstates[mkey_state]->gkeys[gkey].keysequence.record_steps=rec_index;

    memset(canvas->buffer,0,G15_BUFFER_LEN);
    if(rec_index){
        strcpy(tmpstr,"For key ");
        strcat(tmpstr,gkeystring[map_gkey(keystate)]);
        g15r_renderString (canvas, (unsigned char *)"Recording", 0, G15_TEXT_LARGE, 80-((strlen("Recording")/2)*8), 4);
        g15r_renderString (canvas, (unsigned char *)"Complete", 0, G15_TEXT_LARGE, 80-((strlen("Complete")/2)*8), 18);

    }else{
        strcpy(tmpstr,"From Key ");
        strcat(tmpstr,gkeystring[map_gkey(keystate)]);
        g15r_renderString (canvas, (unsigned char *)"Macro", 0, G15_TEXT_LARGE, 80-((strlen("Macro")/2)*8), 4);
        g15r_renderString (canvas, (unsigned char *)"Deleted", 0, G15_TEXT_LARGE, 80-((strlen("Deleted")/2)*8), 18);
    }
    g15r_renderString (canvas, (unsigned char *)tmpstr, 0, G15_TEXT_LARGE, 80-((strlen(tmpstr)/2)*8), 32);
    
    g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);

    memset(recstring,0,strlen((char*)recstring));
    rec_index = 0;
}

int calc_mkey_offset() {
        int mkey_offset=0;
        switch(mkey_state){
          case 0:
            mkey_offset = 0;
            break;
          case 1:
            mkey_offset = 18;
            break;
          case 2:
            mkey_offset = 36;
            break;
          default:
            mkey_offset=0;
        }
      return mkey_offset;
}

void macro_playback(unsigned long keystate)
{
    int i = 0;
    KeySym key;
    int keyevent;
    int gkey = map_gkey(keystate);
    if(gkey<0)
      return;
    
    /* if no macro has been recorded for this key, send the g15daemon default keycode */
    if(mstates[mkey_state]->gkeys[gkey].keysequence.record_steps==0){
        int mkey_offset=0;

        mkey_offset = calc_mkey_offset();

        pthread_mutex_lock(&x11mutex);
        keyevent=XKeysymToKeycode(dpy, gkeydefaults[gkey+mkey_offset]);
        pthread_mutex_unlock(&x11mutex);

        fake_keyevent(keyevent,1,None);
        fake_keyevent(keyevent,0,None);
        return;
    }

    for(i=0;i<mstates[mkey_state]->gkeys[gkey].keysequence.record_steps;i++){

        fake_keyevent(mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].keycode,
                          mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].pressed,
                          mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].modifiers);

        pthread_mutex_lock(&x11mutex);
        key = XKeycodeToKeysym(dpy,mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].keycode,0);
        pthread_mutex_unlock(&x11mutex);
        
        switch (key) {
            case XK_Control_L:
            case XK_Control_R:
            case XK_Meta_L:
            case XK_Meta_R:
            case XK_Alt_L:
            case XK_Alt_R:
            case XK_Super_L:
            case XK_Super_R:
            case XK_Hyper_L:
            case XK_Hyper_R:
             usleep(mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].time_ms*1000); 
              break;
            default:
             usleep(1000);
        }
    }
}

/* WARNING:  uses global mkey state */
void dump_macro(unsigned int gkey)
{
    int i = 0;
    KeySym key;
    if(gkey>17)
      return;
    printf("Key %s: ",gkeystring[gkey]);
    /* if no macro has been recorded for this key, dump the g15daemon default keycode */
    if(mstates[mkey_state]->gkeys[gkey].keysequence.record_steps==0){
        int mkey_offset=0;
        mkey_offset = calc_mkey_offset();
        printf("\t%s\n",XKeysymToString(gkeydefaults[gkey+mkey_offset]));
        return;
    }
    printf("\n");
    for(i=0;i<mstates[mkey_state]->gkeys[gkey].keysequence.record_steps;i++){
        key = XKeycodeToKeysym(dpy,mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].keycode,0);
        printf("\t%s %s\n",XKeysymToString(key),mstates[mkey_state]->gkeys[gkey].keysequence.recorded_keypress[i].pressed?"Down":"Up");
    }
}

void change_keymap(int offset){
    int i=0,j=0;
    pthread_mutex_lock(&x11mutex);
    for(i=offset;i<offset+18;i++,j++)
    {
      KeySym newmap[1];
      newmap[0]=gkeydefaults[i];
      XChangeKeyboardMapping (dpy, gkeycodes[j], 1, newmap, 1);
    }
    XFlush(dpy);
    pthread_mutex_unlock(&x11mutex);

}

/* ensure that the multimedia keys are configured */
void configure_mmediakeys(){

   KeySym newmap[1];
   int i=0;
   pthread_mutex_lock(&x11mutex);
   for(i=0;i<6;i++){
     newmap[0]=mmedia_defaults[i];
     XChangeKeyboardMapping (dpy, mmedia_codes[i], 1, newmap, 1);
   }
   XFlush(dpy);
   pthread_mutex_unlock(&x11mutex);
   
}

void handle_mkey_switch(unsigned int mkey) {
    int mkey_offset = 0;  
    switch(mkey) {
      case G15_KEY_M1:
        mled_state=G15_LED_M1;
        mkey_state=0;  
        break;
      case G15_KEY_M2:
        mled_state=G15_LED_M2;  
        mkey_state=1;
        break;
      case G15_KEY_M3:
        mled_state=G15_LED_M3;  
        mkey_state=2;
        break;
    }
    mkey_offset = calc_mkey_offset();
    recording = 0;
    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,mled_state);
    change_keymap(mkey_offset);
}

void *Lkeys_thread() {
    unsigned long keystate = 0;
    struct pollfd fds;
    char ver[5];
    int foo = 0;
    strncpy(ver,G15DAEMON_VERSION,3);
    float g15v;
    sscanf(ver,"%f",&g15v);

    while(!leaving){

        /* g15daemon series 1.2 need key request packets */
        if((g15v*10)<=18) {
            keystate = g15_send_cmd (g15screen_fd, G15DAEMON_GET_KEYSTATE, foo);
        } else {
            fds.fd = g15screen_fd;
            fds.events = POLLIN;
            fds.revents=0;
            keystate=0;
            if ((poll(&fds, 1, 5000)) > 0) {
                read (g15screen_fd, &keystate, sizeof (keystate));    
            }
        }

        if (keystate!=0)
        {
            switch (keystate)
            {
                case G15_KEY_L5:{
                    int fg_check = g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, foo);
                    if(fg_check)
                      leaving = 1;
                    break;
                }
                case G15_KEY_MR:
                    if(0==g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, foo)){
                        usleep(1000);
                        g15_send_cmd (g15screen_fd, G15DAEMON_SWITCH_PRIORITIES, foo);         
                    }
                    usleep(1000);
                    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS, G15_LED_MR | mled_state);
                    g15r_initCanvas (canvas);
                    g15r_renderString (canvas, (unsigned char *)"Recording", 0, G15_TEXT_LARGE, 80-((strlen("Recording")/2)*8), 1);
                    g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);

                    recording = 1;
                    pthread_mutex_lock(&x11mutex);
                    XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
                    pthread_mutex_unlock(&x11mutex);
                    memset(&current_recording,0,sizeof(current_recording));
                    break;
                case G15_KEY_M1:
                    handle_mkey_switch(G15_KEY_M1);
                    break;
                case G15_KEY_M2:
                    handle_mkey_switch(G15_KEY_M2);
                    break;
                case G15_KEY_M3:
                    handle_mkey_switch(G15_KEY_M3);
                    break;
                default:
                    if(keystate >=G15_KEY_G1 && keystate <=G15_KEY_G18){
                        if(recording==1){
                            record_complete(keystate);
                            recording = 0;
                             pthread_mutex_lock(&x11mutex);
                             XUngrabKeyboard(dpy,CurrentTime);
                             XFlush(dpy);
                             pthread_mutex_unlock(&x11mutex);
                        } else {
                            macro_playback(keystate);
                        }
                    }
                    break;
            }
            keystate = 0;
        }
    }
    return NULL;
}

unsigned int g15daemon_gettime_ms(){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (tv.tv_sec*1000+tv.tv_usec/1000);
}

void xkey_handler(XEvent *event) {

    static unsigned long lasttime;
    unsigned char keytext[256];
    unsigned int keycode = event->xkey.keycode;
    int press = True;

    if(event->type==KeyRelease){ // we only do keyreleases for some keys
      pthread_mutex_lock(&x11mutex);
        KeySym key = XKeycodeToKeysym(dpy, keycode, 0);
      pthread_mutex_unlock(&x11mutex);
        switch (key) {
            case XK_Shift_L:
            case XK_Shift_R:
            case XK_Control_L:
            case XK_Control_R:
            case XK_Caps_Lock:
            case XK_Shift_Lock:
            case XK_Meta_L:
            case XK_Meta_R:
            case XK_Alt_L:
            case XK_Alt_R:
            case XK_Super_L:
            case XK_Super_R:
            case XK_Hyper_L:
            case XK_Hyper_R:
            default: 
                press = False;
        }
    }
    if(recording){
        current_recording.recorded_keypress[rec_index].keycode = keycode;
        current_recording.recorded_keypress[rec_index].pressed = press;
        current_recording.recorded_keypress[rec_index].modifiers = event->xkey.state;
        if(rec_index==0)
            current_recording.recorded_keypress[rec_index].time_ms=0;
        else
            current_recording.recorded_keypress[rec_index].time_ms=g15daemon_gettime_ms() - lasttime;
        if(rec_index < MAX_KEYSTEPS) {
          rec_index++;
        /* now the default stuff */
        pthread_mutex_lock(&x11mutex);        
          XUngrabKeyboard(dpy,CurrentTime);
       pthread_mutex_unlock(&x11mutex);

        fake_keyevent(keycode,press,event->xkey.state);
       
        pthread_mutex_lock(&x11mutex);
        XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
        XFlush(dpy);
        strcpy((char*)keytext,XKeysymToString(XKeycodeToKeysym(dpy, keycode, 0)));
        pthread_mutex_unlock(&x11mutex);        
        if(0==strcmp((char*)keytext,"space"))
            strcpy((char*)keytext," ");
        if(0==strcmp((char*)keytext,"period"))
            strcpy((char*)keytext,".");
        if(press==True){
          strcat((char*)recstring,(char*)keytext);
          g15r_renderString (canvas, (unsigned char *)recstring, 0, G15_TEXT_MED, 80-((strlen((char*)recstring)/2)*5), 22);
          g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);
        }
      } else {
          pthread_mutex_lock(&x11mutex);        
            XUngrabKeyboard(dpy,CurrentTime);
          pthread_mutex_unlock(&x11mutex);
          recording = 0;
          rec_index = 0;
          
        }

    }else
        rec_index=0;

        lasttime = g15daemon_gettime_ms();
}

static void* xevent_thread()
{
    XEvent event;
    long event_mask = KeyPressMask|KeyReleaseMask|FocusChangeMask|SubstructureNotifyMask;
    int retval=0;
    pthread_mutex_lock(&x11mutex);
    XSelectInput(dpy, root_win, event_mask);
    pthread_mutex_unlock(&x11mutex);
    while(!leaving){
        pthread_mutex_lock(&x11mutex);
        memset(&event,0,sizeof(XEvent));
        retval = XCheckMaskEvent(dpy, event_mask, &event);
        pthread_mutex_unlock(&x11mutex);
        if(retval == True){
            switch(event.type) {
                case KeyPress:
                    xkey_handler(&event);
                    break;
                case KeyRelease: 
                    xkey_handler(&event);
                    break;
                case FocusIn: 
                case FocusOut:
                case EnterNotify:
                case LeaveNotify:
                case MapNotify:
                case UnmapNotify:
                case MapRequest:
                case ConfigureNotify:
                case CreateNotify:
                case DestroyNotify:
                    break;
                case ReparentNotify: {
                        if(recording) {
                            pthread_mutex_lock(&x11mutex);
                            XGrabKeyboard(dpy, root_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
                            XFlush(dpy);
                            pthread_mutex_unlock(&x11mutex);
                        }
                        break;
                    }
                default:
                    printf("Unhandled event (%i) received\n",event.type);
            }
        }else 
        usleep(25000);
    }
    return NULL;
}

int myx_error_handler(Display *dpy, XErrorEvent *err){
    return 0;
}

void g15macro_sighandler(int sig) {
    switch(sig){
         case SIGINT:
         case SIGTERM:
         case SIGQUIT:
         case SIGPIPE:
              leaving = 1;
               break;
    }
}

int main(int argc, char **argv)
{
    pthread_t Xkeys;
    pthread_t Lkeys;
    int xtest_major_version = 0;
    int xtest_minor_version = 0;
    struct sigaction new_action;
    int dummy=0,i=0;
    unsigned char user[256];
    struct passwd *username;
    char configpath[1024];
    char splashpath[1024];
    unsigned int dump = 0;
    
    strncpy(configpath,getenv("HOME"),1024);

    memset(user,0,256);
    for(i=0;i<argc;i++){
        if (!strncmp(argv[i], "-u",2) || !strncmp(argv[i], "--user",6)) {
           if(argv[i+1]!=NULL){
             strncpy((char*)user,argv[i+1],128);
             i++;
           }
        }

        if (!strncmp(argv[i], "-d",2) || !strncmp(argv[i], "--dump",6)) {
          dump = 1;
        }

        if (!strncmp(argv[i], "-v",2) || !strncmp(argv[i], "--version",9)) {
          printf("G15Macro version %s\n\n",PACKAGE_VERSION);
          exit(0);
        }
    }

    if(strlen((char*)user)){
      username = getpwnam((char*)user);
        if (username==NULL) {
            username = getpwuid(geteuid());
            printf("BEWARE: running as effective uid %i\n",username->pw_uid);
        }
        else {
           if(0==setuid(username->pw_uid)) {
             setgid(username->pw_gid);
             strncpy(configpath,username->pw_dir,1024);
             printf("running as user %s\n",username->pw_name);
           }
           else
             printf("Unable to run as user \"%s\" - you dont have permissions for that.\nRunning as \"%s\"\n",username->pw_name,getenv("USER"));
        }
    }
                                                                        
    strncat(configpath,"/.g15macro",1024-strlen(configpath));
    mkdir(configpath,0777);
    strncat(configpath,"/g15macro-data",1024-strlen(configpath));

    config_fd = open(configpath,O_RDONLY|O_SYNC);

    mstates[0] = malloc(sizeof(mstates_t));
    mstates[1] = (mstates_t*)malloc(sizeof(mstates_t));
    mstates[2] = (mstates_t*)malloc(sizeof(mstates_t));

    if(config_fd) {
      read(config_fd,mstates[0],sizeof(mstates_t));
      read(config_fd,mstates[1],sizeof(mstates_t));
      read(config_fd,mstates[2],sizeof(mstates_t));
      close(config_fd);
    }else {
      memset(mstates[0],0,sizeof(mstates));
      memset(mstates[1],0,sizeof(mstates));
      memset(mstates[2],0,sizeof(mstates));
    }

    do {
      dpy = XOpenDisplay(getenv("DISPLAY"));
      if (!dpy) {
        printf("Unable to open display %s - retrying\n",getenv("DISPLAY"));
        sleep(2);
        }
    }while(!dpy);
   
    do {
      if((g15screen_fd = new_g15_screen(G15_G15RBUF))<0){
        printf("Sorry, cant connect to the G15daemon - retrying\n");
        sleep(2);
      }
    }while(g15screen_fd<0);

    if(dump){
        int n;
        printf("G15Macro Dumping Codes...");
        for(n=0;n<3;n++){
          mkey_state=n;
          printf("\n\nCodes for MKey %i\n",n+1);
          for(i=0;i<18;i++)
             dump_macro(i);
          }
          exit(0);
    }
    
    g15_send_cmd (g15screen_fd,G15DAEMON_KEY_HANDLER, dummy);
    usleep(1000);
    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,mled_state);
    usleep(1000);
    canvas = (g15canvas *) malloc (sizeof (g15canvas));
    if (canvas != NULL) {
        g15r_initCanvas(canvas);
    } else {
        printf("Unable to initialise the libg15render canvas\nExiting\n");
        return 1;
    }


    root_win = DefaultRootWindow(dpy);
    if (!root_win) {
        printf("Cant find root window\n");
        return 1;
    }

    have_xtest = False;
#ifdef USE_XTEST
#ifdef HAVE_X11_EXTENSIONS_XTEST_H    
    have_xtest = XTestQueryExtension(dpy, &dummy, &dummy, &xtest_major_version, &xtest_minor_version);
#endif
#endif

    if(have_xtest == False || xtest_major_version < 2 || (xtest_major_version <= 2 && xtest_minor_version < 2))
    {
        printf("XTEST extension not supported\nReverting to XSendEvent for keypress emulation\n");
    }

    /* completely ignore errors and carry on */
    XSetErrorHandler(myx_error_handler);
    configure_mmediakeys();
    change_keymap(0);
    XFlush(dpy);
    
    new_action.sa_handler = g15macro_sighandler;
    new_action.sa_flags = 0;
    sigaction(SIGINT, &new_action, NULL);
    sigaction(SIGQUIT, &new_action, NULL);
    sigaction(SIGTERM, &new_action, NULL);
    sigaction(SIGPIPE, &new_action, NULL);
    
    snprintf((char*)splashpath,1024,"%s/%s",DATADIR,"g15macro/splash/g15macro.wbmp");
    g15r_loadWbmpSplash(canvas, splashpath);
    g15_send(g15screen_fd,(char *)canvas->buffer,G15_BUFFER_LEN);
    usleep(1000);
    pthread_mutex_init(&x11mutex,NULL);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    int thread_policy=SCHED_FIFO;
    pthread_attr_setschedpolicy(&attr,thread_policy);       
    pthread_attr_setstacksize(&attr,32*1024); /* set stack to 32k - dont need 8Mb !! */

    pthread_create(&Xkeys, &attr, xevent_thread, NULL);
    pthread_create(&Lkeys, &attr, Lkeys_thread, NULL);
    do{
        if(display_timeout<-1)
          display_timeout=-1;
        else
          display_timeout--;

        if(recording)
            display_timeout=500;
        if(display_timeout<=0){
            int fg_check = g15_send_cmd (g15screen_fd, G15DAEMON_IS_FOREGROUND, dummy);
            if (fg_check==1) { // foreground 
                    g15_send_cmd (g15screen_fd, G15DAEMON_SWITCH_PRIORITIES, dummy);
            	    g15r_loadWbmpSplash(canvas, splashpath);
            }

           usleep(500*1000);
        }
    }while(!usleep(1000) &&  !leaving);
    g15_send_cmd (g15screen_fd,G15DAEMON_MKEYLEDS,0);
    usleep(1000);
    if(recording){
        recording = 0;
        XUngrabKeyboard(dpy,CurrentTime);
    }
    config_fd = open(configpath,O_CREAT|O_WRONLY|O_SYNC,0600);
    write(config_fd,mstates[0],sizeof(mstates_t));
    write(config_fd,mstates[1],sizeof(mstates_t));
    write(config_fd,mstates[2],sizeof(mstates_t));

    close(config_fd);
    pthread_join(Xkeys,NULL);
    pthread_join(Lkeys,NULL);
    pthread_mutex_destroy(&x11mutex);
    /* revert the keymap to g15daemon default on exit */
    change_keymap(0);

    XCloseDisplay(dpy);    
    close(g15screen_fd);
    return 1;
}
