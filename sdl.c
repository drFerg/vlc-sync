/* libSDL and libVLC sample code
* Copyright © 2008 Sam Hocevar <sam@zoy.org>
* license: [http://en.wikipedia.org/wiki/WTFPL WTFPL] */

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <assert.h>

#include <SDL/SDL.h>
#include <SDL/SDL_mutex.h>

#include <vlc/vlc.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/if_ether.h> /* includes net/ethernet.h */
#include <netinet/ether.h>  /* for ethernet ascii */
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>

#define WIDTH 1280
#define HEIGHT 720

#define VIDEOWIDTH 1280
#define VIDEOHEIGHT 720
#define PORT "3000"
#define MAXBUFLEN 100
#define RECVFLAGS 0

#define PLAYING 0
#define PAUSED  1

struct ctx
{
  SDL_Surface *surf;
  SDL_mutex *mutex;
  int pause;
};

typedef struct media_state {
  int state;
  int changed;
  libvlc_time_t t;
} MediaState;

struct connectionData{
  struct sockaddr_in addr;
  int fd;
};

struct socketAddr{
  struct sockaddr_in addr;
  int fd;
};

struct packet {
  uint8_t state;
};

MediaState ms;

void send_packet(struct socketAddr *client, struct packet* packet, size_t len){
  int numbytes;
  int fd = client->fd;
  if ((numbytes = sendto(fd, packet, len, 0,
   (struct sockaddr *)&(client->addr), sizeof(struct sockaddr))) == -1) {
    perror("talker: sendto");
    exit(1);
  }
  printf("Sent data\n");
}

void send_state(struct socketAddr *client, uint8_t state) {
  struct packet *p = (struct packet *) malloc(sizeof(struct packet));
  p->state = state;
  send_packet(client, p, sizeof(struct packet));
}


static void *lock(void *data, void **p_pixels)
{
  struct ctx *ctx = data;

  SDL_LockMutex(ctx->mutex);
  SDL_LockSurface(ctx->surf);
  *p_pixels = ctx->surf->pixels;
return NULL; /* picture identifier, not needed here */
}

static void unlock(void *data, void *id, void *const *p_pixels)
{
  struct ctx *ctx = data;

/* VLC just rendered the video, but we can also render stuff */
  uint16_t *pixels = *p_pixels;
  int x, y;
  if (ctx->pause){
    for(y = 10; y < 40; y++){
      for(x = 10; x < 40; x++){
        if(x < 13 || y < 13 || x > 36 || y > 36)
          pixels[y * VIDEOWIDTH + x] = 0xffff;
        else
          pixels[y * VIDEOWIDTH + x] = 0x0;
      }
    }
  }
  SDL_UnlockSurface(ctx->surf);
  SDL_UnlockMutex(ctx->mutex);

assert(id == NULL); /* picture identifier, not needed here */
}

static void display(void *data, void *id)
{
/* VLC wants to display the video */
  (void) data;
  assert(id == NULL);
}

int vlc_main(char *filepath, struct socketAddr *conn)
{
  libvlc_instance_t *libvlc;
  libvlc_media_t *m;
  libvlc_media_player_t *mp;
  char const *vlc_argv[] = {
    //"--no-audio", /* skip any audio track */
    "--no-xlib", /* tell VLC to not use Xlib */
  };
  int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);

  SDL_Surface *screen, *empty;
  SDL_Event event;
  SDL_Rect rect;
  int done = 0, action = 0, pause = 0, n = 0;

  struct ctx ctx;
  ctx.pause = 1;


  /*
  *  Initialise libSDL
  */
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTTHREAD) == -1) {
    printf("cannot initialize SDL\n");
    return EXIT_FAILURE;
  }

  empty = SDL_CreateRGBSurface(SDL_SWSURFACE, VIDEOWIDTH, VIDEOHEIGHT,
   32, 0, 0, 0, 0);
  ctx.surf = SDL_CreateRGBSurface(SDL_SWSURFACE, VIDEOWIDTH, VIDEOHEIGHT,
    16, 0x001f, 0x07e0, 0xf800, 0);

  ctx.mutex = SDL_CreateMutex();
  
  int options = SDL_ANYFORMAT | SDL_HWSURFACE | SDL_DOUBLEBUF;

  screen = SDL_SetVideoMode(WIDTH, HEIGHT, 0, options);
  if(!screen) {
    printf("cannot set video mode\n");
    return EXIT_FAILURE;
  }

  /*
  *  Initialise libVLC
  */
  libvlc = libvlc_new(vlc_argc, vlc_argv);
  m = libvlc_media_new_path(libvlc, filepath);
  mp = libvlc_media_player_new_from_media(m);
  libvlc_media_release(m);

  libvlc_video_set_callbacks(mp, lock, unlock, display, &ctx);
  libvlc_video_set_format(mp, "RV16", VIDEOWIDTH, VIDEOHEIGHT, VIDEOWIDTH*2);
  

  /*
  *  Main loop
  */
  rect.w = 0;
  rect.h = 0;

  while(!done)
  { 
    action = 0;

    /* Keys: enter (fullscreen), space (pause), escape (quit) */
    while(SDL_PollEvent(&event)) { 
      switch(event.type) {
        case SDL_QUIT:
        done = 1;
        break;
        case SDL_KEYDOWN:
        action = event.key.keysym.sym;
        break;
      }
    }

    switch(action) {
      case SDLK_ESCAPE: 
        done = 1;
        break;
      case SDLK_RETURN:
        options ^= SDL_FULLSCREEN;
        screen = SDL_SetVideoMode(WIDTH, HEIGHT, 0, options);
        break;
      case ' ':
        ctx.pause = !ctx.pause;
        if (ctx.pause) libvlc_media_player_pause(mp);
        else libvlc_media_player_play(mp);
        send_state(conn, ctx.pause);
        break;
    }
    if (ms.changed) {
      if (ctx.pause == ms.state){
        printf("Already in that state\n");
        ms.changed = 0;
      }
      else {
        ctx.pause = ms.state;
        ms.changed = 0;
        if (ctx.pause) libvlc_media_player_pause(mp);
        else libvlc_media_player_play(mp);
      }
    }

    rect.x = (int)((1. + .5 * sin(0.03 * n)) * (WIDTH - VIDEOWIDTH) / 2);
    rect.y = (int)((1. + .5 * cos(0.03 * n)) * (HEIGHT - VIDEOHEIGHT) / 2);

    if(!pause)
      n++;

    /* Blitting the surface does not prevent it from being locked and
    * written to by another thread, so we use this additional mutex. */
    SDL_LockMutex(ctx.mutex);
    SDL_BlitSurface(ctx.surf, NULL, screen, NULL);
    SDL_UnlockMutex(ctx.mutex);

    SDL_Flip(screen);
    SDL_Delay(10);

    SDL_BlitSurface(empty, NULL, screen, NULL);
  }

  /*
  * Stop stream and clean up libVLC
  */
  libvlc_media_player_stop(mp);
  libvlc_media_player_release(mp);
  libvlc_release(libvlc);

  /*
  * Close window and clean up libSDL
  */
  SDL_DestroyMutex(ctx.mutex);
  SDL_FreeSurface(ctx.surf);
  SDL_FreeSurface(empty);

  SDL_Quit();

  return 0;
}



