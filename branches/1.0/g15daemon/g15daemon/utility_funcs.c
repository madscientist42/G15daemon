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
    
    (c) 2006 Mike Lampard, Philip Lawatsch, and others
    
    This daemon listens on localhost port 15550 for client connections,
    and arbitrates LCD display.  Allows for multiple simultaneous clients.
    Client screens can be cycled through by pressing the 'L1' key.
*/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <libdaemon/daemon.h>
#include "g15daemon.h"
#include "libg15.h"

extern int leaving;

/* handy function from xine_utils.c */
void *g15_xmalloc(size_t size) {
    void *ptr;

    /* prevent xmalloc(0) of possibly returning NULL */
    if( !size )
        size++;

    if((ptr = calloc(1, size)) == NULL) {
        daemon_log(LOG_WARNING, "g15_xmalloc() failed: %s.\n", strerror(errno));
        return NULL;
    }
    return ptr;
}


lcd_t * create_lcd () {

    lcd_t *lcd = g15_xmalloc (sizeof (lcd_t));
    
    lcd->max_x = LCD_WIDTH;
    lcd->max_y = LCD_HEIGHT;

    return (lcd);
}

void quit_lcd (lcd_t * lcd) {
    
    free (lcd);
}


/* set a pixel in a libg15 buffer */
static void set_g15_pixel(unsigned char *g15buffer, unsigned int x, unsigned int y, unsigned char val)
{
    unsigned int curr_row = y;
    unsigned int curr_col = x;
    
    unsigned int pixel_offset = curr_row * LCD_WIDTH + curr_col;
    unsigned int byte_offset = pixel_offset / 8;
    unsigned int bit_offset = 7-(pixel_offset % 8);

    if (val)
        g15buffer[byte_offset] = g15buffer[byte_offset] | 1 << bit_offset;
    else
        g15buffer[byte_offset] = g15buffer[byte_offset]  &  ~(1 << bit_offset);

}

/* convert our 1byte-per-pixel buffer into a libg15 buffer - FIXME we should be writing directly into a libg15buffer 
    to save copying 
*/
void write_buf_to_g15(lcd_t *lcd)
{
    int x,y;
    unsigned char g15buffer[7040];

    for(x=0;x<LCD_WIDTH;x++)
        for(y=0;y<LCD_HEIGHT;y++)
            set_g15_pixel(g15buffer, x, y, lcd->buf[(y*LCD_WIDTH)+x]);

    pthread_mutex_lock(&g15lib_mutex);
    writePixmapToLCD(g15buffer);
    pthread_mutex_unlock(&g15lib_mutex);
    return;
}

void setpixel (lcd_t * lcd, int x1, int y1, int colour) {

    if(lcd->buf==NULL) return;

    if (x1 < 0)
        return;
    if (x1 > lcd->max_x - 1)
        return;
    if (y1 < 0)
        return;
    if (y1 > lcd->max_y - 1)
        return;

    lcd->buf[x1 + (LCD_WIDTH * y1)] = colour;

    return;
}


static int abs2 (int value) {

    if (value < 0)
        return -value;
    else
        return value;
}


void cls (lcd_t * lcd, int colour) {
    memset (lcd->buf, colour, lcd->max_x * lcd->max_y);
}

void line (lcd_t * lcd, int x1, int y1, int x2, int y2, int colour) {

    int d, sx, sy, dx, dy;
    unsigned int ax, ay;

    x1 = x1 - 1;
    y1 = y1 - 1;
    x2 = x2 - 1;
    y2 = y2 - 1;

    dx = x2 - x1;
    ax = abs2 (dx) << 1;
    if (dx < 0)
        sx = -1;
    else
        sx = 1;

    dy = y2 - y1;
    ay = abs2 (dy) << 1;
    if (dy < 0)
        sy = -1;
    else
        sy = 1;

    /* set the pixel */
    setpixel (lcd, x1, y1, colour);

    if (ax > ay)
    {
        d = ay - (ax >> 1);
        while (x1 != x2)
        {
            if (d >= 0)
            {
                y1 += sy;
                d -= ax;
            }
            x1 += sx;
            d += ay;
            setpixel (lcd, x1, y1, colour);

        }
    }
    else
    {
        d = ax - (ay >> 1);
        while (y1 != y2)
        {
            if (d >= 0)
            {
                x1 += sx;
                d -= ay;
            }
            y1 += sy;
            d += ax;
            setpixel (lcd, x1, y1, colour);
        }
    }
}


