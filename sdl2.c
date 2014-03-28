// libSDL and libVLC sample code.
// License: [http://en.wikipedia.org/wiki/WTFPL WTFPL]

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <assert.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL_mutex.h"
//#include "SDL2/SDL_opengl.h"
#include "vlc/vlc.h"

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

#define PLAYING 1
#define PAUSED  0
struct context {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_mutex *mutex;
    int n;
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

// VLC prepares to render a video frame.
static void *lock(void *data, void **p_pixels) {

    struct context *c = (struct context *)data;

    int pitch;
    SDL_LockMutex(c->mutex);
    SDL_LockTexture(c->texture, NULL, p_pixels, &pitch);
    return NULL; // Picture identifier, not needed here.
}

// VLC just rendered a video frame.
static void unlock(void *data, void *id, void *const *p_pixels) {
    int i;
    struct context *c = (struct context *)data;

    uint16_t *pixels = (uint16_t *)*p_pixels;
    //We can also render stuff.
    int x, y;
    for(y = 10; y < 40; y++) {
        for(x = 10; x < 40; x++) {
            if(x < 13 || y < 13 || x > 36 || y > 36) {
                pixels[y * VIDEOWIDTH + x] = 0xffff;
            } else {
                // RV16 = 5+6+5 pixels per color, BGR.
                pixels[y * VIDEOWIDTH + x] = 0x02ff;
            }
        }
    }

    SDL_UnlockTexture(c->texture);
    SDL_UnlockMutex(c->mutex);
}

// VLC wants to display a video frame.
static void display(void *data, void *id) {
    //Data to be displayed
}

static void quit(int c) {
    SDL_Quit();
    exit(c);
}

int vlc_main(char *filepath, struct socketAddr *conn) {

    libvlc_instance_t *libvlc;
    libvlc_media_t *m;
    libvlc_media_player_t *mp;
    char const *vlc_argv[] = {

        //"--no-audio", // Don't play audio.
        //"--no-xlib", // Don't use Xlib.
        // Apply a video filter.
        //"--video-filter", "sepia",
        //"--sepia-intensity=200"
        "-v",
    };
    int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);

    SDL_Event event;
    int done = 0, action = 0, pause = 0, n = 0;

    pause = 1;
    struct context context;
    int nRenderDrivers = SDL_GetNumRenderDrivers();
    int i = 0;
    for (; i < nRenderDrivers; i++) {
        SDL_RendererInfo info;
        SDL_GetRenderDriverInfo(i, &info); //d3d
        printf("====info name %d: %s =====\n", i, info.name);
        printf("====max_texture_height %d =====\n",info.max_texture_height);
        printf("====max_texture_width %d =====\n", info.max_texture_width);

    }

    // Initialise libSDL.
    if(SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("Could not initialize SDL: %s.\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    // Create SDL graphics objects.
    SDL_Window * window = SDL_CreateWindow(
            "SyncPlayer",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            WIDTH, HEIGHT,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (!window) {
        fprintf(stderr, "Couldn't create window: %s\n", SDL_GetError());
        quit(3);
    }

    context.renderer = SDL_CreateRenderer(window, -1, 1);
    if (!context.renderer) {
        fprintf(stderr, "Couldn't create renderer: %s\n", SDL_GetError());
        quit(4);
    }
    // SDL_SetRenderDrawColor(context.renderer, 10, 10, 100, 255);
    // SDL_RenderClear(context.renderer);
    // SDL_RenderPresent(context.renderer);
    // printf("%p\n", &(context.renderer));
    context.texture = SDL_CreateTexture(
            context.renderer,
            SDL_PIXELFORMAT_BGR565, SDL_TEXTUREACCESS_STREAMING,
            VIDEOWIDTH, VIDEOHEIGHT);
    if (!context.texture) {
        fprintf(stderr, "Couldn't create texture: %s\n", SDL_GetError());
        quit(5);
    }

    context.mutex = SDL_CreateMutex();

    // If you don't have this variable set you must have plugins directory
    // with the executable or libvlc_new() will not work!
    putenv("VLC_PLUGIN_PATH=/usr/include/vlc");
    printf("VLC_PLUGIN_PATH=%s\n", getenv("VLC_PLUGIN_PATH"));

    // Initialise libVLC.
    libvlc = libvlc_new(vlc_argc, vlc_argv);
    if(NULL == libvlc) {
        printf("LibVLC initialization failure.\n");
        return EXIT_FAILURE;
    }

    m = libvlc_media_new_path(libvlc, filepath);
    printf("Media: %s\n", filepath);
    mp = libvlc_media_player_new_from_media(m);
    libvlc_media_release(m);

    libvlc_video_set_callbacks(mp, lock, unlock, &display, &context);
    libvlc_video_set_format(mp, "RV16", VIDEOWIDTH, VIDEOHEIGHT, VIDEOWIDTH*2);
    //libvlc_media_player_play(mp);


    // Main loop.
    while(!done) {

        action = 0;

        // Keys: enter (fullscreen), space (pause), escape (quit).
        while( SDL_PollEvent( &event )) {

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
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
                break;
            case ' ':
                ms.state = !ms.state;
                if (ms.state == PAUSED) libvlc_media_player_pause(mp);
                else libvlc_media_player_play(mp);
                send_state(conn, ms.state);
                printf("%s\n", (ms.state == PAUSED ? "PAUSED":"PLAYING"));
                break;
        }
        if (ms.changed) {
                ms.changed = 0;
                if (ms.state == PAUSED) libvlc_media_player_pause(mp);
                else libvlc_media_player_play(mp);
            }

        if(!ms.state) { context.n++; }
        SDL_SetRenderDrawColor(context.renderer, 0, 0, 0, 255);
        SDL_RenderClear(context.renderer);
        SDL_RenderCopy(context.renderer, context.texture, NULL, NULL);
        SDL_RenderPresent(context.renderer);
        SDL_Delay(10);
    }

        
    

    // Stop stream and clean up libVLC.
    libvlc_media_player_stop(mp);
    libvlc_media_player_release(mp);
    libvlc_release(libvlc);

    // Close window and clean up libSDL.
    SDL_DestroyMutex(context.mutex);
    SDL_DestroyRenderer(context.renderer);

    quit(0);

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
  char *remotePort, *localPort;
  struct sockaddr_in client_addr;
  struct socketAddr conn;
  struct socketAddr sender;


  int fd;
  int sniffer_active = 0;
  int listener_active = 0;
  char *client_addr_str;
  char *filepath;
  char c;
  while ((c = getopt(argc, argv, "a:f:p:r:")) != -1) {
    switch (c) {
      case 'a': 
        client_addr_str = optarg; 
        break;
      case 'f':
        filepath = optarg;
        break;
      case 'p':
        localPort = optarg;
        break;
      case 'r':
        remotePort = optarg;
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

    if (&localPort != NULL)
      conn.addr.sin_port = htons(atoi(localPort));
    else 
      conn.addr.sin_port = htons(atoi(PORT));
    
    if (&remotePort != NULL)
      sender.addr.sin_port = htons(atoi(remotePort));
    else       
      sender.addr.sin_port = htons(atoi(remotePort));

    printf("%s\n", client_addr_str);
  }
  else{
    printf("Usage: vlcsync -a <friend's IP> -f <filepath> -p <localPort> -r <remotePort>\n");
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