int create_server_socket(){
  int fd;
  int status;
  struct addrinfo hints, *servinfo, *result;
  printf(">> Creating UDP socket on port %s...", PORT);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; // set to AF_INET to force IPv4
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE; // use my IP

  if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "error!\ngetaddrinfo: %s\n", gai_strerror(status));
    return -1;
  }

  // loop through all the results and bind to the first we can
  for(result = servinfo; result != NULL; result = result->ai_next) {
    if ((fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) == -1) {
      continue;
    }

    if (bind(fd, result->ai_addr, result->ai_addrlen) == -1) {
      close(fd);
      continue;
    }
    break;
  }

  if (result == NULL) {
    fprintf(stderr, "error!\nlistener: failed to bind socket\n");
    return 2;
  }

  freeaddrinfo(servinfo);
  printf("done!\n");
  return fd;
}

int create_client_socket(){
  int fd;

  if ((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    perror(">> Client Socket");
  }
  return fd;
}

//-------------------------------------
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void listener(struct socketAddr *conn){
  struct sockaddr_storage src_addr;
  char s[INET6_ADDRSTRLEN];
  char buf[MAXBUFLEN];
  int numbytes;
  int addr_len = sizeof(struct sockaddr_storage);
  printf("L: Waiting for sniffer to init...\n");

  printf("L: Waiting to recvfrom...\n");
  while(1){
    if ((numbytes = recvfrom(conn->fd, buf, MAXBUFLEN-1 , RECVFLAGS, 
                             (struct sockaddr *)&src_addr, &addr_len)) == -1) {
      perror("recvfrom");
      exit(1);
    }
    printf("L: Got packet from %s\n",
      inet_ntop(src_addr.ss_family, get_in_addr((struct sockaddr *)&src_addr), s, sizeof s));
    printf("L: Packet is %d bytes long\n", numbytes);
    buf[numbytes] = '\0';
    printf("L: Packet contains \"%d\"\n", buf[0]);
    ms.state = buf[0];
    ms.changed = 1;
  }
  close(conn->fd);
}

int main(int argc, char *argv[]) {
  pthread_t sniffer_thread, listener_thread;
  struct sockaddr_in client_addr;
  struct socketAddr conn;
  struct socketAddr sender;


  int fd;
  int sniffer_active = 0;
  int listener_active = 0;
  char *client_addr_str;
  char *filepath;
  char c;
  while ((c = getopt(argc, argv, "a:f:")) != -1) {
    switch (c) {
      case 'a': 
        client_addr_str = optarg; 
        break;
      case 'f':
        filepath = optarg;
        break;
      case '?':
        if (optopt == 'a')
          fprintf(stderr, "Option -%c requires an address as an argument\n", optopt);
        else if (optopt == 'f')
          fprintf(stderr, "Option %c requires a filepath as an argument\n", optopt);
        else {
          fprintf(stderr, "Unknown option %c\n", optopt);
          return 0;
        }
    }
  }

  if (argc > 1){
    conn.addr.sin_family = AF_INET;
    inet_pton(AF_INET, client_addr_str, &(conn.addr.sin_addr));
    inet_pton(AF_INET, client_addr_str, &(sender.addr.sin_addr));
    conn.addr.sin_port = htons(atoi(PORT));
    sender.addr.sin_port = htons(atoi(PORT));
    printf("%s\n", client_addr_str);
  }
  else{
    printf("Usage: vlcsync <friend's IP> \n");
    return 0;
  }
  printf(">> Initilising...\n");
  sender.fd = create_client_socket();
  conn.fd = create_server_socket();
  if (conn.fd == -1 || sender.fd == -1){
    printf("OH NOES\n");
  }
  printf("---------------\n---------------\n");
  pthread_create(&listener_thread, NULL, (void *)&listener, &conn);
  //pthread_join(sniffer_thread, NULL);
  vlc_main(filepath, &sender);
}
 