void rectangle (lcd_t * lcd, int x1, int y1, int x2, int y2, int filled, int colour) {

    int y;

    if (x1 != x2 && y1 != y2)
    {
        if (!filled)
        {
            line (lcd, x1, y1, x2, x1, colour);
            line (lcd, x1, y1, x1, y2, colour);
            line (lcd, x1, y2, x2, y2, colour);
            line (lcd, x2, y1, x2, y2, colour);
        }
        else
        {
            for (y = y1; y <= y2; y++)
            {
                memset (lcd->buf + x1 + (lcd->max_x * y), colour, x2 - x1);
            }
        }
    }
}


void draw_bignum (lcd_t * lcd, int x1, int y1, int x2, int y2, int colour, int num) {
    x1 += 2;
    x2 -= 2;

    switch(num){
        case 45: 
            rectangle (lcd, x1, y1+((y2/2)-2), x2, y1+((y2/2)+2), 1, BLACK);
            break;
        case 46:
            rectangle (lcd, x2-5, y2-5, x2, y2 , 1, BLACK);
            break;
        case 48:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1 +5, y1 +5, x2 -5, y2 - 6, 1, WHITE);
            break;
        case 49: 
            rectangle (lcd, x2-5, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1, y1, x2 -5, y2, 1, WHITE);
            break;
        case 50:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1, y1+5, x2 -5, y1+((y2/2)-3), 1, WHITE);
            rectangle (lcd, x1+5, y1+((y2/2)+3), x2 , y2-6, 1, WHITE);
            break;
        case 51:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1, y1+5, x2 -5, y1+((y2/2)-3), 1, WHITE);
            rectangle (lcd, x1, y1+((y2/2)+3), x2-5 , y2-6, 1, WHITE);
            break;
        case 52:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1, y1+((y2/2)+3), x2 -5, y2, 1, WHITE);
            rectangle (lcd, x1+5, y1, x2-5 , y1+((y2/2)-3), 1, WHITE);
            break;
        case 53:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1+5, y1+5, x2 , y1+((y2/2)-3), 1, WHITE);
            rectangle (lcd, x1, y1+((y2/2)+3), x2-5 , y2-6, 1, WHITE);
            break;
        case 54:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1+5, y1+5, x2 , y1+((y2/2)-3), 1, WHITE);
            rectangle (lcd, x1+5, y1+((y2/2)+3), x2-5 , y2-6, 1, WHITE);
            break;
        case 55:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1, y1+5, x2 -5, y2, 1, WHITE);
            break;
        case 56:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1+5, y1+5, x2-5 , y1+((y2/2)-3), 1, WHITE);
            rectangle (lcd, x1+5, y1+((y2/2)+3), x2-5 , y2-6, 1, WHITE);
            break;
        case 57:
            rectangle (lcd, x1, y1, x2, y2 , 1, BLACK);
            rectangle (lcd, x1+5, y1+5, x2-5 , y1+((y2/2)-3), 1, WHITE);
            rectangle (lcd, x1, y1+((y2/2)+3), x2-5 , y2, 1, WHITE);
            break;
        case 58: 
            rectangle (lcd, x2-5, y1+5, x2, y1+10 , 1, BLACK);
            rectangle (lcd, x2-5, y2-10, x2, y2-5 , 1, BLACK);
            break;

    }
}

/* initialise a new displaylist, and add an initial node at the tail (used for the clock) */
lcdlist_t *lcdlist_init () {
    
    lcdlist_t *displaylist = NULL;
    
    pthread_mutex_init(&lcdlist_mutex, NULL);
    pthread_mutex_lock(&lcdlist_mutex);
    
    displaylist = g15_xmalloc(sizeof(lcdlist_t));
    
    displaylist->head = g15_xmalloc(sizeof(lcdnode_t));
    
    displaylist->tail = displaylist->head;
    displaylist->current = displaylist->head;
    
    displaylist->head->lcd = create_lcd();
    displaylist->head->prev = displaylist->head;
    displaylist->head->next = displaylist->head;
    displaylist->head->list = displaylist;
    
    pthread_mutex_unlock(&lcdlist_mutex);
    return displaylist;
}

lcdnode_t *lcdnode_add(lcdlist_t **display_list) {
    
    lcdnode_t *new = NULL;
    
    pthread_mutex_lock(&lcdlist_mutex);
    
    new = g15_xmalloc(sizeof(lcdnode_t));
    new->prev = (*display_list)->current;
    new->next = NULL; 
    new->lcd = create_lcd();
    
    (*display_list)->current->next=new;
    (*display_list)->current = new;
    (*display_list)->head = new;
    (*display_list)->head->list = *display_list;
    
    pthread_mutex_unlock(&lcdlist_mutex);
    
    return new;
}

void lcdnode_remove (lcdnode_t *oldnode) {
    
    lcdlist_t **display_list = NULL;
    lcdnode_t **prev = NULL;
    lcdnode_t **next = NULL;
    
    pthread_mutex_lock(&lcdlist_mutex);
    
    display_list = &oldnode->list;
    prev = &oldnode->prev;
    next = &oldnode->next;
    
    quit_lcd(oldnode->lcd);
    
    if((*display_list)->current == oldnode)
        (*display_list)->current = oldnode->prev; 
    
    if(oldnode->next!=NULL){
        (*next)->prev = oldnode->prev;
    }else{
        (*prev)->next = NULL;
        (*display_list)->head = oldnode->prev;
    }

    free(oldnode);
    pthread_mutex_unlock(&lcdlist_mutex);
}

void lcdlist_destroy(lcdlist_t **displaylist) {
    
    int i = 0;
    
    while ((*displaylist)->head != (*displaylist)->tail) {
        i++;
        lcdnode_remove((*displaylist)->head);
    }
    
    if(i)
        daemon_log(LOG_INFO,"removed %i stray client nodes",i);
    
    free((*displaylist)->tail->lcd);
    free((*displaylist)->tail);
    free(*displaylist);
    
    pthread_mutex_destroy(&lcdlist_mutex);
}

/* Sleep routine (hackish). */
void pthread_sleep(int seconds) {
    pthread_mutex_t dummy_mutex;
    static pthread_cond_t dummy_cond = PTHREAD_COND_INITIALIZER;
    struct timespec timeout;

    /* Create a dummy mutex which doesn't unlock for sure while waiting. */
    pthread_mutex_init(&dummy_mutex, NULL);
    pthread_mutex_lock(&dummy_mutex);

    timeout.tv_sec = time(NULL) + seconds;
    timeout.tv_nsec = 0;

    pthread_cond_timedwait(&dummy_cond, &dummy_mutex, &timeout);

    /*    pthread_cond_destroy(&dummy_cond); */
    pthread_mutex_unlock(&dummy_mutex);
    pthread_mutex_destroy(&dummy_mutex);
}

/* millisecond sleep routine. */
int pthread_msleep(int milliseconds) {
    
    struct timespec timeout;
    if(milliseconds>999)
        milliseconds=999;
    timeout.tv_sec = 0;
    timeout.tv_nsec = milliseconds*1000000;

    return nanosleep (&timeout, NULL);
}


void lcdclock(lcd_t *lcd)
{
    unsigned int col = 0;
    unsigned int len=0;
    int narrows=0;
    int totalwidth=0;
    char buf[10];
    
    time_t currtime = time(NULL);
    
    if(lcd->ident < currtime - 60) {	
        cls(lcd,WHITE);
        memset(buf,0,10);
        strftime(buf,6,"%H:%M",localtime(&currtime));

        if(buf[0]==49) 
            narrows=1;

        len = strlen(buf); 

        if(narrows)
            totalwidth=(len*20)+(15);
        else
            totalwidth=len*20;

        for (col=0;col<len;col++) 
        {
            draw_bignum (lcd, (80-(totalwidth)/2)+col*20, 1,(80-(totalwidth)/2)+(col+1)*20, LCD_HEIGHT, BLACK, buf[col]);
        }
    
        lcd->ident = currtime;
    }
}


/* the client must send 6880 bytes for each lcd screen.  This thread will continue to copy data
 * into the clients LCD buffer for as long as the connection remains open. 
 * so, the client should open a socket, check to ensure that the server is a g15daemon,
 * and send multiple 6880 byte packets (1 for each screen update) 
 * once the client disconnects by closing the socket, the LCD buffer is 
 * removed and will no longer be displayed.
 */
void *lcd_client_thread(void *display) {

    lcdnode_t *g15node = display;
    lcd_t *client_lcd = g15node->lcd;
    int client_sock = client_lcd->connection;
    char helo[]=SERV_HELO;
    char *tmpbuf=g15_xmalloc(6880);
    
    if(g15_send(client_sock, (char*)helo, strlen(SERV_HELO))<0){
        goto exitthread;
    }
    /* check for requested buffer type.. we only handle pixel buffers atm */
    if(g15_recv(client_sock,tmpbuf,4)<4)
        goto exitthread;

    /* we will in the future handle txt buffers gracefully but for now we just hangup */
    if(tmpbuf[0]=='G'){
        while(!leaving) {
            int retval = g15_recv(client_sock,(char *)tmpbuf,6880);
            if(retval!=6880){
                break;
            }
            pthread_mutex_lock(&lcdlist_mutex);
            memcpy(client_lcd->buf,tmpbuf,6880);
            client_lcd->ident = random();
            pthread_mutex_unlock(&lcdlist_mutex);
        }
    }
exitthread:
    close(client_sock);
    free(tmpbuf);
    lcdnode_remove(display);
    return NULL;
}

/* poll the listening socket for connections, spawning new threads as needed to handle clients */
int g15_clientconnect (lcdlist_t **g15daemon, int listening_socket) {

    int conn_s;
    struct pollfd pfd[1];
    pthread_t client_connection;
    pthread_attr_t attr;
    lcdnode_t *clientnode;

    memset(pfd,0,sizeof(pfd));
    pfd[0].fd = listening_socket;
    pfd[0].events = POLLIN;


    if (poll(pfd,1,500)>0){

        if (!(pfd[0].revents & POLLIN)){
            return 0;
        }

        if ( (conn_s = accept(listening_socket, NULL, NULL) ) < 0 ) {
            if(errno==EWOULDBLOCK || errno==EAGAIN){
            }else{
                daemon_log(LOG_WARNING, "error calling accept()\n");
                return -1;
            }
        }

        clientnode = lcdnode_add(g15daemon);
        clientnode->lcd->connection = conn_s;

        memset(&attr,0,sizeof(pthread_attr_t));
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);

        if (pthread_create(&client_connection, &attr, lcd_client_thread, clientnode) != 0) {
            daemon_log(LOG_WARNING,"Couldnt create client thread.");
            if (close(conn_s) < 0 ) {
                daemon_log(LOG_WARNING, "error calling close()\n");
                return -1;
            }
        }
    }
    return 0;
}














