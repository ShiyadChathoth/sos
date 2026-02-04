#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>

// --- 1. Struct Definitions (Must be first) ---
typedef struct {
    char letter;
    int playerID;
} Cell;

typedef struct {
    int r1, c1, r2, c2; 
    int playerID;
} SOSLine;

typedef struct {
    float x, y, vx, vy;
    SDL_Color color;
    int lifetime;
    int active;
} FireworkParticle;

#define MAX_LOBBY_PARTICLES 80
typedef struct {
    float x, y, vx, vy;
    int size;
    Uint8 alpha;
} LobbyParticle;

typedef struct {
    int start;
    int len;
} ChatLine;

#define MAX_FINALE_PARTICLES 500
FireworkParticle finale[MAX_FINALE_PARTICLES];
LobbyParticle lobbyParticles[MAX_LOBBY_PARTICLES];
int lobbyParticlesInit = 0;
int lobbyLastW = 0;
int lobbyLastH = 0;
Uint32 readyPulseStart[6] = {0};
int lobbyUserSized = 0;
int programmaticResizeInFlight = 0;
int programmaticResizeW = 0;
int programmaticResizeH = 0;
int lobbyNeedsAutoSize = 1;
int lastLobbyPlayers = -1;
int lastLobbySpectators = -1;

typedef enum { MENU, NAME_INPUT, LOBBY, GAME } GameState;

#define TURN_DURATION_SECONDS 60.0f
float turnTimer = TURN_DURATION_SECONDS;
int isBotActive = 0;
float frameDeltaSeconds = 0.0f;

#define PORT 5000
#define MAX_CLIENTS 6
#define MAX_GRID 14
#define MAX_CELLS (MAX_GRID * MAX_GRID)
#define DISCOVERY_PORT 5001
#define DISCOVERY_MSG "SOS_DISCOVER"
#define DISCOVERY_REPLY "SOS_HOST"
#define CHAT_MAX_LINES 50
#define CHAT_MSG_LEN 4096

typedef struct {
    int row;
    int col;
    char letter;
    int player;
} Move;

typedef enum {
    PKT_MOVE = 1,
    PKT_READY = 2,
    PKT_START = 3,
    PKT_ROSTER = 4,
    PKT_NAME = 5,
    PKT_TIMER = 6,
    PKT_RETURN_LOBBY = 7,
    PKT_ASSIGN = 8,
    PKT_ROLE = 9,
    PKT_CHAT = 10,
    PKT_STATE = 11
} PacketType;

typedef struct {
    int type;
    Move move;
    int player;
    int ready;
    int numPlayers;
    int readyFlags[6];
    char names[6][20];
    char name[20];
    float timer;
    int currentPlayer;
    int numSpectators;
    char spectatorNames[6][20];
    char chatMsg[CHAT_MSG_LEN];
    Uint32 chatTimestamp;
    int gridSize;
    int filledCells;
    char boardLetters[MAX_CELLS];
    int boardOwners[MAX_CELLS];
} Packet;

typedef struct {
    int sock;
} ClientThreadArgs;

int isServer = 0;
int isNetworked = 1;
int sockfd = -1;
int clientSockets[MAX_CLIENTS];
int localPlayerId = 0;
pthread_t netThread;
pthread_mutex_t gameMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t clientThreads[MAX_CLIENTS];
int readyFlags[6] = {0};
int gameStarted = 0;
int discoverySock = -1;
pthread_t discoveryThread;
pthread_t acceptThread;
int pendingResize = 0;
int pendingPlayers = 0;
Uint32 lastTimerSync = 0;
int pendingWindowResize = 0;
int pendingWinW = 0;
int pendingWinH = 0;
SDL_Rect nameInputBox = {0, 0, 0, 0};
int nameInputFocused = 1;
Uint32 nameCursorPulseStart = 0;
Uint32 chatCursorPulseStart = 0;
Uint32 gameOverAt = 0;
int transitionActive = 0;
Uint32 transitionStart = 0;
Uint32 transitionDuration = 450;
GameState transitionFrom = MENU;
GameState transitionTo = MENU;
int lobbyLocked = 0;
char spectatorNameInput[20] = "";
int spectatorDeferredStart = 0;
SDL_Rect lobbyReadyBtn[6];
SDL_Rect lobbyKickBtn[6];
SDL_Rect lobbyAddBtn[6];
SDL_Rect lobbyPlayerRowRect[6];
SDL_Rect lobbyPlusBtn[6];
SDL_Rect lobbyLockBtn = {0, 0, 0, 0};
SDL_Rect lobbyStartBtn = {0, 0, 0, 0};
SDL_Rect lobbyWatchBtn = {0, 0, 0, 0};
SDL_Rect gameReturnBtn = {0, 0, 0, 0};
SDL_Rect chatInputBox = {0, 0, 0, 0};
SDL_Rect chatToggleBtn = {0, 0, 0, 0};
SDL_Rect chatResizeHandle = {0, 0, 0, 0};
SDL_Rect chatBubbleRects[CHAT_MAX_LINES];
int chatBubbleIndex[CHAT_MAX_LINES];
int chatBubbleCount = 0;
SDL_Rect chatListRect = {0, 0, 0, 0};
SDL_Rect chatSidebarRect = {0, 0, 0, 0};
int chatSelecting = 0;
SDL_Rect chatListScrollBarRect = {0, 0, 0, 0};
SDL_Rect chatListScrollThumbRect = {0, 0, 0, 0};
int chatListScrollDragging = 0;
int chatListScrollDragStartY = 0;
int chatListScrollStartOffset = 0;
SDL_Rect chatInputScrollBarRect = {0, 0, 0, 0};
SDL_Rect chatInputScrollThumbRect = {0, 0, 0, 0};
int chatInputScrollDragging = 0;
int chatInputScrollDragStartY = 0;
int chatInputScrollStartOffset = 0;
int chatInputTextH = 0;
int chatInputVisibleH = 0;
int chatContextVisible = 0;
int chatContextType = 0;
int chatContextBubbleIndex = -1;
int chatContextX = 0;
int chatContextY = 0;
SDL_Rect chatContextRect = {0, 0, 0, 0};
SDL_Rect chatContextItemRects[4];
int chatContextItemCount = 0;

char spectatorNames[6][20] = {"", "", "", "", "", ""};
int numSpectators = 0;
int spectatorSockets[6] = {-1, -1, -1, -1, -1, -1};

#define CHAT_SIDEBAR_W 260
char chatLog[CHAT_MAX_LINES][CHAT_MSG_LEN];
char chatLogName[CHAT_MAX_LINES][20];
int chatLogSender[CHAT_MAX_LINES];
Uint32 chatLogTime[CHAT_MAX_LINES];
int chatHead = 0;
int chatCount = 0;
char chatInput[CHAT_MSG_LEN] = "";
int chatInputFocused = 0;
int chatScrollOffset = 0;
int chatCaret = 0;
int chatSelStart = -1;
int chatSelEnd = -1;
int chatCaretPreferredX = -1;
int chatInputScroll = 0;
int chatSidebarVisible = 1;
int chatSidebarWidth = CHAT_SIDEBAR_W;
float chatSidebarRatio = 0.30f;
int chatSidebarMin = 220;
int chatSidebarMax = 360;
int chatResizing = 0;
int chatResizeStartX = 0;
int chatResizeStartW = 0;
int chatSidebarUserSized = 0;
int pendingBoardState = 0;
Packet pendingBoardPacket;
volatile int forceQuit = 0;
Uint32 forceQuitAt = 0;
char forceQuitMsg[64] = "Host left - game closed";

// --- 2. Global Variables (Must be before functions) ---
TTF_Font* font = NULL;
GameState currentState = MENU;

int gridSize = 10;
int cellSize = 60;
int screenWidth = 600;
int screenHeight = 750;

Cell **board = NULL;
SOSLine *lines = NULL;
int lineCount = 0;

char playerNames[6][20] = {"", "", "", "", "", ""};
char numPlayersStr[3] = "";
int inputPlayerIdx = 0; 
int scores[6] = {0};
int currentPlayer = 0;
int numPlayers = 2;
char selectedLetter = 'S';
int gameOver = 0;
int filledCells = 0;

SDL_Color colors[6] = {
    {255, 50, 50, 255},   // Red
    {50, 255, 50, 255},   // Green
    {50, 50, 255, 255},   // Blue
    {255, 255, 50, 255},  // Yellow
    {255, 50, 255, 255},  // Magenta
    {50, 255, 255, 255}   // Cyan
};

// --- 3. Functions ---
int count_sos(int r, int c);
void* receive_from_client(void* arg);
void return_to_lobby(int broadcastToClients);
void kick_player(int playerId);
void promote_spectator(int spectatorIdx);
int demote_player_to_spectator(int playerId);
int find_player_id_by_socket(int sock);
int find_spectator_index_by_socket(int sock);
void broadcast_system_message(const char *msg);
void remove_spectator_at(int spectatorIdx);
void remove_player_at(int playerId);
void focus_name_input(int focused);
void push_chat_message(const char *msg, const char *name, int senderId, Uint32 ts);
void send_chat_from_input(void);
void close_all_client_sockets(void);
void compute_chat_input_metrics(const char *text, int maxW, float scale, int *outLines, int *outLastW);
void draw_chat_sidebar(SDL_Renderer *ren, int x, int y, int w, int h);
void append_chat_input_text(const char *text);
int handle_chat_bubble_copy(int mx, int my);
int chat_bubble_at(int mx, int my);
void chat_set_caret(int index, int selecting);
void chat_clear_selection(void);
int chat_has_selection(void);
void chat_delete_selection(void);
void chat_insert_text(const char *text);
int chat_input_substring_width(int start, int end, float scale);
int layout_chat_input_lines(const char *text, int maxW, float scale, ChatLine *lines, int maxLines);
int chat_caret_from_xy(int mx, int my, int textX, int textY, int lineHeight, int maxW, float scale, ChatLine *lines, int lineCount);
void chat_context_open(int x, int y, int type, int bubbleIdx);
void chat_context_close(void);
int chat_context_handle_click(int mx, int my);
void chat_context_copy_bubble(void);
void chat_update_caret_from_mouse(int mx, int my, int selecting);
void chat_copy_line_text(const char *text, ChatLine line, char *out, int outSize);
void render_rounded_box(SDL_Renderer *ren, SDL_Rect r, int radius, SDL_Color fill, SDL_Color border, int drawBorder);
void draw_text_wrapped_scaled(SDL_Renderer *ren, const char *text, int x, int y, int maxW, SDL_Color col, float scale);
int measure_text_wrapped_height_scaled(const char *text, int maxW, float scale);
int chat_sidebar_width(void);
int compute_window_width_with_sidebar(int contentW);

ssize_t send_all(int sock, const void *buf, size_t len) {
    size_t sent = 0;
    const char *p = (const char *)buf;
    while (sent < len) {
        ssize_t n = send(sock, p + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

ssize_t recv_all(int sock, void *buf, size_t len) {
    size_t recvd = 0;
    char *p = (char *)buf;
    while (recvd < len) {
        ssize_t n = recv(sock, p + recvd, len - recvd, 0);
        if (n <= 0) return -1;
        recvd += (size_t)n;
    }
    return (ssize_t)recvd;
}

int start_server_socket() {
    struct sockaddr_in serverAddr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 0;
    }
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        if (errno == EADDRINUSE) {
            close(sockfd);
            sockfd = -1;
            return 0;
        }
        perror("bind");
        return 0;
    }
    if (listen(sockfd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(sockfd);
        sockfd = -1;
        return 0;
    }
    printf("Server started. Waiting for players...\n");
    return 1;
}

void* discovery_responder(void* arg) {
    (void)arg;
    struct sockaddr_in cli;
    socklen_t clen = sizeof(cli);
    char buf[64];

    while (1) {
        ssize_t n = recvfrom(discoverySock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&cli, &clen);
        if (n <= 0) continue;
        buf[n] = '\0';
        if (strcmp(buf, DISCOVERY_MSG) == 0) {
            sendto(discoverySock, DISCOVERY_REPLY, strlen(DISCOVERY_REPLY), 0, (struct sockaddr*)&cli, clen);
        }
    }
    return NULL;
}

int try_bind_discovery() {
    struct sockaddr_in addr;
    discoverySock = socket(AF_INET, SOCK_DGRAM, 0);
    if (discoverySock < 0) return 0;
    int opt = 1;
    setsockopt(discoverySock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DISCOVERY_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(discoverySock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(discoverySock);
        discoverySock = -1;
        return 0;
    }
    pthread_create(&discoveryThread, NULL, discovery_responder, NULL);
    return 1;
}

static void send_discovery_broadcasts(int s) {
    // Always try the limited broadcast first.
    struct sockaddr_in bcast;
    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(DISCOVERY_PORT);
    bcast.sin_addr.s_addr = inet_addr("255.255.255.255");
    sendto(s, DISCOVERY_MSG, strlen(DISCOVERY_MSG), 0, (struct sockaddr*)&bcast, sizeof(bcast));

    // Also send to per-interface broadcast addresses (Wi-Fi routers often block 255.255.255.255).
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0 || !ifaddr) return;
    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        struct sockaddr_in *baddr = (struct sockaddr_in *)ifa->ifa_broadaddr;
        if (!baddr) continue;
        sendto(s, DISCOVERY_MSG, strlen(DISCOVERY_MSG), 0, (struct sockaddr*)baddr, sizeof(*baddr));
    }
    freeifaddrs(ifaddr);
}

int find_host_ip(char *outIp, size_t outLen) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    for (int i = 0; i < 5; i++) {
        send_discovery_broadcasts(s);

        char buf[64];
        struct sockaddr_in from;
        socklen_t fromLen = sizeof(from);
        ssize_t n = recvfrom(s, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &fromLen);
        if (n > 0) {
            buf[n] = '\0';
            if (strcmp(buf, DISCOVERY_REPLY) == 0) {
                inet_ntop(AF_INET, &from.sin_addr, outIp, outLen);
                close(s);
                return 1;
            }
        }
    }
    close(s);
    return 0;
}

void accept_clients(int expected) {
    struct sockaddr_in clientAddr;
    socklen_t addr_size = sizeof(clientAddr);

    for (int i = 0; i < expected; i++) {
        int cs = accept(sockfd, (struct sockaddr*)&clientAddr, &addr_size);
        if (cs < 0) {
            perror("accept");
            exit(1);
        }
        clientSockets[i] = cs;
        printf("Player connected!\n");

        int assignedId = i + 1;
        send_all(cs, &assignedId, sizeof(assignedId));
        send_all(cs, &numPlayers, sizeof(numPlayers));
    }
}

void connect_to_server(const char *ip) {
    struct sockaddr_in serverAddr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect");
        exit(1);
    }
    printf("Connected to host!\n");
}

int apply_move(Move m) {
    if (m.row < 0 || m.row >= gridSize || m.col < 0 || m.col >= gridSize) return 0;
    if (board[m.row][m.col].letter != ' ') return 0;

    board[m.row][m.col].letter = m.letter;
    board[m.row][m.col].playerID = m.player;
    filledCells++;

    currentPlayer = m.player;
    int points = count_sos(m.row, m.col);
    if (points > 0) {
        scores[m.player] += points;
    } else {
        currentPlayer = (m.player + 1) % numPlayers;
    }
    turnTimer = TURN_DURATION_SECONDS;
    if (filledCells == gridSize * gridSize) {
        gameOver = 1;
        if (gameOverAt == 0) gameOverAt = SDL_GetTicks();
    }
    return 1;
}

void broadcast_move(Move m) {
    for (int i = 0; i < numPlayers - 1; i++) {
        if (clientSockets[i] >= 0) {
            Packet p = {0};
            p.type = PKT_MOVE;
            p.move = m;
            send_all(clientSockets[i], &p, sizeof(p));
        }
    }
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] >= 0) {
            Packet p = {0};
            p.type = PKT_MOVE;
            p.move = m;
            send_all(spectatorSockets[i], &p, sizeof(p));
        }
    }
}

void broadcast_chat(const char *msg) {
    Packet p = {0};
    p.type = PKT_CHAT;
    strncpy(p.chatMsg, msg, CHAT_MSG_LEN - 1);
    p.chatMsg[CHAT_MSG_LEN - 1] = '\0';
    p.chatTimestamp = (Uint32)time(NULL);
    for (int i = 0; i < numPlayers - 1; i++) {
        if (clientSockets[i] >= 0) {
            send_all(clientSockets[i], &p, sizeof(p));
        }
    }
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] >= 0) {
            send_all(spectatorSockets[i], &p, sizeof(p));
        }
    }
}

void broadcast_ready_update(int playerId, int ready) {
    Packet p = {0};
    p.type = PKT_READY;
    p.player = playerId;
    p.ready = ready;
    for (int i = 0; i < numPlayers - 1; i++) {
        if (clientSockets[i] >= 0) {
            send_all(clientSockets[i], &p, sizeof(p));
        }
    }
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] >= 0) {
            send_all(spectatorSockets[i], &p, sizeof(p));
        }
    }
}

void broadcast_start_game() {
    Packet p = {0};
    p.type = PKT_START;
    for (int i = 0; i < numPlayers - 1; i++) {
        if (clientSockets[i] >= 0) {
            send_all(clientSockets[i], &p, sizeof(p));
        }
    }
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] >= 0) {
            send_all(spectatorSockets[i], &p, sizeof(p));
        }
    }
}

void send_board_state(int sock) {
    if (sock < 0) return;
    if (!board) return;
    Packet p = {0};
    p.type = PKT_STATE;
    p.gridSize = gridSize;
    p.filledCells = filledCells;
    int idx = 0;
    for (int r = 0; r < gridSize; r++) {
        for (int c = 0; c < gridSize; c++) {
            if (idx >= MAX_CELLS) break;
            p.boardLetters[idx] = board[r][c].letter;
            p.boardOwners[idx] = board[r][c].playerID;
            idx++;
        }
    }
    send_all(sock, &p, sizeof(p));
}

void broadcast_timer_sync() {
    Packet p = {0};
    p.type = PKT_TIMER;
    p.timer = turnTimer;
    p.currentPlayer = currentPlayer;
    for (int i = 0; i < numPlayers - 1; i++) {
        if (clientSockets[i] >= 0) {
            send_all(clientSockets[i], &p, sizeof(p));
        }
    }
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] >= 0) {
            send_all(spectatorSockets[i], &p, sizeof(p));
        }
    }
}

void broadcast_roster() {
    Packet p = {0};
    p.type = PKT_ROSTER;
    p.numPlayers = numPlayers;
    memcpy(p.names, playerNames, sizeof(p.names));
    memcpy(p.readyFlags, readyFlags, sizeof(p.readyFlags));
    p.numSpectators = numSpectators;
    memcpy(p.spectatorNames, spectatorNames, sizeof(p.spectatorNames));
    for (int i = 0; i < numPlayers - 1; i++) {
        if (clientSockets[i] >= 0) {
            send_all(clientSockets[i], &p, sizeof(p));
        }
    }
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] >= 0) {
            send_all(spectatorSockets[i], &p, sizeof(p));
        }
    }
}

void broadcast_return_lobby() {
    Packet p = {0};
    p.type = PKT_RETURN_LOBBY;
    for (int i = 0; i < numPlayers - 1; i++) {
        if (clientSockets[i] >= 0) {
            send_all(clientSockets[i], &p, sizeof(p));
        }
    }
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] >= 0) {
            send_all(spectatorSockets[i], &p, sizeof(p));
        }
    }
}

int all_ready() {
    for (int i = 0; i < numPlayers; i++) {
        if (!readyFlags[i]) return 0;
    }
    return 1;
}

int ready_count() {
    int count = 0;
    for (int i = 0; i < numPlayers; i++) {
        if (readyFlags[i]) count++;
    }
    return count;
}

void request_resize(int players) {
    pendingPlayers = players;
    pendingResize = 1;
}

void request_window_resize(int w, int h) {
    pendingWinW = w;
    pendingWinH = h;
    pendingWindowResize = 1;
    programmaticResizeInFlight = 1;
    programmaticResizeW = w;
    programmaticResizeH = h;
}

void apply_window_size(int w, int h) {
    screenWidth = w;
    screenHeight = h;
    if (chatSidebarVisible) {
        if (chatSidebarUserSized) {
            if (chatSidebarWidth < chatSidebarMin) chatSidebarWidth = chatSidebarMin;
            if (chatSidebarWidth > chatSidebarMax) chatSidebarWidth = chatSidebarMax;
        } else {
            chatSidebarWidth = (int)(screenWidth * chatSidebarRatio);
            if (chatSidebarWidth < chatSidebarMin) chatSidebarWidth = chatSidebarMin;
            if (chatSidebarWidth > chatSidebarMax) chatSidebarWidth = chatSidebarMax;
        }
    }
    int availableHeight = screenHeight - 150;
    int usableW = screenWidth;
    if (currentState == GAME) {
        usableW = screenWidth - chat_sidebar_width();
    }
    int cellW = usableW / gridSize;
    int cellH = availableHeight / gridSize;
    cellSize = (cellW < cellH) ? cellW : cellH;
}

void apply_game_window_size() {
    int contentW = gridSize * cellSize;
    int w = compute_window_width_with_sidebar(contentW);
    int h = (gridSize * cellSize) + 150;
    request_window_resize(w, h);
}

void apply_lobby_window_size() {
    int contentW = screenWidth - chat_sidebar_width();
    if (contentW < 520) contentW = 520;
    int h = 220 + ((numPlayers + numSpectators) * 40);
    if (h < 320) h = 320;
    if (h > 520) h = 520;
    int w = compute_window_width_with_sidebar(contentW);
    if (lobbyUserSized) {
        if (w <= screenWidth && h <= screenHeight) return;
    }
    request_window_resize(w, h);
}

void* accept_clients_thread(void* arg) {
    (void)arg;
    struct sockaddr_in clientAddr;
    socklen_t addr_size = sizeof(clientAddr);

    while (numPlayers < 6 || numSpectators < 6) {
        int cs = accept(sockfd, (struct sockaddr*)&clientAddr, &addr_size);
        if (cs < 0) continue;
        Packet roster = {0};
        int assignedId = 0;
        int assignSpectator = 0;
        pthread_mutex_lock(&gameMutex);
        if (!gameStarted && !lobbyLocked && numPlayers < 6) {
            assignedId = numPlayers;
            clientSockets[assignedId - 1] = cs;
            numPlayers++;
            readyFlags[assignedId] = 0;
            scores[assignedId] = 0;
            playerNames[assignedId][0] = '\0';
            request_resize(numPlayers);
        } else if (numSpectators < 6) {
            spectatorSockets[numSpectators] = cs;
            spectatorNames[numSpectators][0] = '\0';
            numSpectators++;
            assignedId = -1;
            assignSpectator = 1;
        } else {
            close(cs);
            pthread_mutex_unlock(&gameMutex);
            continue;
        }
        roster.type = PKT_ROSTER;
        roster.numPlayers = numPlayers;
        memcpy(roster.names, playerNames, sizeof(roster.names));
        memcpy(roster.readyFlags, readyFlags, sizeof(roster.readyFlags));
        roster.numSpectators = numSpectators;
        memcpy(roster.spectatorNames, spectatorNames, sizeof(roster.spectatorNames));

        ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
        args->sock = cs;
        pthread_create(&clientThreads[(assignSpectator ? 0 : assignedId - 1)], NULL, receive_from_client, args);
        pthread_mutex_unlock(&gameMutex);
        send_all(cs, &assignedId, sizeof(assignedId));
        send_all(cs, &roster, sizeof(roster));
        if (gameStarted) {
            Packet start = {0};
            start.type = PKT_START;
            send_all(cs, &start, sizeof(start));
            send_board_state(cs);
        }
        broadcast_roster();
    }
    return NULL;
}

void* receive_from_client(void* arg) {
    ClientThreadArgs *a = (ClientThreadArgs *)arg;
    Packet p;

    while (1) {
        if (recv_all(a->sock, &p, sizeof(p)) <= 0) {
            int playerId = find_player_id_by_socket(a->sock);
            int spectatorIdx = find_spectator_index_by_socket(a->sock);
            char leftName[32] = "";
            int shouldBroadcastRoster = 0;
            int shouldReturnLobby = 0;
            pthread_mutex_lock(&gameMutex);
            if (playerId >= 0) {
                strncpy(leftName, playerNames[playerId], sizeof(leftName) - 1);
                leftName[sizeof(leftName) - 1] = '\0';
                remove_player_at(playerId);
                shouldBroadcastRoster = 1;
                if (gameStarted && numPlayers < 2) {
                    shouldReturnLobby = 1;
                }
            } else if (spectatorIdx >= 0) {
                strncpy(leftName, spectatorNames[spectatorIdx], sizeof(leftName) - 1);
                leftName[sizeof(leftName) - 1] = '\0';
                remove_spectator_at(spectatorIdx);
                shouldBroadcastRoster = 1;
            }
            pthread_mutex_unlock(&gameMutex);
            if (leftName[0] != '\0') {
                char msg[64];
                snprintf(msg, sizeof(msg), "%s left the game", leftName);
                broadcast_system_message(msg);
            }
            if (shouldBroadcastRoster && !shouldReturnLobby) {
                broadcast_roster();
            }
            if (shouldReturnLobby) {
                return_to_lobby(1);
            }
            break;
        }
        int playerId = find_player_id_by_socket(a->sock);
        int spectatorIdx = find_spectator_index_by_socket(a->sock);
        int shouldBroadcastMove = 0;
        int shouldBroadcastRoster = 0;
        int shouldBroadcastReady = 0;
        int shouldBroadcastStart = 0;
        int readyVal = 0;
        Move toBroadcast;
        pthread_mutex_lock(&gameMutex);
        if (p.type == PKT_NAME) {
            if (playerId >= 0 && !gameStarted) {
                strncpy(playerNames[playerId], p.name, 19);
                playerNames[playerId][19] = '\0';
                shouldBroadcastRoster = 1;
            } else if (spectatorIdx >= 0) {
                strncpy(spectatorNames[spectatorIdx], p.name, 19);
                spectatorNames[spectatorIdx][19] = '\0';
                shouldBroadcastRoster = 1;
            }
        } else if (p.type == PKT_READY && !gameStarted) {
            if (playerId >= 0) {
                readyFlags[playerId] = p.ready ? 1 : 0;
                readyVal = readyFlags[playerId];
                readyPulseStart[playerId] = SDL_GetTicks();
                shouldBroadcastReady = 1;
            }
        } else if (p.type == PKT_ROLE && !gameStarted) {
            if (p.player == -1 && playerId > 0) {
                if (demote_player_to_spectator(playerId)) {
                    shouldBroadcastRoster = 1;
                }
            }
        } else if (p.type == PKT_CHAT) {
            if (p.chatMsg[0] != '\0') {
                Packet out = {0};
                out.type = PKT_CHAT;
                out.player = playerId;
                if (spectatorIdx >= 0) out.player = -1;
                out.chatTimestamp = (Uint32)time(NULL);
                strncpy(out.chatMsg, p.chatMsg, CHAT_MSG_LEN - 1);
                out.chatMsg[CHAT_MSG_LEN - 1] = '\0';
                if (out.player >= 0 && out.player < numPlayers) {
                    strncpy(out.name, playerNames[out.player], 19);
                    out.name[19] = '\0';
                } else if (spectatorIdx >= 0) {
                    strncpy(out.name, spectatorNames[spectatorIdx], 19);
                    out.name[19] = '\0';
                } else {
                    strncpy(out.name, "Spectator", 19);
                    out.name[19] = '\0';
                }
                push_chat_message(out.chatMsg, out.name, out.player, out.chatTimestamp);
                for (int i = 0; i < numPlayers - 1; i++) {
                    if (clientSockets[i] >= 0) {
                        send_all(clientSockets[i], &out, sizeof(out));
                    }
                }
                for (int i = 0; i < numSpectators; i++) {
                    if (spectatorSockets[i] >= 0) {
                        send_all(spectatorSockets[i], &out, sizeof(out));
                    }
                }
            }
        } else if (p.type == PKT_MOVE && gameStarted) {
            if (playerId >= 0) {
                Move m = p.move;
                m.player = playerId;
                if (currentPlayer == m.player && board[m.row][m.col].letter == ' ') {
                    if (apply_move(m)) {
                        shouldBroadcastMove = 1;
                        toBroadcast = m;
                    }
                }
            }
        }
        pthread_mutex_unlock(&gameMutex);
        if (shouldBroadcastRoster) {
            broadcast_roster();
        }
        if (shouldBroadcastReady) {
            broadcast_ready_update(playerId, readyVal);
        }
        if (shouldBroadcastStart) {
            broadcast_start_game();
        }
        if (shouldBroadcastMove) {
            broadcast_move(toBroadcast);
        }
    }
    close(a->sock);
    free(a);
    return NULL;
}

void* receive_moves_from_server(void* arg) {
    (void)arg;
    Packet p;
    while (1) {
        if (recv_all(sockfd, &p, sizeof(p)) <= 0) {
            forceQuit = 1;
            if (forceQuitAt == 0) forceQuitAt = SDL_GetTicks();
            strncpy(forceQuitMsg, "Host left - game closed", sizeof(forceQuitMsg) - 1);
            forceQuitMsg[sizeof(forceQuitMsg) - 1] = '\0';
            break;
        }
        pthread_mutex_lock(&gameMutex);
        if (p.type == PKT_ROSTER) {
            numPlayers = p.numPlayers;
            memcpy(playerNames, p.names, sizeof(p.names));
            memcpy(readyFlags, p.readyFlags, sizeof(p.readyFlags));
            numSpectators = p.numSpectators;
            memcpy(spectatorNames, p.spectatorNames, sizeof(p.spectatorNames));
            if (!gameStarted) {
                request_resize(numPlayers);
                if (currentState == LOBBY) {
                    apply_lobby_window_size();
                }
            }
        } else if (p.type == PKT_TIMER) {
            turnTimer = p.timer;
            currentPlayer = p.currentPlayer;
        } else if (p.type == PKT_READY) {
            if (p.player >= 0 && p.player < numPlayers) {
                readyFlags[p.player] = p.ready ? 1 : 0;
                readyPulseStart[p.player] = SDL_GetTicks();
            }
        } else if (p.type == PKT_ASSIGN) {
            int prevPlayerId = localPlayerId;
            localPlayerId = p.player;
            if (localPlayerId < 0) {
                gameStarted = 0;
                spectatorDeferredStart = 0;
                if (prevPlayerId >= 0 && playerNames[prevPlayerId][0] != '\0') {
                    strncpy(spectatorNameInput, playerNames[prevPlayerId], 19);
                    spectatorNameInput[19] = '\0';
                    currentState = LOBBY;
                    apply_lobby_window_size();
                } else {
                    spectatorNameInput[0] = '\0';
                    currentState = NAME_INPUT;
                    focus_name_input(1);
                    request_window_resize(520, 260);
                }
            }
        } else if (p.type == PKT_RETURN_LOBBY) {
            return_to_lobby(0);
        } else if (p.type == PKT_START) {
            gameStarted = 1;
            if (localPlayerId < 0 && currentState == NAME_INPUT) {
                spectatorDeferredStart = 1;
            } else {
                currentState = GAME;
                apply_game_window_size();
            }
        } else if (p.type == PKT_MOVE) {
            apply_move(p.move);
        } else if (p.type == PKT_CHAT) {
            if (p.chatMsg[0] != '\0') {
                push_chat_message(p.chatMsg, p.name, p.player, p.chatTimestamp);
            }
        } else if (p.type == PKT_STATE) {
            pendingBoardPacket = p;
            pendingBoardState = 1;
        }
        pthread_mutex_unlock(&gameMutex);
    }
    return NULL;
}

void exchange_names_after_input() {
    if (isServer) {
        for (int i = 0; i < numPlayers - 1; i++) {
            recv_all(clientSockets[i], playerNames[i + 1], 20);
            playerNames[i + 1][19] = '\0';
        }
        for (int i = 0; i < numPlayers - 1; i++) {
            send_all(clientSockets[i], playerNames, numPlayers * 20);
        }
    } else {
        send_all(sockfd, playerNames[localPlayerId], 20);
        recv_all(sockfd, playerNames, numPlayers * 20);
        playerNames[localPlayerId][19] = '\0';
    }
}

void init_ttf() {
    if (TTF_Init() == -1) printf("TTF Error: %s\n", TTF_GetError());
    font = TTF_OpenFont("/home/shiyad/sos_temp/DejaVuSans.ttf", 24);
    if (!font) {
        font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 24);
    }
    if (!font) {
        font = TTF_OpenFont("arial.ttf", 24);
    }
    if (!font) printf("Font Error: Ensure a valid TTF font is available.\n");
}

void draw_text(SDL_Renderer *r, const char* text, int x, int y, SDL_Color col) {
    if (!font || !text || strlen(text) == 0) return;
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, col);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(r, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dstRect = { x, y, surface->w, surface->h };
    SDL_RenderCopy(r, texture, NULL, &dstRect);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

void draw_text_scaled(SDL_Renderer *r, const char* text, int centerX, int centerY, SDL_Color col, float scale) {
    if (!font || !text) return;
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, col);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(r, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    
    int w = (int)(surface->w * scale);
    int h = (int)(surface->h * scale);
    SDL_Rect dstRect = { centerX - w/2, centerY - h/2, w, h };
    
    SDL_SetTextureAlphaMod(texture, col.a); // Apply the fade
    SDL_RenderCopy(r, texture, NULL, &dstRect);
    
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

void draw_text_scaled_topleft(SDL_Renderer *r, const char* text, int x, int y, SDL_Color col, float scale) {
    if (!font || !text) return;
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, col);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(r, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    int w = (int)(surface->w * scale);
    int h = (int)(surface->h * scale);
    SDL_Rect dstRect = { x, y, w, h };
    SDL_SetTextureAlphaMod(texture, col.a);
    SDL_RenderCopy(r, texture, NULL, &dstRect);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

int text_width_scaled(const char* text, float scale) {
    if (!font || !text || text[0] == '\0') return 0;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, text, &w, &h) != 0) return 0;
    return (int)(w * scale);
}

void text_size_scaled(const char* text, float scale, int *outW, int *outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!font || !text || text[0] == '\0') return;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(font, text, &w, &h) != 0) return;
    if (outW) *outW = (int)(w * scale);
    if (outH) *outH = (int)(h * scale);
}

int point_in_rect(int x, int y, SDL_Rect r) {
    return (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h);
}

int find_player_id_by_socket(int sock) {
    for (int i = 1; i < numPlayers; i++) {
        if (clientSockets[i - 1] == sock) return i;
    }
    return -1;
}

int find_spectator_index_by_socket(int sock) {
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] == sock) return i;
    }
    return -1;
}

void focus_name_input(int focused) {
    nameInputFocused = focused;
    if (focused) {
        nameCursorPulseStart = SDL_GetTicks();
    }
}

void reset_round_state(void) {
    gameOver = 0;
    gameOverAt = 0;
    filledCells = 0;
    lineCount = 0;
    currentPlayer = 0;
    selectedLetter = 'S';
    turnTimer = TURN_DURATION_SECONDS;
    if (board != NULL) {
        for (int r = 0; r < gridSize; r++) {
            for (int c = 0; c < gridSize; c++) {
                board[r][c].letter = ' ';
                board[r][c].playerID = 0;
            }
        }
    }
    for (int i = 0; i < 6; i++) scores[i] = 0;
}

void start_transition(GameState from, GameState to, Uint32 durationMs) {
    transitionActive = 1;
    transitionStart = SDL_GetTicks();
    transitionDuration = durationMs;
    transitionFrom = from;
    transitionTo = to;
}

void draw_fade_overlay(SDL_Renderer *ren, Uint8 alpha) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, alpha);
    SDL_Rect r = {0, 0, screenWidth, screenHeight};
    SDL_RenderFillRect(ren, &r);
}

void apply_board_state_packet(const Packet *p) {
    if (!p || !board) return;
    if (p->gridSize <= 0 || p->gridSize > MAX_GRID) return;
    if (gridSize != p->gridSize) return;
    filledCells = 0;
    lineCount = 0;
    int idx = 0;
    for (int r = 0; r < gridSize; r++) {
        for (int c = 0; c < gridSize; c++) {
            if (idx >= MAX_CELLS) break;
            board[r][c].letter = p->boardLetters[idx];
            board[r][c].playerID = p->boardOwners[idx];
            if (board[r][c].letter != ' ') filledCells++;
            idx++;
        }
    }
}

void render_rounded_box(SDL_Renderer *ren, SDL_Rect r, int radius, SDL_Color fill, SDL_Color border, int drawBorder) {
    if (radius < 0) radius = 0;
    if (radius > r.w / 2) radius = r.w / 2;
    if (radius > r.h / 2) radius = r.h / 2;

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, fill.r, fill.g, fill.b, fill.a);

    SDL_Rect mid = { r.x + radius, r.y, r.w - (radius * 2), r.h };
    SDL_RenderFillRect(ren, &mid);
    SDL_Rect left = { r.x, r.y + radius, radius, r.h - (radius * 2) };
    SDL_Rect right = { r.x + r.w - radius, r.y + radius, radius, r.h - (radius * 2) };
    SDL_RenderFillRect(ren, &left);
    SDL_RenderFillRect(ren, &right);

    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius) {
                SDL_RenderDrawPoint(ren, r.x + radius + dx, r.y + radius + dy);
                SDL_RenderDrawPoint(ren, r.x + r.w - radius - 1 + dx, r.y + radius + dy);
                SDL_RenderDrawPoint(ren, r.x + radius + dx, r.y + r.h - radius - 1 + dy);
                SDL_RenderDrawPoint(ren, r.x + r.w - radius - 1 + dx, r.y + r.h - radius - 1 + dy);
            }
        }
    }

    if (drawBorder) {
        SDL_SetRenderDrawColor(ren, border.r, border.g, border.b, border.a);
        SDL_Rect br = r;
        SDL_RenderDrawRect(ren, &br);
    }
}

void draw_text_wrapped_scaled(SDL_Renderer *ren, const char *text, int x, int y, int maxW, SDL_Color col, float scale) {
    if (!font || !text || text[0] == '\0' || maxW <= 0) return;
    if (scale <= 0.0f) scale = 1.0f;
    int wrapW = (int)((float)maxW / scale);
    if (wrapW < 10) wrapW = 10;
    SDL_Surface* surface = TTF_RenderText_Blended_Wrapped(font, text, col, (Uint32)wrapW);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(ren, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dstRect = { x, y, (int)(surface->w * scale), (int)(surface->h * scale) };
    SDL_RenderCopy(ren, texture, NULL, &dstRect);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

int measure_text_wrapped_height_scaled(const char *text, int maxW, float scale) {
    if (!font || !text || text[0] == '\0' || maxW <= 0) return 0;
    if (scale <= 0.0f) scale = 1.0f;
    int wrapW = (int)((float)maxW / scale);
    if (wrapW < 10) wrapW = 10;
    SDL_Surface* surface = TTF_RenderText_Blended_Wrapped(font, text, (SDL_Color){255,255,255,255}, (Uint32)wrapW);
    if (!surface) return 0;
    int h = (int)(surface->h * scale);
    SDL_FreeSurface(surface);
    return h;
}

void compute_chat_input_metrics(const char *text, int maxW, float scale, int *outLines, int *outLastW) {
    if (outLines) *outLines = 1;
    if (outLastW) *outLastW = 0;
    if (!text || text[0] == '\0' || maxW <= 0) return;

    int lines = 1;
    int lastW = 0;
    char lineBuf[CHAT_MSG_LEN];
    int lineLen = 0;
    int lastSpacePos = -1;

    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            lastW = text_width_scaled(lineBuf, scale);
            lines++;
            lineLen = 0;
            lastSpacePos = -1;
            continue;
        }
        if (lineLen < CHAT_MSG_LEN - 1) {
            lineBuf[lineLen++] = c;
            lineBuf[lineLen] = '\0';
        }
        if (c == ' ') {
            lastSpacePos = lineLen - 1;
        }
        int w = text_width_scaled(lineBuf, scale);
        if (w > maxW && lineLen > 1) {
            if (lastSpacePos >= 0) {
                int moveLen = lineLen - lastSpacePos - 1;
                if (moveLen > 0) {
                    memmove(lineBuf, lineBuf + lastSpacePos + 1, (size_t)moveLen);
                }
                lineLen = moveLen;
                lineBuf[lineLen] = '\0';
            } else {
                char lastChar = lineBuf[lineLen - 1];
                lineBuf[0] = lastChar;
                lineLen = 1;
                lineBuf[1] = '\0';
            }
            lines++;
            lastSpacePos = -1;
            w = text_width_scaled(lineBuf, scale);
        }
        lastW = w;
    }

    if (outLines) *outLines = lines;
    if (outLastW) *outLastW = lastW;
}

void append_chat_input_text(const char *text) {
    chat_insert_text(text);
}

int chat_has_selection(void) {
    return (chatSelStart >= 0 && chatSelEnd >= 0 && chatSelStart != chatSelEnd);
}

void chat_clear_selection(void) {
    chatSelStart = -1;
    chatSelEnd = -1;
}

static void chat_normalize_selection(int *outStart, int *outEnd) {
    int a = chatSelStart;
    int b = chatSelEnd;
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    int maxLen = (int)strlen(chatInput);
    if (a > maxLen) a = maxLen;
    if (b > maxLen) b = maxLen;
    if (outStart) *outStart = a;
    if (outEnd) *outEnd = b;
}

void chat_set_caret(int index, int selecting) {
    int len = (int)strlen(chatInput);
    if (index < 0) index = 0;
    if (index > len) index = len;
    if (selecting) {
        if (!chat_has_selection()) {
            chatSelStart = chatCaret;
            chatSelEnd = index;
        } else {
            chatSelEnd = index;
        }
    } else {
        chat_clear_selection();
    }
    chatCaret = index;
    chatCaretPreferredX = -1;
}

void chat_delete_selection(void) {
    if (!chat_has_selection()) return;
    int a = 0, b = 0;
    chat_normalize_selection(&a, &b);
    if (a == b) return;
    size_t len = strlen(chatInput);
    memmove(chatInput + a, chatInput + b, len - (size_t)b + 1);
    chatCaret = a;
    chat_clear_selection();
}

void chat_insert_text(const char *text) {
    if (!text) return;
    chat_delete_selection();
    size_t len = strlen(chatInput);
    size_t maxLen = CHAT_MSG_LEN - 1;
    if (len >= maxLen) return;
    char temp[CHAT_MSG_LEN];
    size_t tlen = 0;
    for (size_t i = 0; text[i] != '\0' && tlen < CHAT_MSG_LEN - 1; i++) {
        char c = text[i];
        if (c == '\r') {
            if (text[i + 1] == '\n') i++;
            c = '\n';
        }
        temp[tlen++] = c;
    }
    temp[tlen] = '\0';
    size_t insertLen = tlen;
    if (len + insertLen > maxLen) {
        insertLen = maxLen - len;
    }
    if (insertLen == 0) return;
    memmove(chatInput + chatCaret + insertLen, chatInput + chatCaret, len - (size_t)chatCaret + 1);
    memcpy(chatInput + chatCaret, temp, insertLen);
    chatCaret += (int)insertLen;
}

int chat_input_substring_width(int start, int end, float scale) {
    if (end < start) {
        int t = start;
        start = end;
        end = t;
    }
    int len = (int)strlen(chatInput);
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    int subLen = end - start;
    if (subLen <= 0) return 0;
    char buf[CHAT_MSG_LEN];
    if (subLen > CHAT_MSG_LEN - 1) subLen = CHAT_MSG_LEN - 1;
    memcpy(buf, chatInput + start, (size_t)subLen);
    buf[subLen] = '\0';
    return text_width_scaled(buf, scale);
}

int layout_chat_input_lines(const char *text, int maxW, float scale, ChatLine *lines, int maxLines) {
    if (!lines || maxLines <= 0) return 0;
    if (!text || text[0] == '\0' || maxW <= 0) {
        lines[0].start = 0;
        lines[0].len = 0;
        return 1;
    }
    int count = 0;
    int lineStart = 0;
    char lineBuf[CHAT_MSG_LEN];
    int lineLen = 0;
    int lastSpacePos = -1;
    int lastSpaceIndex = -1;
    for (int i = 0; ; i++) {
        char c = text[i];
        if (c == '\0' || c == '\n') {
            if (count < maxLines) {
                lines[count].start = lineStart;
                lines[count].len = lineLen;
                count++;
            }
            if (c == '\0') break;
            lineStart = i + 1;
            lineLen = 0;
            lastSpacePos = -1;
            lastSpaceIndex = -1;
            continue;
        }
        if (lineLen < CHAT_MSG_LEN - 1) {
            lineBuf[lineLen++] = c;
            lineBuf[lineLen] = '\0';
        }
        if (c == ' ') {
            lastSpacePos = lineLen - 1;
            lastSpaceIndex = i;
        }
        int w = text_width_scaled(lineBuf, scale);
        if (w > maxW && lineLen > 1) {
            if (lastSpacePos >= 0) {
                int lineLenFinal = lastSpacePos;
                if (count < maxLines) {
                    lines[count].start = lineStart;
                    lines[count].len = lineLenFinal;
                    count++;
                }
                int moveLen = lineLen - lastSpacePos - 1;
                if (moveLen > 0) {
                    memmove(lineBuf, lineBuf + lastSpacePos + 1, (size_t)moveLen);
                }
                lineLen = moveLen;
                lineBuf[lineLen] = '\0';
                lineStart = lastSpaceIndex + 1;
                lastSpacePos = -1;
                lastSpaceIndex = -1;
                for (int j = 0; j < lineLen; j++) {
                    if (lineBuf[j] == ' ') {
                        lastSpacePos = j;
                        lastSpaceIndex = lineStart + j;
                    }
                }
            } else {
                int lineLenFinal = lineLen - 1;
                if (count < maxLines) {
                    lines[count].start = lineStart;
                    lines[count].len = lineLenFinal;
                    count++;
                }
                lineStart = i;
                lineBuf[0] = c;
                lineLen = 1;
                lineBuf[1] = '\0';
                lastSpacePos = (c == ' ') ? 0 : -1;
                lastSpaceIndex = (c == ' ') ? i : -1;
            }
        }
    }
    if (count <= 0) {
        lines[0].start = 0;
        lines[0].len = 0;
        return 1;
    }
    return count;
}

static int chat_find_index_at_x(int lineStart, int lineLen, float scale, int targetX) {
    if (lineLen <= 0) return lineStart;
    if (targetX <= 0) return lineStart;
    int best = lineStart + lineLen;
    for (int i = 1; i <= lineLen; i++) {
        int w = chat_input_substring_width(lineStart, lineStart + i, scale);
        if (w >= targetX) {
            best = lineStart + i;
            break;
        }
    }
    return best;
}

int chat_caret_from_xy(int mx, int my, int textX, int textY, int lineHeight, int maxW, float scale, ChatLine *lines, int lineCount) {
    if (!lines || lineCount <= 0 || lineHeight <= 0) return (int)strlen(chatInput);
    int relY = my - textY;
    if (relY < 0) relY = 0;
    int lineIdx = relY / lineHeight;
    if (lineIdx < 0) lineIdx = 0;
    if (lineIdx >= lineCount) lineIdx = lineCount - 1;
    int relX = mx - textX;
    if (relX < 0) relX = 0;
    if (relX > maxW) relX = maxW;
    int start = lines[lineIdx].start;
    int len = lines[lineIdx].len;
    return chat_find_index_at_x(start, len, scale, relX);
}

int chat_bubble_at(int mx, int my) {
    if (!chatSidebarVisible) return -1;
    for (int i = 0; i < chatBubbleCount; i++) {
        if (point_in_rect(mx, my, chatBubbleRects[i])) {
            return chatBubbleIndex[i];
        }
    }
    return -1;
}

int handle_chat_bubble_copy(int mx, int my) {
    int idx = chat_bubble_at(mx, my);
    if (idx >= 0 && idx < CHAT_MAX_LINES) {
        SDL_SetClipboardText(chatLog[idx]);
        return 1;
    }
    return 0;
}

void chat_context_open(int x, int y, int type, int bubbleIdx) {
    chatContextVisible = 1;
    chatContextType = type;
    chatContextBubbleIndex = bubbleIdx;
    chatContextX = x;
    chatContextY = y;
}

void chat_context_close(void) {
    chatContextVisible = 0;
    chatContextType = 0;
    chatContextBubbleIndex = -1;
    chatContextItemCount = 0;
    chatContextRect = (SDL_Rect){0, 0, 0, 0};
}

void chat_context_copy_bubble(void) {
    if (chatContextBubbleIndex >= 0 && chatContextBubbleIndex < CHAT_MAX_LINES) {
        SDL_SetClipboardText(chatLog[chatContextBubbleIndex]);
    }
}

int chat_context_handle_click(int mx, int my) {
    if (!chatContextVisible) return 0;
    if (!point_in_rect(mx, my, chatContextRect)) return 0;
    for (int i = 0; i < chatContextItemCount; i++) {
        if (point_in_rect(mx, my, chatContextItemRects[i])) {
            if (chatContextType == 1) {
                if (i == 0) {
                    if (chat_has_selection()) {
                        int a = 0, b = 0;
                        chat_normalize_selection(&a, &b);
                        char buf[CHAT_MSG_LEN];
                        int len = b - a;
                        if (len > CHAT_MSG_LEN - 1) len = CHAT_MSG_LEN - 1;
                        memcpy(buf, chatInput + a, (size_t)len);
                        buf[len] = '\0';
                        SDL_SetClipboardText(buf);
                    } else {
                        SDL_SetClipboardText(chatInput);
                    }
                } else if (i == 1) {
                    if (chat_has_selection()) {
                        int a = 0, b = 0;
                        chat_normalize_selection(&a, &b);
                        char buf[CHAT_MSG_LEN];
                        int len = b - a;
                        if (len > CHAT_MSG_LEN - 1) len = CHAT_MSG_LEN - 1;
                        memcpy(buf, chatInput + a, (size_t)len);
                        buf[len] = '\0';
                        SDL_SetClipboardText(buf);
                        chat_delete_selection();
                    } else {
                        SDL_SetClipboardText(chatInput);
                        chatInput[0] = '\0';
                        chatCaret = 0;
                    }
                } else if (i == 2) {
                    if (SDL_HasClipboardText()) {
                        char *clip = SDL_GetClipboardText();
                        chat_insert_text(clip);
                        SDL_free(clip);
                    }
                } else if (i == 3) {
                    chatSelStart = 0;
                    chatSelEnd = (int)strlen(chatInput);
                    chatCaret = chatSelEnd;
                }
            } else if (chatContextType == 2) {
                chat_context_copy_bubble();
            }
            chat_context_close();
            return 1;
        }
    }
    return 1;
}

void chat_update_caret_from_mouse(int mx, int my, int selecting) {
    int inputPad = 12;
    int textMaxW = chatInputBox.w - (inputPad * 2);
    if (textMaxW < 0) textMaxW = 0;
    int baseW = 0, baseH = 0;
    text_size_scaled("Ag", 1.0f, &baseW, &baseH);
    float textScale = 0.85f;
    if (textScale > 1.0f) textScale = 1.0f;
    if (textScale < 0.6f) textScale = 0.6f;
    int lineHeight = (int)((float)baseH * textScale);
    if (lineHeight < 10) lineHeight = 10;
    ChatLine linesBuf[CHAT_MSG_LEN];
    int lineCount = layout_chat_input_lines(chatInput, textMaxW, textScale, linesBuf, CHAT_MSG_LEN);
    int inputTextH = lineCount * lineHeight;
    int visibleTextH = chatInputBox.h - 12;
    int scrollY = 0;
    if (inputTextH > visibleTextH) {
        scrollY = chatInputScroll;
    }
    int textX = chatInputBox.x + inputPad;
    int textY = chatInputBox.y + 6 - scrollY;
    int caretIdx = chat_caret_from_xy(mx, my, textX, textY, lineHeight, textMaxW, textScale, linesBuf, lineCount);
    chat_set_caret(caretIdx, selecting);
}

void chat_copy_line_text(const char *text, ChatLine line, char *out, int outSize) {
    if (!out || outSize <= 0) return;
    out[0] = '\0';
    if (!text) return;
    int len = line.len;
    if (len < 0) len = 0;
    if (len > outSize - 1) len = outSize - 1;
    memcpy(out, text + line.start, (size_t)len);
    out[len] = '\0';
}

int chat_sidebar_width(void) {
    return chatSidebarVisible ? chatSidebarWidth : 0;
}

int compute_window_width_with_sidebar(int contentW) {
    if (!chatSidebarVisible) return contentW;
    if (chatSidebarUserSized) {
        if (chatSidebarWidth < chatSidebarMin) chatSidebarWidth = chatSidebarMin;
        if (chatSidebarWidth > chatSidebarMax) chatSidebarWidth = chatSidebarMax;
        return contentW + chatSidebarWidth;
    }
    int total = (int)((float)contentW / (1.0f - chatSidebarRatio));
    int side = (int)(total * chatSidebarRatio);
    if (side < chatSidebarMin) side = chatSidebarMin;
    if (side > chatSidebarMax) side = chatSidebarMax;
    chatSidebarWidth = side;
    return contentW + side;
}

void push_chat_message(const char *msg, const char *name, int senderId, Uint32 ts) {
    if (!msg || msg[0] == '\0') return;
    strncpy(chatLog[chatHead], msg, CHAT_MSG_LEN - 1);
    chatLog[chatHead][CHAT_MSG_LEN - 1] = '\0';
    if (name && name[0] != '\0') {
        strncpy(chatLogName[chatHead], name, 19);
        chatLogName[chatHead][19] = '\0';
    } else {
        strncpy(chatLogName[chatHead], "Unknown", 19);
        chatLogName[chatHead][19] = '\0';
    }
    chatLogSender[chatHead] = senderId;
    chatLogTime[chatHead] = ts;
    chatHead = (chatHead + 1) % CHAT_MAX_LINES;
    if (chatCount < CHAT_MAX_LINES) chatCount++;
}

void send_chat_from_input(void) {
    if (chatInput[0] == '\0') return;
    if (isServer) {
        const char *name = (localPlayerId >= 0 && playerNames[localPlayerId][0] != '\0')
            ? playerNames[localPlayerId]
            : "Host";
        Uint32 ts = (Uint32)time(NULL);
        Packet out = {0};
        out.type = PKT_CHAT;
        out.player = localPlayerId;
        out.chatTimestamp = ts;
        strncpy(out.chatMsg, chatInput, CHAT_MSG_LEN - 1);
        out.chatMsg[CHAT_MSG_LEN - 1] = '\0';
        strncpy(out.name, name, 19);
        out.name[19] = '\0';
        push_chat_message(out.chatMsg, out.name, out.player, out.chatTimestamp);
        for (int i = 0; i < numPlayers - 1; i++) {
            if (clientSockets[i] >= 0) {
                send_all(clientSockets[i], &out, sizeof(out));
            }
        }
        for (int i = 0; i < numSpectators; i++) {
            if (spectatorSockets[i] >= 0) {
                send_all(spectatorSockets[i], &out, sizeof(out));
            }
        }
    } else {
        Packet p = {0};
        p.type = PKT_CHAT;
        p.player = localPlayerId;
        p.chatTimestamp = (Uint32)time(NULL);
        strncpy(p.chatMsg, chatInput, CHAT_MSG_LEN - 1);
        p.chatMsg[CHAT_MSG_LEN - 1] = '\0';
        if (localPlayerId >= 0 && playerNames[localPlayerId][0] != '\0') {
            strncpy(p.name, playerNames[localPlayerId], 19);
            p.name[19] = '\0';
        } else {
            strncpy(p.name, "Spectator", 19);
            p.name[19] = '\0';
        }
        send_all(sockfd, &p, sizeof(p));
    }

    chatInput[0] = '\0';
    chatCaret = 0;
    chat_clear_selection();
    chatCaretPreferredX = -1;
    chatInputScroll = 0;
}

void broadcast_system_message(const char *msg) {
    if (!msg || msg[0] == '\0') return;
    Packet out = {0};
    out.type = PKT_CHAT;
    out.player = -2;
    out.chatTimestamp = (Uint32)time(NULL);
    strncpy(out.chatMsg, msg, CHAT_MSG_LEN - 1);
    out.chatMsg[CHAT_MSG_LEN - 1] = '\0';
    strncpy(out.name, "SYSTEM", 19);
    out.name[19] = '\0';
    push_chat_message(out.chatMsg, out.name, out.player, out.chatTimestamp);
    for (int i = 0; i < numPlayers - 1; i++) {
        if (clientSockets[i] >= 0) {
            send_all(clientSockets[i], &out, sizeof(out));
        }
    }
    for (int i = 0; i < numSpectators; i++) {
        if (spectatorSockets[i] >= 0) {
            send_all(spectatorSockets[i], &out, sizeof(out));
        }
    }
}

void draw_chat_sidebar(SDL_Renderer *ren, int x, int y, int w, int h) {
    if (w <= 0) {
        chatBubbleCount = 0;
        chatListRect = (SDL_Rect){0, 0, 0, 0};
        chatSidebarRect = (SDL_Rect){0, 0, 0, 0};
        chatInputScrollBarRect = (SDL_Rect){0, 0, 0, 0};
        chatInputScrollThumbRect = (SDL_Rect){0, 0, 0, 0};
        int tabW = 24;
        int tabH = 60;
        chatToggleBtn = (SDL_Rect){ screenWidth - tabW - 6, y + (h - tabH) / 2, tabW, tabH };
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        int hovered = point_in_rect(mx, my, chatToggleBtn);
        SDL_Color baseFill = {31, 44, 52, 120};
        SDL_Color baseStroke = {60, 75, 90, 120};
        SDL_Color hoverFill = {44, 56, 66, 255};
        SDL_Color hoverStroke = {90, 110, 130, 255};
        render_rounded_box(ren, chatToggleBtn, 8, hovered ? hoverFill : baseFill, hovered ? hoverStroke : baseStroke, 1);
        if (hovered) {
            int tw = 0, th = 0;
            text_size_scaled(">", 1.0f, &tw, &th);
            int tx = chatToggleBtn.x + (chatToggleBtn.w - tw) / 2;
            int ty = chatToggleBtn.y + (chatToggleBtn.h - th) / 2;
            draw_text_scaled_topleft(ren, ">", tx, ty, (SDL_Color){230, 240, 255, 255}, 1.0f);
        }
        chatInputBox = (SDL_Rect){0, 0, 0, 0};
        chatInputFocused = 0;
        chatResizeHandle = (SDL_Rect){0, 0, 0, 0};
        return;
    }
    chatBubbleCount = 0;
    SDL_Color bgCol = {17, 27, 33, 255};     // #111B21
    SDL_Color barCol = {31, 44, 52, 255};    // #1F2C34
    SDL_Color incCol = {32, 44, 51, 255};    // #202C33
    SDL_Color outCol = {0, 92, 75, 255};     // #005C4B
    SDL_Color nameCol = {130, 200, 255, 255};
    SDL_Color textCol = {230, 240, 255, 255};
    SDL_Color timeCol = {170, 180, 200, 255};
    float timeScale = 0.6f;
    float msgScale = 0.75f;

    SDL_Rect bg = { x, y, w, h };
    chatSidebarRect = bg;
    SDL_SetRenderDrawColor(ren, bgCol.r, bgCol.g, bgCol.b, bgCol.a);
    SDL_RenderFillRect(ren, &bg);

    int padding = 12;
    int inputPad = 12;
    int textMaxW = (w - (padding * 2)) - (inputPad * 2);
    if (textMaxW < 0) textMaxW = 0;
    int baseW = 0, baseH = 0;
    text_size_scaled("Ag", 1.0f, &baseW, &baseH);
    float textScale = 0.85f;
    if (textScale > 1.0f) textScale = 1.0f;
    if (textScale < 0.6f) textScale = 0.6f;
    int lineHeight = (int)((float)baseH * textScale);
    if (lineHeight < 10) lineHeight = 10;
    ChatLine inputLinesBuf[CHAT_MSG_LEN];
    int inputLines = layout_chat_input_lines(chatInput, textMaxW, textScale, inputLinesBuf, CHAT_MSG_LEN);
    int maxInputLines = 6;
    int visibleLines = inputLines;
    if (visibleLines > maxInputLines) visibleLines = maxInputLines;
    int inputBoxH = (visibleLines * lineHeight) + 12;
    int footerH = inputBoxH + 16;
    int listTop = y + padding + 8;
    int listBottom = y + h - footerH - padding;
    int listH = listBottom - listTop;
    if (listH < 0) listH = 0;
    chatListRect = (SDL_Rect){ x + padding, listTop, w - (padding * 2), listH };

    int bubbleMaxW = w - (padding * 2) - 18;
    int bubbleLimit = (int)((float)w * 0.72f);
    if (bubbleMaxW > bubbleLimit) bubbleMaxW = bubbleLimit;
    if (bubbleMaxW < 60) bubbleMaxW = 60;

    int start = (chatHead - chatCount + CHAT_MAX_LINES) % CHAT_MAX_LINES;
    float nameScale = 0.8f;
    int msgHeights[CHAT_MAX_LINES];
    int msgWidths[CHAT_MAX_LINES];
    int msgIsSelf[CHAT_MAX_LINES];
    int totalH = 0;

    for (int i = 0; i < chatCount; i++) {
        int idx = (start + i) % CHAT_MAX_LINES;
        int sender = chatLogSender[idx];
        int isSelf = (localPlayerId >= 0 && sender == localPlayerId);
        msgIsSelf[i] = isSelf;

        int nameH = 0;
        if (!isSelf) {
            int nw = 0, nh = 0;
            text_size_scaled(chatLogName[idx], nameScale, &nw, &nh);
            nameH = nh + 4;
        }
        int textH = measure_text_wrapped_height_scaled(chatLog[idx], bubbleMaxW - 16, msgScale);
        int bubbleH = textH + 14;
        int timeH = 14;
        msgHeights[i] = nameH + bubbleH + timeH + 8;
        msgWidths[i] = bubbleMaxW;
        totalH += msgHeights[i];
    }

    int maxOffset = totalH - listH;
    if (maxOffset < 0) maxOffset = 0;
    if (chatScrollOffset < 0) chatScrollOffset = 0;
    if (chatScrollOffset > maxOffset) chatScrollOffset = maxOffset;

    chatListScrollBarRect = (SDL_Rect){ listTop, listTop, 0, 0 };
    chatListScrollThumbRect = (SDL_Rect){ listTop, listTop, 0, 0 };
    if (maxOffset > 0 && listH > 0) {
        int barW = 6;
        int barX = x + w - padding - barW;
        int barY = listTop;
        int barH = listH;
        chatListScrollBarRect = (SDL_Rect){ barX, barY, barW, barH };
        int thumbH = (int)((float)listH * ((float)listH / (float)totalH));
        if (thumbH < 16) thumbH = 16;
        if (thumbH > barH) thumbH = barH;
        int thumbY = barY;
        if (maxOffset > 0) {
            thumbY = barY + (int)((float)(barH - thumbH) * ((float)chatScrollOffset / (float)maxOffset));
        }
        chatListScrollThumbRect = (SDL_Rect){ barX, thumbY, barW, thumbH };
    }

    int drawY = listBottom - totalH + chatScrollOffset;
    if (drawY < listTop) drawY = listTop - (totalH - listH) + chatScrollOffset;

    for (int i = 0; i < chatCount; i++) {
        int idx = (start + i) % CHAT_MAX_LINES;
        int isSelf = msgIsSelf[i];
        int blockH = msgHeights[i];
        int blockTop = drawY;
        int blockBottom = drawY + blockH;

        if (blockBottom >= listTop && blockTop <= listBottom) {
            int bubbleW = bubbleMaxW;
            int bubbleX = isSelf ? (x + w - padding - bubbleW) : (x + padding);
            int curY = drawY;

            if (!isSelf) {
                SDL_Color ncol = (chatLogSender[idx] >= 0 && chatLogSender[idx] < 6) ? colors[chatLogSender[idx]] : nameCol;
                draw_text_scaled_topleft(ren, chatLogName[idx], bubbleX + 6, curY, ncol, nameScale);
                int nw = 0, nh = 0;
                text_size_scaled(chatLogName[idx], nameScale, &nw, &nh);
                curY += nh + 4;
            }

            int textH = measure_text_wrapped_height_scaled(chatLog[idx], bubbleW - 16, msgScale);
            SDL_Rect bubble = { bubbleX, curY, bubbleW, textH + 14 };
            render_rounded_box(ren, bubble, 8, isSelf ? outCol : incCol, (SDL_Color){0,0,0,0}, 0);
            draw_text_wrapped_scaled(ren, chatLog[idx], bubbleX + 8, curY + 6, bubbleW - 16, textCol, msgScale);
            if (chatBubbleCount < CHAT_MAX_LINES) {
                chatBubbleRects[chatBubbleCount] = bubble;
                chatBubbleIndex[chatBubbleCount] = idx;
                chatBubbleCount++;
            }

            char timeBuf[16];
            time_t tt = (time_t)chatLogTime[idx];
            struct tm *tm_info = localtime(&tt);
            if (tm_info) {
                strftime(timeBuf, sizeof(timeBuf), "%H:%M", tm_info);
            } else {
                strncpy(timeBuf, "--:--", sizeof(timeBuf) - 1);
                timeBuf[sizeof(timeBuf) - 1] = '\0';
            }
            int timeW = (int)(text_width_scaled(timeBuf, timeScale));
            draw_text_scaled_topleft(ren, timeBuf, bubbleX + bubbleW - timeW - 8, curY + textH + 10, timeCol, timeScale);
        }
        drawY += blockH;
    }

    if (chatListScrollBarRect.w > 0) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 40, 55, 65, 180);
        SDL_RenderFillRect(ren, &chatListScrollBarRect);
        SDL_SetRenderDrawColor(ren, 90, 120, 140, 220);
        SDL_RenderFillRect(ren, &chatListScrollThumbRect);
    }

    SDL_Rect footer = { x, y + h - footerH, w, footerH };
    SDL_SetRenderDrawColor(ren, barCol.r, barCol.g, barCol.b, barCol.a);
    SDL_RenderFillRect(ren, &footer);

    chatInputBox = (SDL_Rect){ x + padding, y + h - footerH + 8, w - (padding * 2), inputBoxH };
    render_rounded_box(ren, chatInputBox, chatInputBox.h / 2, (SDL_Color){44, 56, 66, 255}, (SDL_Color){60, 75, 90, 255}, 1);
    int inputTextH = inputLines * lineHeight;
    int visibleTextH = chatInputBox.h - 12;
    if (visibleTextH < lineHeight) visibleTextH = lineHeight;
    chatInputTextH = inputTextH;
    chatInputVisibleH = visibleTextH;
    int scrollY = 0;
    if (inputTextH > visibleTextH) {
        int caretLine = inputLines > 0 ? inputLines - 1 : 0;
        for (int i = 0; i < inputLines; i++) {
            int start = inputLinesBuf[i].start;
            int end = start + inputLinesBuf[i].len;
            if (chatCaret >= start && chatCaret <= end) {
                caretLine = i;
                break;
            }
        }
        int caretY = caretLine * lineHeight;
        if (caretY - chatInputScroll < 0) {
            chatInputScroll = caretY;
        } else if (caretY + lineHeight - chatInputScroll > visibleTextH) {
            chatInputScroll = caretY + lineHeight - visibleTextH;
        }
        int maxScroll = inputTextH - visibleTextH;
        if (chatInputScroll < 0) chatInputScroll = 0;
        if (chatInputScroll > maxScroll) chatInputScroll = maxScroll;
        scrollY = chatInputScroll;
    } else {
        chatInputScroll = 0;
        scrollY = 0;
    }
    SDL_Rect clip = { chatInputBox.x + inputPad, chatInputBox.y + 6, chatInputBox.w - (inputPad * 2), chatInputBox.h - 12 };
    SDL_RenderSetClipRect(ren, &clip);
    int textY = chatInputBox.y + 6 - scrollY;
    if (chat_has_selection()) {
        int selA = 0, selB = 0;
        chat_normalize_selection(&selA, &selB);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 80, 140, 200, 140);
        for (int i = 0; i < inputLines; i++) {
            int lineStart = inputLinesBuf[i].start;
            int lineEnd = lineStart + inputLinesBuf[i].len;
            int a = selA > lineStart ? selA : lineStart;
            int b = selB < lineEnd ? selB : lineEnd;
            if (b > a) {
                int selX1 = chatInputBox.x + inputPad + chat_input_substring_width(lineStart, a, textScale);
                int selX2 = chatInputBox.x + inputPad + chat_input_substring_width(lineStart, b, textScale);
                int selY = chatInputBox.y + 6 + (i * lineHeight) - scrollY;
                SDL_Rect selR = { selX1, selY, selX2 - selX1, lineHeight };
                SDL_RenderFillRect(ren, &selR);
            }
        }
    }
    for (int i = 0; i < inputLines; i++) {
        char lineBuf[CHAT_MSG_LEN];
        chat_copy_line_text(chatInput, inputLinesBuf[i], lineBuf, sizeof(lineBuf));
        if (lineBuf[0] == '\0') continue;
        int drawY = chatInputBox.y + 6 + (i * lineHeight) - scrollY;
        draw_text_scaled_topleft(ren, lineBuf, chatInputBox.x + inputPad, drawY, textCol, textScale);
    }
    SDL_RenderSetClipRect(ren, NULL);
    if (chatInputFocused) {
        Uint32 now = SDL_GetTicks();
        if ((now / 500) % 2 == 0) {
            int caretLine = inputLines > 0 ? inputLines - 1 : 0;
            for (int i = 0; i < inputLines; i++) {
                int start = inputLinesBuf[i].start;
                int end = start + inputLinesBuf[i].len;
                if (chatCaret >= start && chatCaret <= end) {
                    caretLine = i;
                    break;
                }
            }
            int caretXOff = 0;
            if (inputLines > 0) {
                int lineStart = inputLinesBuf[caretLine].start;
                caretXOff = chat_input_substring_width(lineStart, chatCaret, textScale);
            }
            int caretX = chatInputBox.x + inputPad + caretXOff + 2;
            int caretY = chatInputBox.y + 6 + (caretLine * lineHeight) - scrollY;
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 200);
            SDL_Rect caret = { caretX, caretY, 2, lineHeight };
            SDL_RenderFillRect(ren, &caret);
        }
    }
    if (inputTextH > visibleTextH) {
        int barW = 4;
        int barH = (int)((float)visibleTextH * ((float)visibleTextH / (float)inputTextH));
        if (barH < 8) barH = 8;
        int barX = chatInputBox.x + chatInputBox.w - barW - 4;
        int barY = chatInputBox.y + 6 + (int)((float)scrollY / (float)(inputTextH - visibleTextH) * (float)(visibleTextH - barH));
        SDL_SetRenderDrawColor(ren, 90, 110, 130, 200);
        SDL_Rect bar = { barX, barY, barW, barH };
        SDL_RenderFillRect(ren, &bar);
        chatInputScrollBarRect = (SDL_Rect){ barX, chatInputBox.y + 6, barW, visibleTextH };
        chatInputScrollThumbRect = bar;
    } else {
        chatInputScrollBarRect = (SDL_Rect){0, 0, 0, 0};
        chatInputScrollThumbRect = (SDL_Rect){0, 0, 0, 0};
    }

    int btnSize = 22;
    chatToggleBtn = (SDL_Rect){ x + w - btnSize - 10, y + (h - btnSize) / 2, btnSize, btnSize };
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    int hovered = point_in_rect(mx, my, chatToggleBtn);
    SDL_Color baseFill = {44, 56, 66, 90};
    SDL_Color baseStroke = {60, 75, 90, 120};
    SDL_Color hoverFill = {44, 56, 66, 255};
    SDL_Color hoverStroke = {90, 110, 130, 255};
    render_rounded_box(ren, chatToggleBtn, 6, hovered ? hoverFill : baseFill, hovered ? hoverStroke : baseStroke, 1);
    if (hovered) {
        int tw = 0, th = 0;
        text_size_scaled("<", 1.0f, &tw, &th);
        int tx = chatToggleBtn.x + (chatToggleBtn.w - tw) / 2;
        int ty = chatToggleBtn.y + (chatToggleBtn.h - th) / 2;
        draw_text_scaled_topleft(ren, "<", tx, ty, (SDL_Color){230, 240, 255, 255}, 1.0f);
    }

    chatResizeHandle = (SDL_Rect){ x, y + 10, 6, h - 20 };
    SDL_SetRenderDrawColor(ren, 40, 55, 65, 220);
    SDL_RenderFillRect(ren, &chatResizeHandle);

    if (chatContextVisible) {
        const char *items[4];
        int count = 0;
        if (chatContextType == 1) {
            items[0] = "Copy";
            items[1] = "Cut";
            items[2] = "Paste";
            items[3] = "Select All";
            count = 4;
        } else if (chatContextType == 2) {
            items[0] = "Copy";
            count = 1;
        }
        int itemH = 24;
        int pad = 6;
        int menuW = 140;
        int menuH = (count * itemH) + (pad * 2);
        int menuX = chatContextX;
        int menuY = chatContextY;
        if (menuX + menuW > x + w) menuX = x + w - menuW;
        if (menuX < x) menuX = x;
        if (menuY + menuH > y + h) menuY = y + h - menuH;
        if (menuY < y) menuY = y;
        chatContextRect = (SDL_Rect){ menuX, menuY, menuW, menuH };
        chatContextItemCount = count;

        render_rounded_box(ren, chatContextRect, 6, (SDL_Color){30, 40, 48, 245}, (SDL_Color){70, 90, 110, 255}, 1);
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        int pasteEnabled = SDL_HasClipboardText();
        for (int i = 0; i < count; i++) {
            SDL_Rect r = { menuX + 4, menuY + pad + (i * itemH), menuW - 8, itemH };
            chatContextItemRects[i] = r;
            int hovered = point_in_rect(mx, my, r);
            if (hovered) {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 60, 80, 100, 220);
                SDL_RenderFillRect(ren, &r);
            }
            SDL_Color tcol = {220, 230, 240, 255};
            if (chatContextType == 1 && i == 2 && !pasteEnabled) {
                tcol = (SDL_Color){150, 160, 170, 200};
            }
            int tw = 0, th = 0;
            text_size_scaled(items[i], 0.75f, &tw, &th);
            int tx = r.x + 8;
            int ty = r.y + (r.h - th) / 2;
            draw_text_scaled_topleft(ren, items[i], tx, ty, tcol, 0.75f);
        }
    }
}

void return_to_lobby(int broadcastToClients) {
    reset_round_state();
    for (int i = 0; i < 6; i++) readyFlags[i] = 0;
    gameStarted = 0;
    currentState = LOBBY;
    lobbyUserSized = 0;
    lobbyNeedsAutoSize = 1;
    lastLobbyPlayers = -1;
    lastLobbySpectators = -1;
    apply_lobby_window_size();
    if (broadcastToClients) {
        broadcast_return_lobby();
        broadcast_roster();
    }
    if (isServer && !lobbyLocked) {
        while (numPlayers < 6 && numSpectators > 0) {
            promote_spectator(0);
        }
    }
}

void close_all_client_sockets(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientSockets[i] >= 0) {
            close(clientSockets[i]);
            clientSockets[i] = -1;
        }
    }
    for (int i = 0; i < 6; i++) {
        if (spectatorSockets[i] >= 0) {
            close(spectatorSockets[i]);
            spectatorSockets[i] = -1;
        }
    }
}

void remove_spectator_at(int spectatorIdx) {
    if (spectatorIdx < 0 || spectatorIdx >= numSpectators) return;
    for (int i = spectatorIdx; i < numSpectators - 1; i++) {
        spectatorSockets[i] = spectatorSockets[i + 1];
        strcpy(spectatorNames[i], spectatorNames[i + 1]);
    }
    spectatorSockets[numSpectators - 1] = -1;
    spectatorNames[numSpectators - 1][0] = '\0';
    numSpectators--;
}

void remove_player_at(int playerId) {
    if (playerId <= 0 || playerId >= numPlayers) return;
    int sockIndex = playerId - 1;
    int wasCurrent = (playerId == currentPlayer);
    int removedBefore = (playerId < currentPlayer);
    clientSockets[sockIndex] = -1;

    for (int i = playerId; i < numPlayers - 1; i++) {
        clientSockets[i - 1] = clientSockets[i];
        readyFlags[i] = readyFlags[i + 1];
        scores[i] = scores[i + 1];
        strcpy(playerNames[i], playerNames[i + 1]);
    }
    clientSockets[numPlayers - 2] = -1;
    readyFlags[numPlayers - 1] = 0;
    scores[numPlayers - 1] = 0;
    playerNames[numPlayers - 1][0] = '\0';
    numPlayers--;

    if (numPlayers <= 0) {
        currentPlayer = 0;
    } else if (wasCurrent) {
        if (currentPlayer >= numPlayers) currentPlayer = 0;
    } else if (removedBefore) {
        if (currentPlayer > 0) currentPlayer--;
    }
    for (int i = 1; i < numPlayers; i++) {
        if (clientSockets[i - 1] >= 0) {
            Packet p = {0};
            p.type = PKT_ASSIGN;
            p.player = i;
            send_all(clientSockets[i - 1], &p, sizeof(p));
        }
    }
    request_resize(numPlayers);
}

void kick_player(int playerId) {
    if (playerId <= 0 || playerId >= numPlayers) return;
    int sockIndex = playerId - 1;
    if (numSpectators >= 6) return;
    int kickedSock = clientSockets[sockIndex];
    if (kickedSock < 0) return;

    spectatorSockets[numSpectators] = kickedSock;
    strncpy(spectatorNames[numSpectators], playerNames[playerId], 19);
    spectatorNames[numSpectators][19] = '\0';
    numSpectators++;

    Packet assign = {0};
    assign.type = PKT_ASSIGN;
    assign.player = -1;
    send_all(kickedSock, &assign, sizeof(assign));
    for (int i = playerId; i < numPlayers - 1; i++) {
        clientSockets[i - 1] = clientSockets[i];
        readyFlags[i] = readyFlags[i + 1];
        scores[i] = scores[i + 1];
        strcpy(playerNames[i], playerNames[i + 1]);
    }
    clientSockets[numPlayers - 2] = -1;
    readyFlags[numPlayers - 1] = 0;
    scores[numPlayers - 1] = 0;
    playerNames[numPlayers - 1][0] = '\0';
    numPlayers--;
    request_resize(numPlayers);
    for (int i = 1; i < numPlayers; i++) {
        if (clientSockets[i - 1] >= 0) {
            Packet p = {0};
            p.type = PKT_ASSIGN;
            p.player = i;
            send_all(clientSockets[i - 1], &p, sizeof(p));
        }
    }
    broadcast_roster();
}

int demote_player_to_spectator(int playerId) {
    if (playerId <= 0 || playerId >= numPlayers) return 0;
    if (numSpectators >= 6) return 0;

    int sockIndex = playerId - 1;
    int demoteSock = clientSockets[sockIndex];
    if (demoteSock < 0) return 0;

    spectatorSockets[numSpectators] = demoteSock;
    strncpy(spectatorNames[numSpectators], playerNames[playerId], 19);
    spectatorNames[numSpectators][19] = '\0';
    numSpectators++;

    Packet assign = {0};
    assign.type = PKT_ASSIGN;
    assign.player = -1;
    send_all(demoteSock, &assign, sizeof(assign));

    for (int i = playerId; i < numPlayers - 1; i++) {
        clientSockets[i - 1] = clientSockets[i];
        readyFlags[i] = readyFlags[i + 1];
        scores[i] = scores[i + 1];
        strcpy(playerNames[i], playerNames[i + 1]);
    }
    clientSockets[numPlayers - 2] = -1;
    readyFlags[numPlayers - 1] = 0;
    scores[numPlayers - 1] = 0;
    playerNames[numPlayers - 1][0] = '\0';
    numPlayers--;
    request_resize(numPlayers);
    for (int i = 1; i < numPlayers; i++) {
        if (clientSockets[i - 1] >= 0) {
            Packet p = {0};
            p.type = PKT_ASSIGN;
            p.player = i;
            send_all(clientSockets[i - 1], &p, sizeof(p));
        }
    }
    return 1;
}

void promote_spectator(int spectatorIdx) {
    if (spectatorIdx < 0 || spectatorIdx >= numSpectators) return;
    if (numPlayers >= 6) return;
    int sock = spectatorSockets[spectatorIdx];
    if (sock < 0) return;

    int assignedId = numPlayers;
    clientSockets[assignedId - 1] = sock;
    numPlayers++;
    readyFlags[assignedId] = 0;
    scores[assignedId] = 0;
    strncpy(playerNames[assignedId], spectatorNames[spectatorIdx], 19);
    playerNames[assignedId][19] = '\0';

    for (int i = spectatorIdx; i < numSpectators - 1; i++) {
        spectatorSockets[i] = spectatorSockets[i + 1];
        strcpy(spectatorNames[i], spectatorNames[i + 1]);
    }
    spectatorSockets[numSpectators - 1] = -1;
    spectatorNames[numSpectators - 1][0] = '\0';
    numSpectators--;

    Packet assign = {0};
    assign.type = PKT_ASSIGN;
    assign.player = assignedId;
    send_all(sock, &assign, sizeof(assign));

    request_resize(numPlayers);
    broadcast_roster();
}

void init_lobby_particles(void) {
    lobbyParticlesInit = 1;
    lobbyLastW = screenWidth;
    lobbyLastH = screenHeight;
    for (int i = 0; i < MAX_LOBBY_PARTICLES; i++) {
        lobbyParticles[i].x = (float)(rand() % (screenWidth + 1));
        lobbyParticles[i].y = (float)(rand() % (screenHeight + 1));
        lobbyParticles[i].vx = ((rand() % 100) / 1000.0f) + 0.02f;
        lobbyParticles[i].vy = ((rand() % 100) / 1000.0f) + 0.04f;
        lobbyParticles[i].size = (rand() % 3) + 1;
        lobbyParticles[i].alpha = (Uint8)(80 + rand() % 90);
    }
}

void adjust_grid_metrics(int players, SDL_Window *win) {
    // Free previous memory
    if (board != NULL) {
        for (int i = 0; i < gridSize; i++) free(board[i]);
        free(board);
    }
    if (lines != NULL) free(lines);

    numPlayers = players;
    gridSize = (players * 2) + 2;
    cellSize = (gridSize > 12) ? 45 : 60;
    screenWidth = gridSize * cellSize;
    screenHeight = (gridSize * cellSize) + 150;

    SDL_SetWindowSize(win, screenWidth, screenHeight);

    // Allocate memory
    board = malloc(gridSize * sizeof(Cell*));
    for (int i = 0; i < gridSize; i++) {
        board[i] = malloc(gridSize * sizeof(Cell));
        for (int j = 0; j < gridSize; j++) {
            board[i][j].letter = ' ';
            board[i][j].playerID = 0;
        }
    }
    lines = malloc(gridSize * gridSize * 4 * sizeof(SOSLine));
}

void draw_grid(SDL_Renderer *ren) {
    int gridW = screenWidth - chat_sidebar_width();
    SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
    for(int i = 0; i <= gridSize; i++) {
        // Vertical lines
        SDL_RenderDrawLine(ren, i * cellSize, 100, i * cellSize, 100 + gridSize * cellSize);
        // Horizontal lines
        SDL_RenderDrawLine(ren, 0, 100 + i * cellSize, gridW, 100 + i * cellSize);
    }
}

void draw_strikes(SDL_Renderer *ren) {
    for (int i = 0; i < lineCount; i++) {
        SDL_SetRenderDrawColor(ren, colors[lines[i].playerID].r, colors[lines[i].playerID].g, colors[lines[i].playerID].b, 255);
        SDL_RenderDrawLine(ren, lines[i].c1*cellSize+cellSize/2, 100+lines[i].r1*cellSize+cellSize/2, 
                                        lines[i].c2*cellSize+cellSize/2, 100+lines[i].r2*cellSize+cellSize/2);
    }
}

void draw_menu(SDL_Renderer *ren) {
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color grey = {200, 200, 200, 255};
    SDL_Rect textBox = { 50, 150, 200, 50 };

    draw_text(ren, "SOS GAME - B.TECH PROJECT", 50, 50, white);
    draw_text(ren, "Enter Number of Players (2-6):", 50, 120, grey);

    SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
    SDL_RenderDrawRect(ren, &textBox);
    draw_text(ren, numPlayersStr, 60, 160, white);

    draw_text(ren, "Press Enter to Continue", 50, 220, grey);
}

void draw_name_input(SDL_Renderer *ren) {
    int isSpectator = (localPlayerId < 0);
    char p[50];
    if (isSpectator) {
        snprintf(p, sizeof(p), "Name for Spectator:");
    } else {
        snprintf(p, sizeof(p), "Name for Player %d:", inputPlayerIdx + 1);
    }
    float scale = screenWidth / 600.0f;
    if (scale < 0.7f) scale = 0.7f;
    if (scale > 1.3f) scale = 1.3f;

    int margin = (int)(50 * scale);
    if (margin < 10) margin = 10;
    int boxW = screenWidth - (margin * 2);
    if (boxW < 200) boxW = screenWidth - 20;
    if (boxW < 120) boxW = 120;
    int topY = (int)(90 * scale);
    int boxH = (int)(40 * scale);
    if (boxH < 28) boxH = 28;
    int labelGap = (int)(10 * scale);
    SDL_Rect textBox = { (screenWidth - boxW) / 2, topY + (int)(26 * scale) + labelGap, boxW, boxH };
    nameInputBox = textBox;

    // Decorative header strip
    SDL_Rect strip = { textBox.x, textBox.y - (int)(18 * scale), textBox.w, (int)(10 * scale) };
    SDL_SetRenderDrawColor(ren, 40, 140, 200, 255);
    SDL_RenderFillRect(ren, &strip);

    // Soft panel backdrop
    SDL_Rect panel = { textBox.x - (int)(12 * scale), textBox.y - (int)(28 * scale), textBox.w + (int)(24 * scale), textBox.h + (int)(70 * scale) };
    SDL_SetRenderDrawColor(ren, 30, 30, 40, 180);
    SDL_RenderFillRect(ren, &panel);

    // Title text
    SDL_Color titleCol = isSpectator ? (SDL_Color){170, 200, 220, 255} : colors[inputPlayerIdx];
    draw_text_scaled_topleft(ren, p, textBox.x, topY, titleCol, scale);
    
    // Input box
    SDL_SetRenderDrawColor(ren, 90, 90, 110, 255);
    SDL_RenderDrawRect(ren, &textBox);
    SDL_SetRenderDrawColor(ren, 60, 60, 80, 255);
    SDL_Rect inner = { textBox.x + 2, textBox.y + 2, textBox.w - 4, textBox.h - 4 };
    SDL_RenderDrawRect(ren, &inner);
    char *nameBuf = isSpectator ? spectatorNameInput : playerNames[inputPlayerIdx];
    draw_text_scaled_topleft(ren, nameBuf, textBox.x + 10, textBox.y + (int)(8 * scale), (SDL_Color){255, 255, 255, 255}, scale);

    if (nameInputFocused) {
        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - nameCursorPulseStart;
        int show = ((elapsed / 500) % 2) == 0;
        if (show) {
            int textW = text_width_scaled(nameBuf, scale);
            int caretX = textBox.x + 10 + textW + (int)(2 * scale);
            int caretY = textBox.y + (int)(6 * scale);
            int caretW = (int)(2 * scale);
            if (caretW < 2) caretW = 2;
            int caretH = textBox.h - (int)(12 * scale);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 220);
            SDL_Rect caret = { caretX, caretY, caretW, caretH };
            SDL_RenderFillRect(ren, &caret);
        }
    }

    // Subtle dots for fun UI
    SDL_SetRenderDrawColor(ren, 255, 200, 80, 255);
    SDL_Rect dot1 = { textBox.x + textBox.w - (int)(18 * scale), textBox.y - (int)(14 * scale), (int)(6 * scale), (int)(6 * scale) };
    SDL_Rect dot2 = { textBox.x + textBox.w - (int)(30 * scale), textBox.y - (int)(14 * scale), (int)(6 * scale), (int)(6 * scale) };
    SDL_RenderFillRect(ren, &dot1);
    SDL_RenderFillRect(ren, &dot2);

    draw_text_scaled_topleft(ren, "Press Enter to confirm", textBox.x, textBox.y + boxH + (int)(16 * scale), (SDL_Color){120, 120, 120, 255}, scale);
}

void draw_live_scoreboard(SDL_Renderer *ren) {
    int contentW = screenWidth - chat_sidebar_width();
    for (int slot = 0; slot < numPlayers; slot++) {
        int i = (localPlayerId >= 0) ? (localPlayerId + slot) % numPlayers : slot;
        SDL_Color textCol = (i == currentPlayer) ? colors[i] : (SDL_Color){150, 150, 150, 255};
        char buf[50];
        sprintf(buf, "%s: %d", playerNames[i], scores[i]);
        
        int xPos = slot * (contentW / numPlayers) + 10;
        draw_text(ren, buf, xPos, 10, textCol);

        // Timeout Loading Bar Animation
        if (i == currentPlayer && !gameOver) {
            if (!isNetworked || isServer) {
                turnTimer -= frameDeltaSeconds;
                if (turnTimer <= 0.0f) {
                    turnTimer = TURN_DURATION_SECONDS;
                    currentPlayer = (currentPlayer + 1) % numPlayers;
                }
            }
            float progress = turnTimer / TURN_DURATION_SECONDS;
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
            SDL_Rect loader = { xPos, 45, (int)((contentW / numPlayers - 20) * progress), 6 };
            SDL_SetRenderDrawColor(ren, colors[i].r, colors[i].g, colors[i].b, 255);
            SDL_RenderFillRect(ren, &loader);
        }
    }
}

void draw_lobby(SDL_Renderer *ren) {
    int contentW = screenWidth - chat_sidebar_width();
    SDL_Color white = {230, 240, 255, 255};
    SDL_Color grey = {167, 179, 199, 255};
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    if (!lobbyParticlesInit || lobbyLastW != screenWidth || lobbyLastH != screenHeight) {
        init_lobby_particles();
    }

    // Animated background particles
    for (int i = 0; i < MAX_LOBBY_PARTICLES; i++) {
        lobbyParticles[i].x += lobbyParticles[i].vx;
        lobbyParticles[i].y += lobbyParticles[i].vy;
        if (lobbyParticles[i].x > screenWidth + 10) lobbyParticles[i].x = -10;
        if (lobbyParticles[i].y > screenHeight + 10) lobbyParticles[i].y = -10;
        SDL_SetRenderDrawColor(ren, 90, 110, 140, lobbyParticles[i].alpha);
        SDL_Rect p = { (int)lobbyParticles[i].x, (int)lobbyParticles[i].y, lobbyParticles[i].size, lobbyParticles[i].size };
        SDL_RenderFillRect(ren, &p);
    }

    // Top header
    SDL_Rect header = { 0, 0, contentW, 100 };
    SDL_SetRenderDrawColor(ren, 15, 25, 45, 230);
    SDL_RenderFillRect(ren, &header);
    SDL_Rect headerAccent = { 0, 92, contentW, 8 };
    SDL_SetRenderDrawColor(ren, 0, 229, 255, 255);
    SDL_RenderFillRect(ren, &headerAccent);

    draw_text(ren, "MULTIPLAYER LOBBY", 20, 20, white);
    if (isNetworked) {
        if (localPlayerId >= 0) {
            draw_text(ren, "Press SPACE to toggle Ready", 20, 60, grey);
        } else {
            draw_text(ren, "Spectator mode - watching live", 20, 60, grey);
        }
    } else {
        draw_text(ren, "Click READY to toggle, then START", 20, 60, grey);
    }

    // Lock button (host only)
    if (isNetworked && isServer) {
        const char *lockLabel = lobbyLocked ? "LOCKED" : "LOCK";
        int lw = 0, lh = 0;
        text_size_scaled(lockLabel, 1.0f, &lw, &lh);
        int padX = 20;
        int btnW = lw + padX * 2;
        if (btnW < 100) btnW = 100;
        lobbyLockBtn = (SDL_Rect){ contentW - btnW - 20, 26, btnW, 32 };
        SDL_SetRenderDrawColor(ren, lobbyLocked ? 180 : 0, lobbyLocked ? 70 : 229, lobbyLocked ? 80 : 255, 220);
        SDL_RenderFillRect(ren, &lobbyLockBtn);
        SDL_SetRenderDrawColor(ren, 220, 220, 220, 255);
        SDL_RenderDrawRect(ren, &lobbyLockBtn);
        int tx = lobbyLockBtn.x + (lobbyLockBtn.w - lw) / 2;
        int ty = lobbyLockBtn.y + (lobbyLockBtn.h - lh) / 2;
        draw_text(ren, lockLabel, tx, ty, (SDL_Color){8,19,31,255});
    } else {
        lobbyLockBtn = (SDL_Rect){0,0,0,0};
    }

    // Soft panel backdrop
    SDL_Rect panel = { 30, 120, contentW - 60, screenHeight - 160 };
    SDL_SetRenderDrawColor(ren, 26, 42, 68, 165);
    SDL_RenderFillRect(ren, &panel);
    SDL_SetRenderDrawColor(ren, 0, 229, 255, 180);
    SDL_RenderDrawRect(ren, &panel);

    int cardW = contentW - 100;
    int cardX = 50;
    int rowHPlayers = 52;
    int rowHSpectators = 44;
    int y = 140;

    // Players section
    draw_text(ren, "PLAYERS", cardX, y, (SDL_Color){200, 220, 240, 255});
    y += 30;
    for (int slot = 0; slot < numPlayers; slot++) {
        int i = (localPlayerId >= 0) ? (localPlayerId + slot) % numPlayers : slot;
        int rowY = y + slot * rowHPlayers;

        float pulse = 0.0f;
        Uint32 now = SDL_GetTicks();
        if (readyPulseStart[i] > 0) {
            Uint32 dt = now - readyPulseStart[i];
            if (dt < 450) {
                float t = dt / 450.0f;
                pulse = sinf(t * 3.14159f) * 6.0f;
            } else {
                readyPulseStart[i] = 0;
            }
        }

        SDL_Rect card = { cardX - (int)(pulse * 0.5f), rowY - (int)(pulse * 0.5f), cardW + (int)pulse, 42 + (int)pulse };
        lobbyPlayerRowRect[i] = (SDL_Rect){ cardX, rowY, cardW, 42 };
        SDL_SetRenderDrawColor(ren, 22, 35, 58, 210);
        SDL_RenderFillRect(ren, &card);
        SDL_SetRenderDrawColor(ren, 43, 59, 90, 200);
        SDL_RenderDrawRect(ren, &card);

        SDL_SetRenderDrawColor(ren, 0, 229, 255, 255);
        SDL_Rect avatar = { cardX + 10, rowY + 8, 26, 26 };
        SDL_RenderDrawRect(ren, &avatar);

        SDL_Color nameCol = readyFlags[i] ? colors[i] : (SDL_Color){160, 160, 160, 255};
        int nameX = cardX + 44;
        draw_text(ren, playerNames[i], nameX, rowY + 10, nameCol);

        const char *statusText = readyFlags[i] ? "READY" : "NOT READY";
        int statusW = text_width_scaled(statusText, 1.0f) + 24;
        if (statusW < 90) statusW = 90;
        if (statusW > 160) statusW = 160;
        SDL_Rect statusPill = { cardX + cardW - statusW - 16, rowY + 6, statusW, 30 };
        SDL_SetRenderDrawColor(ren, 0, 229, 255, readyFlags[i] ? 80 : 40);
        SDL_RenderFillRect(ren, &statusPill);
        SDL_SetRenderDrawColor(ren, 0, 229, 255, 160);
        SDL_RenderDrawRect(ren, &statusPill);
        draw_text(ren, statusText, statusPill.x + 12, statusPill.y + 4, (SDL_Color){0,229,255,255});

        int nameW = text_width_scaled(playerNames[i], 1.0f);
        int btnY = rowY + 9;
        int btnSize = 24;
        int minusX = nameX + nameW + 18;
        int plusX = minusX + btnSize + 10;
        int maxBtnX = statusPill.x - (btnSize * 2) - 16;
        if (plusX > maxBtnX) {
            plusX = maxBtnX;
            minusX = plusX - btnSize - 10;
        }

        lobbyKickBtn[i] = (SDL_Rect){0,0,0,0};
        lobbyPlusBtn[i] = (SDL_Rect){0,0,0,0};

        if (isNetworked && isServer && i != localPlayerId) {
            lobbyKickBtn[i] = (SDL_Rect){ minusX, btnY, btnSize, btnSize };
            SDL_SetRenderDrawColor(ren, 0, 229, 255, 210);
            SDL_RenderDrawRect(ren, &lobbyKickBtn[i]);
            SDL_SetRenderDrawColor(ren, 0, 229, 255, 80);
            SDL_Rect innerMinus = { lobbyKickBtn[i].x + 4, lobbyKickBtn[i].y + 4, lobbyKickBtn[i].w - 8, lobbyKickBtn[i].h - 8 };
            SDL_RenderFillRect(ren, &innerMinus);
            int mw = 0, mh = 0;
            text_size_scaled("-", 1.0f, &mw, &mh);
            int mx = lobbyKickBtn[i].x + (lobbyKickBtn[i].w - mw) / 2;
            int my = lobbyKickBtn[i].y + (lobbyKickBtn[i].h - mh) / 2;
            draw_text(ren, "-", mx, my, (SDL_Color){0,229,255,255});
        }
    }

    // Other players section
    int otherStartY = y + numPlayers * rowHPlayers + 30;
    draw_text(ren, "OTHER PLAYERS", cardX, otherStartY, (SDL_Color){200, 220, 240, 255});
    otherStartY += 22;
    for (int s = 0; s < numSpectators; s++) {
        int rowY = otherStartY + s * rowHSpectators;
        SDL_Rect card = { cardX, rowY, cardW, 32 };
        SDL_SetRenderDrawColor(ren, 18, 30, 50, 210);
        SDL_RenderFillRect(ren, &card);
        SDL_SetRenderDrawColor(ren, 43, 59, 90, 200);
        SDL_RenderDrawRect(ren, &card);

        char specLabel[32];
        if (spectatorNames[s][0] == '\0') {
            sprintf(specLabel, "Spectator %d", s + 1);
        } else {
            strncpy(specLabel, spectatorNames[s], sizeof(specLabel) - 1);
            specLabel[sizeof(specLabel) - 1] = '\0';
        }
        draw_text(ren, specLabel, cardX + 32, rowY + 6, (SDL_Color){170, 170, 190, 255});

        lobbyAddBtn[s] = (SDL_Rect){0,0,0,0};
        if (isNetworked && isServer && numPlayers < 6) {
            lobbyAddBtn[s] = (SDL_Rect){ cardX + cardW - 70, rowY + 4, 26, 26 };
            SDL_SetRenderDrawColor(ren, 0, 160, 120, 230);
            SDL_RenderFillRect(ren, &lobbyAddBtn[s]);
            SDL_SetRenderDrawColor(ren, 210, 255, 220, 255);
            SDL_Rect innerPlus = { lobbyAddBtn[s].x + 4, lobbyAddBtn[s].y + 4, lobbyAddBtn[s].w - 8, lobbyAddBtn[s].h - 8 };
            SDL_RenderFillRect(ren, &innerPlus);
            SDL_SetRenderDrawColor(ren, 0, 90, 60, 255);
            SDL_RenderDrawRect(ren, &lobbyAddBtn[s]);
            int pw = 0, ph = 0;
            text_size_scaled("+", 1.0f, &pw, &ph);
            int px = lobbyAddBtn[s].x + (lobbyAddBtn[s].w - pw) / 2;
            int py = lobbyAddBtn[s].y + (lobbyAddBtn[s].h - ph) / 2;
            draw_text(ren, "+", px, py, (SDL_Color){0,90,60,255});
        }
    }

    // Start button (host or local)
    if (!isNetworked || isServer) {
        int canStart = 0;
        if (!isNetworked) {
            canStart = (ready_count() >= 2);
        } else {
            canStart = (numPlayers >= 2 && all_ready());
        }
        lobbyStartBtn = (SDL_Rect){ cardX + cardW - 170, screenHeight - 60, 170, 40 };
        SDL_SetRenderDrawColor(ren, canStart ? 0 : 50, canStart ? 229 : 80, canStart ? 255 : 120, 220);
        SDL_RenderFillRect(ren, &lobbyStartBtn);
        SDL_SetRenderDrawColor(ren, 0, 229, 255, 200);
        SDL_RenderDrawRect(ren, &lobbyStartBtn);
        draw_text(ren, "START GAME", lobbyStartBtn.x + 14, lobbyStartBtn.y + 10, (SDL_Color){8,19,31,255});
    } else {
        lobbyStartBtn = (SDL_Rect){0,0,0,0};
    }

    // Watch live button (client players only)
    lobbyWatchBtn = (SDL_Rect){0,0,0,0};
    if (isNetworked && !isServer && localPlayerId >= 0) {
        int canWatch = (numSpectators < 6);
        lobbyWatchBtn = (SDL_Rect){ panel.x, screenHeight - 60, 170, 40 };
        SDL_SetRenderDrawColor(ren, canWatch ? 50 : 30, canWatch ? 120 : 60, canWatch ? 160 : 90, 220);
        SDL_RenderFillRect(ren, &lobbyWatchBtn);
        SDL_SetRenderDrawColor(ren, 180, 200, 220, 220);
        SDL_RenderDrawRect(ren, &lobbyWatchBtn);
        {
            int textW = 0, textH = 0;
            text_size_scaled("WATCH LIVE", 1.0f, &textW, &textH);
            int textX = lobbyWatchBtn.x + (lobbyWatchBtn.w - textW) / 2;
            int textY = lobbyWatchBtn.y + (lobbyWatchBtn.h - textH) / 2;
            draw_text(ren, "WATCH LIVE", textX, textY, (SDL_Color){230,240,255,255});
        }
    }

    draw_chat_sidebar(ren, contentW, 0, chat_sidebar_width(), screenHeight);
}

void launch_finale_burst(SDL_Color winnerColor) {
    int burstSize = 50;
    int startIdx = -1;

    // Find a block of inactive particles
    for(int i=0; i<MAX_FINALE_PARTICLES - burstSize; i++) {
        if(!finale[i].active) { startIdx = i; break; }
    }

    if(startIdx == -1) return;

    int rx = rand() % screenWidth;
    int ry = rand() % (screenHeight / 2);

    for(int i=0; i<burstSize; i++) {
        finale[startIdx + i].active = 1;
        finale[startIdx + i].x = rx;
        finale[startIdx + i].y = ry;
        float angle = (rand() % 360) * 3.14 / 180.0;
        float speed = (rand() % 50) / 10.0f + 2.0f;
        finale[startIdx + i].vx = cos(angle) * speed;
        finale[startIdx + i].vy = sin(angle) * speed;
        finale[startIdx + i].color = winnerColor;
        finale[startIdx + i].lifetime = 100 + rand() % 50;
    }
}

void draw_winner_finale(SDL_Renderer *ren, int winnerIdx) {
    static int burstDelay = 0;
    if (burstDelay++ % 30 == 0) launch_finale_burst(colors[winnerIdx]);

    for (int i = 0; i < MAX_FINALE_PARTICLES; i++) {
        if (finale[i].active) {
            finale[i].x += finale[i].vx;
            finale[i].y += finale[i].vy;
            finale[i].vy += 0.05f; // Gentle gravity
            finale[i].lifetime--;

            if (finale[i].lifetime <= 0) {
                finale[i].active = 0;
            } else {
                Uint8 alpha = (Uint8)((finale[i].lifetime / 150.0f) * 255);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, finale[i].color.r, finale[i].color.g, finale[i].color.b, alpha);
                SDL_Rect r = {(int)finale[i].x, (int)finale[i].y, 4, 4};
                SDL_RenderFillRect(ren, &r);
            }
        }
    }
}

int count_sos(int r, int c) {
    int found = 0;
    int dr[] = {0, 1, 1, 1}; 
    int dc[] = {1, 0, 1, -1};

    if (board[r][c].letter == 'O') {
        for (int i = 0; i < 4; i++) {
            int r1 = r - dr[i], c1 = c - dc[i], r2 = r + dr[i], c2 = c + dc[i];
            if (r1 >= 0 && r1 < gridSize && c1 >= 0 && c1 < gridSize &&
                r2 >= 0 && r2 < gridSize && c2 >= 0 && c2 < gridSize) {
                if (board[r1][c1].letter == 'S' && board[r2][c2].letter == 'S') {
                    lines[lineCount++] = (SOSLine){r1, c1, r2, c2, currentPlayer};
                    found++;
                }
            }
        }
    } else { 
        for (int i = 0; i < 4; i++) {
            for (int dir = -1; dir <= 1; dir += 2) {
                int r1 = r + (1 * dir * dr[i]), c1 = c + (1 * dir * dc[i]);
                int r2 = r + (2 * dir * dr[i]), c2 = c + (2 * dir * dc[i]);
                if (r2 >= 0 && r2 < gridSize && c2 >= 0 && c2 < gridSize) {
                    if (board[r1][c1].letter == 'O' && board[r2][c2].letter == 'S') {
                        lines[lineCount++] = (SOSLine){r, c, r2, c2, currentPlayer};
                        found++;
                    }
                }
            }
        }
    }
    return found;
}

void draw_s(SDL_Renderer *r, int x, int y, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    int p = cellSize/4;
    SDL_RenderDrawLine(r, x+p, y+p, x+cellSize-p, y+p);
    SDL_RenderDrawLine(r, x+p, y+p, x+p, y+cellSize/2);
    SDL_RenderDrawLine(r, x+p, y+cellSize/2, x+cellSize-p, y+cellSize/2);
    SDL_RenderDrawLine(r, x+cellSize-p, y+cellSize/2, x+cellSize-p, y+cellSize-p);
    SDL_RenderDrawLine(r, x+p, y+cellSize-p, x+cellSize-p, y+cellSize-p);
}

void draw_o(SDL_Renderer *r, int x, int y, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    SDL_Rect rect = {x+cellSize/4, y+cellSize/4, cellSize/2, cellSize/2};
    SDL_RenderDrawRect(r, &rect);
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    setvbuf(stdout, NULL, _IONBF, 0);

    for (int i = 0; i < MAX_CLIENTS; i++) clientSockets[i] = -1;

    {
        char ip[64] = "";
        int found = find_host_ip(ip, sizeof(ip));
        if (!found) {
            usleep(200000 + (rand() % 300000));
            found = find_host_ip(ip, sizeof(ip));
        }

        if (found) {
            isServer = 0;
            connect_to_server(ip);
            recv_all(sockfd, &localPlayerId, sizeof(localPlayerId));
            numPlayers = 1;
            if (localPlayerId < 0) {
                spectatorNameInput[0] = '\0';
                spectatorDeferredStart = 0;
            }
            pthread_create(&netThread, NULL, receive_moves_from_server, NULL);
        } else {
            if (!try_bind_discovery()) {
                if (find_host_ip(ip, sizeof(ip))) {
                    isServer = 0;
                    connect_to_server(ip);
                    recv_all(sockfd, &localPlayerId, sizeof(localPlayerId));
                    numPlayers = 1;
                    pthread_create(&netThread, NULL, receive_moves_from_server, NULL);
                } else {
                    printf("Unable to bind discovery or find host.\n");
                    return 1;
                }
            } else {
                isServer = 1;
                localPlayerId = 0;
                if (!start_server_socket()) {
                    printf("Failed to start server socket.\n");
                    return 1;
                }
                numPlayers = 1;
            }
        }
    }

    printf("Initializing SDL...\n");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }
    init_ttf();
    printf("Creating window...\n");
    SDL_Window *win = SDL_CreateWindow("B.Tech SOS Project", 100, 100, screenWidth, screenHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        return 1;
    }
    printf("Creating renderer...\n");
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) {
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        return 1;
    }
    printf("Window/renderer ready.\n");
    printf("Application running...\n");

    int running = 1;
    SDL_Event e;

    printf("Adjusting grid metrics...\n");
    adjust_grid_metrics(numPlayers, win);
    printf("Grid ready.\n");
    currentState = NAME_INPUT;
    inputPlayerIdx = localPlayerId;
    if (localPlayerId < 0) {
        currentState = NAME_INPUT;
        inputPlayerIdx = 0;
        spectatorNameInput[0] = '\0';
        focus_name_input(1);
        request_window_resize(520, 260);
    } else {
        focus_name_input(1);
        request_window_resize(520, 260);
    }

    if (isServer) {
        printf("Starting accept thread...\n");
        pthread_create(&acceptThread, NULL, accept_clients_thread, NULL);
        broadcast_roster();
    }

    printf("Starting text input...\n");
    SDL_StartTextInput(); // Start text input for the menu or name input

    Uint32 lastFrameTicks = SDL_GetTicks();
    while (running) {
        // heartbeat
        // printf("Loop...\n");
        Uint32 frameNow = SDL_GetTicks();
        frameDeltaSeconds = (frameNow - lastFrameTicks) / 1000.0f;
        if (frameDeltaSeconds < 0.0f) frameDeltaSeconds = 0.0f;
        if (frameDeltaSeconds > 0.2f) frameDeltaSeconds = 0.2f;
        lastFrameTicks = frameNow;
        SDL_StartTextInput();
        if (forceQuit) {
            if (forceQuitAt == 0) forceQuitAt = SDL_GetTicks();
            if (SDL_GetTicks() - forceQuitAt >= 1500) {
                printf("Host left - game closed.\n");
                running = 0;
                break;
            }
        }
        if (pendingWindowResize) {
            SDL_SetWindowSize(win, pendingWinW, pendingWinH);
            apply_window_size(pendingWinW, pendingWinH);
            pendingWindowResize = 0;
        }
        {
            int rw = 0, rh = 0;
            SDL_GetRendererOutputSize(ren, &rw, &rh);
            if (rw > 0 && rh > 0 && (rw != screenWidth || rh != screenHeight)) {
                apply_window_size(rw, rh);
            }
        }
        if (pendingResize) {
            pthread_mutex_lock(&gameMutex);
            adjust_grid_metrics(pendingPlayers, win);
            filledCells = 0;
            lineCount = 0;
            gameOver = 0;
            currentPlayer = 0;
            for (int i = 0; i < numPlayers; i++) scores[i] = 0;
            pendingResize = 0;
            pthread_mutex_unlock(&gameMutex);
        }
        if (pendingBoardState && board != NULL) {
            pthread_mutex_lock(&gameMutex);
            apply_board_state_packet(&pendingBoardPacket);
            pendingBoardState = 0;
            pthread_mutex_unlock(&gameMutex);
        }
        if (currentState == NAME_INPUT && !gameStarted) {
            if (screenWidth != 520 || screenHeight != 260) {
                request_window_resize(520, 260);
            }
        }
        if (currentState == LOBBY && !gameStarted) {
            if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) {
                lobbyUserSized = 1;
            }
            if (lastLobbyPlayers != numPlayers || lastLobbySpectators != numSpectators) {
                lobbyNeedsAutoSize = 1;
            }
            lastLobbyPlayers = numPlayers;
            lastLobbySpectators = numSpectators;
            if (!lobbyUserSized && lobbyNeedsAutoSize) {
                apply_lobby_window_size();
                lobbyNeedsAutoSize = 0;
            }
        }
        if (isServer && isNetworked && currentState == GAME && gameStarted) {
            Uint32 now = SDL_GetTicks();
            if (now - lastTimerSync >= 100) {
                broadcast_timer_sync();
                lastTimerSync = now;
            }
        }
        if (isServer && isNetworked && currentState == GAME && gameOver && gameOverAt > 0) {
            Uint32 now = SDL_GetTicks();
            if (now - gameOverAt >= 5000) {
                return_to_lobby(1);
            }
        }
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                if (isServer) {
                    printf("Host left - game closed.\n");
                    close_all_client_sockets();
                }
                running = 0;
            }
            
            // Handle window resize
                if (e.type == SDL_WINDOWEVENT) {
                    if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
                        e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        int rw = 0, rh = 0;
                        SDL_GetRendererOutputSize(ren, &rw, &rh);
                        if (rw > 0 && rh > 0) {
                            apply_window_size(rw, rh);
                        } else {
                            apply_window_size(e.window.data1, e.window.data2);
                        }
                        if (programmaticResizeInFlight &&
                            e.window.data1 == programmaticResizeW &&
                            e.window.data2 == programmaticResizeH) {
                            programmaticResizeInFlight = 0;
                        } else if (currentState == LOBBY && !gameStarted) {
                            lobbyUserSized = 1;
                        }
                    } else if (e.window.event == SDL_WINDOWEVENT_MAXIMIZED) {
                        int rw = 0, rh = 0;
                        SDL_GetRendererOutputSize(ren, &rw, &rh);
                        if (rw > 0 && rh > 0) {
                            apply_window_size(rw, rh);
                        }
                        programmaticResizeInFlight = 0;
                        if (currentState == LOBBY && !gameStarted) {
                            lobbyUserSized = 1;
                        }
                    }
                }

            if (e.type == SDL_MOUSEWHEEL && chatSidebarVisible && (currentState == LOBBY || currentState == GAME)) {
                float wheelY = (float)e.wheel.y;
#if SDL_VERSION_ATLEAST(2,0,18)
                if (wheelY == 0.0f && e.wheel.preciseY != 0.0f) {
                    wheelY = e.wheel.preciseY;
                }
#endif
                if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    wheelY = -wheelY;
                }
                int mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                if (point_in_rect(mx, my, chatInputBox) && chatInputTextH > chatInputVisibleH) {
                    chatInputScroll += (int)(-wheelY * 24.0f);
                } else {
                    chatScrollOffset += (int)(wheelY * 24.0f);
                }
            }

            if (currentState == MENU && isServer) {
                if (e.type == SDL_TEXTINPUT) {
                    if (strlen(numPlayersStr) < 1) {
                        strcat(numPlayersStr, e.text.text);
                    }
                } else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_BACKSPACE && strlen(numPlayersStr) > 0) {
                        numPlayersStr[strlen(numPlayersStr) - 1] = '\0';
                    } else if (e.key.keysym.sym == SDLK_RETURN) {
                        if (strlen(numPlayersStr) > 0) {
                            int players = atoi(numPlayersStr);
                            if (players >= 2 && players <= 6) {
                                adjust_grid_metrics(players, win);
                                isBotActive = 0; // Disable bot for LAN play
                                accept_clients(numPlayers - 1);
                                currentState = NAME_INPUT;
                                inputPlayerIdx = localPlayerId;
                                focus_name_input(1);
                            }
                        }
                    }
                }
            } else if (currentState == NAME_INPUT) {
                // ... (Name input logic remains the same) ...
                 if (e.type == SDL_MOUSEBUTTONDOWN) {
                    int mx = e.button.x;
                    int my = e.button.y;
                    int inside = (mx >= nameInputBox.x && mx <= nameInputBox.x + nameInputBox.w &&
                                  my >= nameInputBox.y && my <= nameInputBox.y + nameInputBox.h);
                    focus_name_input(inside);
                } else if (e.type == SDL_TEXTINPUT) {
                    if (!nameInputFocused) continue;
                    if (localPlayerId < 0) {
                        if (strlen(spectatorNameInput) < 19) {
                            strcat(spectatorNameInput, e.text.text);
                        }
                    } else {
                        if (strlen(playerNames[inputPlayerIdx]) < 19) {
                            strcat(playerNames[inputPlayerIdx], e.text.text);
                        }
                    }
                } else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_BACKSPACE && nameInputFocused) {
                        if (localPlayerId < 0) {
                            if (strlen(spectatorNameInput) > 0) {
                                spectatorNameInput[strlen(spectatorNameInput) - 1] = '\0';
                            }
                        } else if (strlen(playerNames[inputPlayerIdx]) > 0) {
                            playerNames[inputPlayerIdx][strlen(playerNames[inputPlayerIdx]) - 1] = '\0';
                        }
                    } else if (e.key.keysym.sym == SDLK_RETURN) {
                        if (localPlayerId < 0) {
                            if (strlen(spectatorNameInput) == 0) {
                                strncpy(spectatorNameInput, "Spectator", 19);
                                spectatorNameInput[19] = '\0';
                            }
                            if (isNetworked && !isServer) {
                                Packet p = {0};
                                p.type = PKT_NAME;
                                p.player = localPlayerId;
                                strncpy(p.name, spectatorNameInput, 19);
                                p.name[19] = '\0';
                                send_all(sockfd, &p, sizeof(p));
                            }
                            if (spectatorDeferredStart || gameStarted) {
                                currentState = GAME;
                                apply_game_window_size();
                                spectatorDeferredStart = 0;
                            } else {
                                currentState = LOBBY;
                                apply_lobby_window_size();
                                start_transition(NAME_INPUT, LOBBY, 450);
                            }
                        } else {
                            if (strlen(playerNames[inputPlayerIdx]) == 0) {
                                sprintf(playerNames[inputPlayerIdx], "P%d", inputPlayerIdx + 1);
                            }
                            if (isNetworked) {
                                if (isServer) {
                                    readyFlags[localPlayerId] = 0;
                                    broadcast_roster();
                                } else {
                                    Packet p = {0};
                                    p.type = PKT_NAME;
                                    p.player = localPlayerId;
                                    strncpy(p.name, playerNames[localPlayerId], 19);
                                    p.name[19] = '\0';
                                    send_all(sockfd, &p, sizeof(p));
                                    readyFlags[localPlayerId] = 0;
                                }
                                currentState = LOBBY;
                                apply_lobby_window_size();
                                start_transition(NAME_INPUT, LOBBY, 450);
                            } else {
                                inputPlayerIdx++;
                                if (inputPlayerIdx >= numPlayers) {
                                    for (int i = 0; i < numPlayers; i++) readyFlags[i] = 0;
                                    currentState = LOBBY;
                                    apply_lobby_window_size();
                                } else {
                                    focus_name_input(1);
                                }
                            }
                        }
                    }
                }
            } else if (currentState == LOBBY) {
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    int mx = e.button.x;
                    int my = e.button.y;
                    if (e.button.button == SDL_BUTTON_LEFT && chatContextVisible) {
                        if (chat_context_handle_click(mx, my)) {
                            continue;
                        }
                        chat_context_close();
                        continue;
                    }
                    if (e.button.button == SDL_BUTTON_RIGHT) {
                        int bubbleIdx = chat_bubble_at(mx, my);
                        if (bubbleIdx >= 0) {
                            chat_context_open(mx, my, 2, bubbleIdx);
                            continue;
                        }
                        if (point_in_rect(mx, my, chatInputBox)) {
                            chat_context_open(mx, my, 1, -1);
                            chatInputFocused = 1;
                            SDL_StartTextInput();
                            continue;
                        }
                        chat_context_close();
                        continue;
                    }
                    if (handle_chat_bubble_copy(mx, my)) {
                        continue;
                    }
                    if (point_in_rect(mx, my, chatListScrollThumbRect)) {
                        chatListScrollDragging = 1;
                        chatListScrollDragStartY = my;
                        chatListScrollStartOffset = chatScrollOffset;
                        continue;
                    }
                    if (point_in_rect(mx, my, chatInputScrollThumbRect)) {
                        chatInputScrollDragging = 1;
                        chatInputScrollDragStartY = my;
                        chatInputScrollStartOffset = chatInputScroll;
                        continue;
                    }
                    if (point_in_rect(mx, my, chatListScrollBarRect)) {
                        int barY = chatListScrollBarRect.y;
                        int barH = chatListScrollBarRect.h;
                        int thumbH = chatListScrollThumbRect.h;
                        if (barH > 0 && thumbH > 0) {
                            int clickY = my - barY - (thumbH / 2);
                            if (clickY < 0) clickY = 0;
                            if (clickY > barH - thumbH) clickY = barH - thumbH;
                            float ratio = (barH - thumbH) > 0 ? (float)clickY / (float)(barH - thumbH) : 0.0f;
                            int maxOffset = 0;
                            int totalH = 0;
                            int start = (chatHead - chatCount + CHAT_MAX_LINES) % CHAT_MAX_LINES;
                            int bubbleMaxW = chatSidebarWidth - (12 * 2) - 18;
                            if (bubbleMaxW < 60) bubbleMaxW = 60;
                            float msgScale = 0.75f;
                            float nameScale = 0.8f;
                            for (int i = 0; i < chatCount; i++) {
                                int idx = (start + i) % CHAT_MAX_LINES;
                                int sender = chatLogSender[idx];
                                int isSelf = (localPlayerId >= 0 && sender == localPlayerId);
                                int nameH = 0;
                                if (!isSelf) {
                                    int nw = 0, nh = 0;
                                    text_size_scaled(chatLogName[idx], nameScale, &nw, &nh);
                                    nameH = nh + 4;
                                }
                                int textH = measure_text_wrapped_height_scaled(chatLog[idx], bubbleMaxW - 16, msgScale);
                                int bubbleH = textH + 14;
                                int timeH = 14;
                                totalH += nameH + bubbleH + timeH + 8;
                            }
                            maxOffset = totalH - barH;
                            if (maxOffset < 0) maxOffset = 0;
                            chatScrollOffset = (int)(ratio * (float)maxOffset);
                        }
                        continue;
                    }
                    if (point_in_rect(mx, my, chatInputScrollBarRect)) {
                        int barY = chatInputScrollBarRect.y;
                        int barH = chatInputScrollBarRect.h;
                        int thumbH = chatInputScrollThumbRect.h;
                        int maxScroll = chatInputTextH - chatInputVisibleH;
                        if (barH > 0 && thumbH > 0 && maxScroll > 0) {
                            int clickY = my - barY - (thumbH / 2);
                            if (clickY < 0) clickY = 0;
                            if (clickY > barH - thumbH) clickY = barH - thumbH;
                            float ratio = (barH - thumbH) > 0 ? (float)clickY / (float)(barH - thumbH) : 0.0f;
                            chatInputScroll = (int)(ratio * (float)maxScroll);
                        }
                        continue;
                    }
                    if (point_in_rect(mx, my, chatToggleBtn)) {
                        chatSidebarVisible = !chatSidebarVisible;
                        if (chatSidebarVisible) {
                            if (chatSidebarUserSized) {
                                if (chatSidebarWidth < chatSidebarMin) chatSidebarWidth = chatSidebarMin;
                                if (chatSidebarWidth > chatSidebarMax) chatSidebarWidth = chatSidebarMax;
                            } else {
                                chatSidebarWidth = (int)(screenWidth * chatSidebarRatio);
                                if (chatSidebarWidth < chatSidebarMin) chatSidebarWidth = chatSidebarMin;
                                if (chatSidebarWidth > chatSidebarMax) chatSidebarWidth = chatSidebarMax;
                            }
                        }
                        if (currentState == LOBBY && !gameStarted) {
                            lobbyNeedsAutoSize = 1;
                        }
                        apply_lobby_window_size();
                        continue;
                    }
                    if (point_in_rect(mx, my, chatResizeHandle)) {
                        chatResizing = 1;
                        chatResizeStartX = mx;
                        chatResizeStartW = chatSidebarWidth;
                        continue;
                    }
                    if (point_in_rect(mx, my, chatInputBox)) {
                        chatInputFocused = 1;
                        SDL_StartTextInput();
                        SDL_Keymod mod = SDL_GetModState();
                        int selecting = (mod & KMOD_SHIFT) != 0;
                        if (!selecting) {
                            chat_update_caret_from_mouse(mx, my, 0);
                            chatSelecting = 1;
                        } else {
                            chat_update_caret_from_mouse(mx, my, 1);
                            chatSelecting = 1;
                        }
                    } else {
                        chatInputFocused = 0;
                        chatSelecting = 0;
                    }
                }
                if (e.type == SDL_MOUSEBUTTONUP) {
                    chatResizing = 0;
                    if (e.button.button == SDL_BUTTON_LEFT) chatInputScrollDragging = 0;
                    if (e.button.button == SDL_BUTTON_LEFT) chatListScrollDragging = 0;
                    if (e.button.button == SDL_BUTTON_LEFT) chatSelecting = 0;
                }
                if (e.type == SDL_MOUSEMOTION) {
                    if (chatResizing) {
                        int dx = chatResizeStartX - e.motion.x;
                        int newW = chatResizeStartW + dx;
                        if (newW < chatSidebarMin) newW = chatSidebarMin;
                        if (newW > chatSidebarMax) newW = chatSidebarMax;
                        chatSidebarWidth = newW;
                        chatSidebarUserSized = 1;
                        if (screenWidth > 0) {
                            chatSidebarRatio = (float)chatSidebarWidth / (float)screenWidth;
                        }
                        int contentW = screenWidth - chatSidebarWidth;
                        if (contentW < 520) contentW = 520;
                        request_window_resize(contentW + chatSidebarWidth, screenHeight);
                    } else if (chatListScrollDragging && chatListScrollBarRect.h > 0 && chatListScrollThumbRect.h > 0) {
                        int dy = e.motion.y - chatListScrollDragStartY;
                        int maxOffset = 0;
                        int listH = chatListScrollBarRect.h;
                        int thumbH = chatListScrollThumbRect.h;
                        if (listH > 0 && thumbH > 0 && listH > thumbH) {
                            float ratio = (float)(listH - thumbH);
                            float scrollRatio = (float)dy / ratio;
                            maxOffset = 0;
                            int totalH = 0;
                            int start = (chatHead - chatCount + CHAT_MAX_LINES) % CHAT_MAX_LINES;
                            int bubbleMaxW = chatSidebarWidth - (12 * 2) - 18;
                            if (bubbleMaxW < 60) bubbleMaxW = 60;
                            float msgScale = 0.75f;
                            float nameScale = 0.8f;
                            for (int i = 0; i < chatCount; i++) {
                                int idx = (start + i) % CHAT_MAX_LINES;
                                int sender = chatLogSender[idx];
                                int isSelf = (localPlayerId >= 0 && sender == localPlayerId);
                                int nameH = 0;
                                if (!isSelf) {
                                    int nw = 0, nh = 0;
                                    text_size_scaled(chatLogName[idx], nameScale, &nw, &nh);
                                    nameH = nh + 4;
                                }
                                int textH = measure_text_wrapped_height_scaled(chatLog[idx], bubbleMaxW - 16, msgScale);
                                int bubbleH = textH + 14;
                                int timeH = 14;
                                totalH += nameH + bubbleH + timeH + 8;
                            }
                            maxOffset = totalH - listH;
                            if (maxOffset < 0) maxOffset = 0;
                            chatScrollOffset = chatListScrollStartOffset + (int)(scrollRatio * (float)maxOffset);
                        }
                    } else if (chatInputScrollDragging && chatInputScrollBarRect.h > 0 && chatInputScrollThumbRect.h > 0) {
                        int dy = e.motion.y - chatInputScrollDragStartY;
                        int barH = chatInputScrollBarRect.h;
                        int thumbH = chatInputScrollThumbRect.h;
                        int maxScroll = chatInputTextH - chatInputVisibleH;
                        if (barH > 0 && thumbH > 0 && maxScroll > 0) {
                            float ratio = (float)(barH - thumbH);
                            float scrollRatio = (ratio > 0.0f) ? ((float)dy / ratio) : 0.0f;
                            chatInputScroll = chatInputScrollStartOffset + (int)(scrollRatio * (float)maxScroll);
                        }
                    } else if (chatSelecting && chatInputFocused) {
                        chat_update_caret_from_mouse(e.motion.x, e.motion.y, 1);
                    }
                }
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE) {
                    int shouldBroadcastReady = 0;
                    int shouldBroadcastStart = 0;
                    int readyVal = 0;
                    if (chatInputFocused) {
                        continue;
                    }
                    if (!isNetworked) {
                        if (ready_count() >= 2) {
                            reset_round_state();
                            gameStarted = 1;
                            currentState = GAME;
                            apply_game_window_size();
                        }
                    } else {
                        if (localPlayerId < 0) {
                            continue;
                        }
                        pthread_mutex_lock(&gameMutex);
                        readyFlags[localPlayerId] = !readyFlags[localPlayerId];
                        readyVal = readyFlags[localPlayerId];
                        readyPulseStart[localPlayerId] = SDL_GetTicks();
                        if (isServer) {
                            shouldBroadcastReady = 1;
                        } else {
                            Packet p = {0};
                            p.type = PKT_READY;
                            p.player = localPlayerId;
                            p.ready = readyFlags[localPlayerId];
                            send_all(sockfd, &p, sizeof(p));
                        }
                        pthread_mutex_unlock(&gameMutex);
                        if (isServer && shouldBroadcastReady) {
                            broadcast_ready_update(localPlayerId, readyVal);
                        }
                        if (isServer && shouldBroadcastStart) {
                            broadcast_start_game();
                        }
                    }
                }
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    int mx = e.button.x;
                    int my = e.button.y;
                    if (isNetworked && !isServer && localPlayerId >= 0 && point_in_rect(mx, my, lobbyWatchBtn)) {
                        Packet p = {0};
                        p.type = PKT_ROLE;
                        p.player = -1;
                        send_all(sockfd, &p, sizeof(p));
                    }
                    if (isNetworked && isServer && point_in_rect(mx, my, lobbyLockBtn)) {
                        lobbyLocked = !lobbyLocked;
                    }
                    if (isServer) {
                        for (int i = 1; i < numPlayers; i++) {
                            if (point_in_rect(mx, my, lobbyKickBtn[i])) {
                                kick_player(i);
                                break;
                            }
                        }
                        for (int s = 0; s < numSpectators; s++) {
                            if (point_in_rect(mx, my, lobbyAddBtn[s])) {
                                promote_spectator(s);
                                break;
                            }
                        }
                    }
                    if (point_in_rect(mx, my, lobbyStartBtn)) {
                        if (!isNetworked) {
                            if (ready_count() >= 2) {
                                reset_round_state();
                                gameStarted = 1;
                                currentState = GAME;
                                apply_game_window_size();
                            }
                        } else if (isServer) {
                            if (numPlayers >= 2 && all_ready()) {
                                gameStarted = 1;
                                currentState = GAME;
                                apply_game_window_size();
                                broadcast_start_game();
                            }
                        }
                    }
                }
                if (e.type == SDL_TEXTINPUT && chatInputFocused) {
                    chat_insert_text(e.text.text);
                } else if (e.type == SDL_KEYDOWN && chatInputFocused) {
                    chat_context_close();
                    SDL_Keymod mod = SDL_GetModState();
                    int ctrl = (mod & KMOD_CTRL) || (mod & KMOD_GUI);
                    int shift = (mod & KMOD_SHIFT);
                    if (ctrl && e.key.keysym.sym == SDLK_c) {
                        if (chat_has_selection()) {
                            int a = 0, b = 0;
                            chat_normalize_selection(&a, &b);
                            char buf[CHAT_MSG_LEN];
                            int len = b - a;
                            if (len > CHAT_MSG_LEN - 1) len = CHAT_MSG_LEN - 1;
                            memcpy(buf, chatInput + a, (size_t)len);
                            buf[len] = '\0';
                            SDL_SetClipboardText(buf);
                        } else {
                            SDL_SetClipboardText(chatInput);
                        }
                    } else if (ctrl && e.key.keysym.sym == SDLK_x) {
                        if (chat_has_selection()) {
                            int a = 0, b = 0;
                            chat_normalize_selection(&a, &b);
                            char buf[CHAT_MSG_LEN];
                            int len = b - a;
                            if (len > CHAT_MSG_LEN - 1) len = CHAT_MSG_LEN - 1;
                            memcpy(buf, chatInput + a, (size_t)len);
                            buf[len] = '\0';
                            SDL_SetClipboardText(buf);
                            chat_delete_selection();
                        } else {
                            SDL_SetClipboardText(chatInput);
                            chatInput[0] = '\0';
                            chatCaret = 0;
                        }
                    } else if (ctrl && e.key.keysym.sym == SDLK_v) {
                        if (SDL_HasClipboardText()) {
                            char *clip = SDL_GetClipboardText();
                            chat_insert_text(clip);
                            SDL_free(clip);
                        }
                    } else if (ctrl && e.key.keysym.sym == SDLK_a) {
                        chatSelStart = 0;
                        chatSelEnd = (int)strlen(chatInput);
                        chatCaret = chatSelEnd;
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        if (chat_has_selection()) {
                            chat_delete_selection();
                        } else if (chatCaret > 0) {
                            memmove(chatInput + chatCaret - 1, chatInput + chatCaret, strlen(chatInput) - (size_t)chatCaret + 1);
                            chatCaret--;
                        }
                    } else if (e.key.keysym.sym == SDLK_DELETE) {
                        if (chat_has_selection()) {
                            chat_delete_selection();
                        } else {
                            int len = (int)strlen(chatInput);
                            if (chatCaret < len) {
                                memmove(chatInput + chatCaret, chatInput + chatCaret + 1, strlen(chatInput) - (size_t)chatCaret);
                            }
                        }
                    } else if (e.key.keysym.sym == SDLK_LEFT) {
                        chat_set_caret(chatCaret - 1, shift);
                    } else if (e.key.keysym.sym == SDLK_RIGHT) {
                        chat_set_caret(chatCaret + 1, shift);
                    } else if (e.key.keysym.sym == SDLK_HOME || e.key.keysym.sym == SDLK_END || e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN) {
                        int inputPad = 12;
                        int textMaxW = chatInputBox.w - (inputPad * 2);
                        if (textMaxW < 0) textMaxW = 0;
                        int baseW = 0, baseH = 0;
                        text_size_scaled("Ag", 1.0f, &baseW, &baseH);
                        float textScale = 0.85f;
                        if (textScale > 1.0f) textScale = 1.0f;
                        if (textScale < 0.6f) textScale = 0.6f;
                        int lineHeight = (int)((float)baseH * textScale);
                        if (lineHeight < 10) lineHeight = 10;
                        ChatLine linesBuf[CHAT_MSG_LEN];
                        int lineCount = layout_chat_input_lines(chatInput, textMaxW, textScale, linesBuf, CHAT_MSG_LEN);
                        int curLine = 0;
                        for (int i = 0; i < lineCount; i++) {
                            int start = linesBuf[i].start;
                            int end = start + linesBuf[i].len;
                            if (chatCaret >= start && chatCaret <= end) {
                                curLine = i;
                                break;
                            }
                        }
                        if (e.key.keysym.sym == SDLK_HOME) {
                            chat_set_caret(linesBuf[curLine].start, shift);
                        } else if (e.key.keysym.sym == SDLK_END) {
                            chat_set_caret(linesBuf[curLine].start + linesBuf[curLine].len, shift);
                        } else if (e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN) {
                            int dir = (e.key.keysym.sym == SDLK_UP) ? -1 : 1;
                            int targetLine = curLine + dir;
                            if (targetLine < 0) targetLine = 0;
                            if (targetLine >= lineCount) targetLine = lineCount - 1;
                            if (chatCaretPreferredX < 0) {
                                int lineStart = linesBuf[curLine].start;
                                chatCaretPreferredX = chat_input_substring_width(lineStart, chatCaret, textScale);
                            }
                            int targetStart = linesBuf[targetLine].start;
                            int targetLen = linesBuf[targetLine].len;
                            int targetIdx = chat_find_index_at_x(targetStart, targetLen, textScale, chatCaretPreferredX);
                            chat_set_caret(targetIdx, shift);
                        }
                    } else if (e.key.keysym.sym == SDLK_RETURN) {
                        if (e.key.keysym.mod & KMOD_SHIFT) {
                            chat_insert_text("\n");
                        } else {
                            send_chat_from_input();
                        }
                    } else if (e.key.keysym.sym == SDLK_PAGEUP) {
                        chatScrollOffset += 3;
                    } else if (e.key.keysym.sym == SDLK_PAGEDOWN) {
                        chatScrollOffset -= 3;
                    }
                }
            } else if (currentState == GAME) {
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    int mx = e.button.x;
                    int my = e.button.y;
                    if (e.button.button == SDL_BUTTON_LEFT && chatContextVisible) {
                        if (chat_context_handle_click(mx, my)) {
                            continue;
                        }
                        chat_context_close();
                        continue;
                    }
                    if (e.button.button == SDL_BUTTON_RIGHT) {
                        int bubbleIdx = chat_bubble_at(mx, my);
                        if (bubbleIdx >= 0) {
                            chat_context_open(mx, my, 2, bubbleIdx);
                            continue;
                        }
                        if (point_in_rect(mx, my, chatInputBox)) {
                            chat_context_open(mx, my, 1, -1);
                            chatInputFocused = 1;
                            SDL_StartTextInput();
                            continue;
                        }
                        chat_context_close();
                        continue;
                    }
                    if (handle_chat_bubble_copy(mx, my)) {
                        continue;
                    }
                    if (point_in_rect(mx, my, chatListScrollThumbRect)) {
                        chatListScrollDragging = 1;
                        chatListScrollDragStartY = my;
                        chatListScrollStartOffset = chatScrollOffset;
                        continue;
                    }
                    if (point_in_rect(mx, my, chatInputScrollThumbRect)) {
                        chatInputScrollDragging = 1;
                        chatInputScrollDragStartY = my;
                        chatInputScrollStartOffset = chatInputScroll;
                        continue;
                    }
                    if (point_in_rect(mx, my, chatInputScrollBarRect)) {
                        int barY = chatInputScrollBarRect.y;
                        int barH = chatInputScrollBarRect.h;
                        int thumbH = chatInputScrollThumbRect.h;
                        int maxScroll = chatInputTextH - chatInputVisibleH;
                        if (barH > 0 && thumbH > 0 && maxScroll > 0) {
                            int clickY = my - barY - (thumbH / 2);
                            if (clickY < 0) clickY = 0;
                            if (clickY > barH - thumbH) clickY = barH - thumbH;
                            float ratio = (barH - thumbH) > 0 ? (float)clickY / (float)(barH - thumbH) : 0.0f;
                            chatInputScroll = (int)(ratio * (float)maxScroll);
                        }
                        continue;
                    }
                    if (point_in_rect(mx, my, chatToggleBtn)) {
                    chatSidebarVisible = !chatSidebarVisible;
                    if (chatSidebarVisible) {
                        if (chatSidebarUserSized) {
                            if (chatSidebarWidth < chatSidebarMin) chatSidebarWidth = chatSidebarMin;
                            if (chatSidebarWidth > chatSidebarMax) chatSidebarWidth = chatSidebarMax;
                        } else {
                            chatSidebarWidth = (int)(screenWidth * chatSidebarRatio);
                            if (chatSidebarWidth < chatSidebarMin) chatSidebarWidth = chatSidebarMin;
                            if (chatSidebarWidth > chatSidebarMax) chatSidebarWidth = chatSidebarMax;
                        }
                    }
                    if (currentState == LOBBY && !gameStarted) {
                        lobbyNeedsAutoSize = 1;
                    }
                        apply_game_window_size();
                        continue;
                    }
                    if (point_in_rect(mx, my, chatResizeHandle)) {
                        chatResizing = 1;
                        chatResizeStartX = mx;
                        chatResizeStartW = chatSidebarWidth;
                        continue;
                    }
                    if (point_in_rect(mx, my, chatInputBox)) {
                        chatInputFocused = 1;
                        SDL_StartTextInput();
                        SDL_Keymod mod = SDL_GetModState();
                        int selecting = (mod & KMOD_SHIFT) != 0;
                        if (!selecting) {
                            chat_update_caret_from_mouse(mx, my, 0);
                            chatSelecting = 1;
                        } else {
                            chat_update_caret_from_mouse(mx, my, 1);
                            chatSelecting = 1;
                        }
                    } else {
                        chatInputFocused = 0;
                        chatSelecting = 0;
                    }
                }
                if (e.type == SDL_MOUSEBUTTONUP) {
                    chatResizing = 0;
                    if (e.button.button == SDL_BUTTON_LEFT) chatInputScrollDragging = 0;
                    if (e.button.button == SDL_BUTTON_LEFT) chatListScrollDragging = 0;
                    if (e.button.button == SDL_BUTTON_LEFT) chatSelecting = 0;
                }
                if (e.type == SDL_MOUSEMOTION) {
                    if (chatResizing) {
                        int dx = chatResizeStartX - e.motion.x;
                        int newW = chatResizeStartW + dx;
                        if (newW < chatSidebarMin) newW = chatSidebarMin;
                        if (newW > chatSidebarMax) newW = chatSidebarMax;
                        chatSidebarWidth = newW;
                        chatSidebarUserSized = 1;
                        if (screenWidth > 0) {
                        chatSidebarRatio = (float)chatSidebarWidth / (float)screenWidth;
                    }
                    int contentW = screenWidth - chatSidebarWidth;
                    request_window_resize(contentW + chatSidebarWidth, screenHeight);
                    } else if (chatListScrollDragging && chatListScrollBarRect.h > 0 && chatListScrollThumbRect.h > 0) {
                        int dy = e.motion.y - chatListScrollDragStartY;
                        int maxOffset = 0;
                        int listH = chatListScrollBarRect.h;
                        int thumbH = chatListScrollThumbRect.h;
                        if (listH > 0 && thumbH > 0 && listH > thumbH) {
                            float ratio = (float)(listH - thumbH);
                            float scrollRatio = (float)dy / ratio;
                            maxOffset = 0;
                            int totalH = 0;
                            int start = (chatHead - chatCount + CHAT_MAX_LINES) % CHAT_MAX_LINES;
                            int bubbleMaxW = chatSidebarWidth - (12 * 2) - 18;
                            if (bubbleMaxW < 60) bubbleMaxW = 60;
                            float msgScale = 0.75f;
                            float nameScale = 0.8f;
                            for (int i = 0; i < chatCount; i++) {
                                int idx = (start + i) % CHAT_MAX_LINES;
                                int sender = chatLogSender[idx];
                                int isSelf = (localPlayerId >= 0 && sender == localPlayerId);
                                int nameH = 0;
                                if (!isSelf) {
                                    int nw = 0, nh = 0;
                                    text_size_scaled(chatLogName[idx], nameScale, &nw, &nh);
                                    nameH = nh + 4;
                                }
                                int textH = measure_text_wrapped_height_scaled(chatLog[idx], bubbleMaxW - 16, msgScale);
                                int bubbleH = textH + 14;
                                int timeH = 14;
                                totalH += nameH + bubbleH + timeH + 8;
                            }
                            maxOffset = totalH - listH;
                            if (maxOffset < 0) maxOffset = 0;
                            chatScrollOffset = chatListScrollStartOffset + (int)(scrollRatio * (float)maxOffset);
                        }
                    } else if (chatInputScrollDragging && chatInputScrollBarRect.h > 0 && chatInputScrollThumbRect.h > 0) {
                        int dy = e.motion.y - chatInputScrollDragStartY;
                        int barH = chatInputScrollBarRect.h;
                        int thumbH = chatInputScrollThumbRect.h;
                        int maxScroll = chatInputTextH - chatInputVisibleH;
                        if (barH > 0 && thumbH > 0 && maxScroll > 0) {
                            float ratio = (float)(barH - thumbH);
                            float scrollRatio = (ratio > 0.0f) ? ((float)dy / ratio) : 0.0f;
                            chatInputScroll = chatInputScrollStartOffset + (int)(scrollRatio * (float)maxScroll);
                        }
                    } else if (chatSelecting && chatInputFocused) {
                        chat_update_caret_from_mouse(e.motion.x, e.motion.y, 1);
                    }
                }
                if (e.type == SDL_TEXTINPUT && chatInputFocused) {
                    chat_insert_text(e.text.text);
                } else if (e.type == SDL_KEYDOWN && chatInputFocused) {
                    chat_context_close();
                    SDL_Keymod mod = SDL_GetModState();
                    int ctrl = (mod & KMOD_CTRL) || (mod & KMOD_GUI);
                    int shift = (mod & KMOD_SHIFT);
                    if (ctrl && e.key.keysym.sym == SDLK_c) {
                        if (chat_has_selection()) {
                            int a = 0, b = 0;
                            chat_normalize_selection(&a, &b);
                            char buf[CHAT_MSG_LEN];
                            int len = b - a;
                            if (len > CHAT_MSG_LEN - 1) len = CHAT_MSG_LEN - 1;
                            memcpy(buf, chatInput + a, (size_t)len);
                            buf[len] = '\0';
                            SDL_SetClipboardText(buf);
                        } else {
                            SDL_SetClipboardText(chatInput);
                        }
                    } else if (ctrl && e.key.keysym.sym == SDLK_x) {
                        if (chat_has_selection()) {
                            int a = 0, b = 0;
                            chat_normalize_selection(&a, &b);
                            char buf[CHAT_MSG_LEN];
                            int len = b - a;
                            if (len > CHAT_MSG_LEN - 1) len = CHAT_MSG_LEN - 1;
                            memcpy(buf, chatInput + a, (size_t)len);
                            buf[len] = '\0';
                            SDL_SetClipboardText(buf);
                            chat_delete_selection();
                        } else {
                            SDL_SetClipboardText(chatInput);
                            chatInput[0] = '\0';
                            chatCaret = 0;
                        }
                    } else if (ctrl && e.key.keysym.sym == SDLK_v) {
                        if (SDL_HasClipboardText()) {
                            char *clip = SDL_GetClipboardText();
                            chat_insert_text(clip);
                            SDL_free(clip);
                        }
                    } else if (ctrl && e.key.keysym.sym == SDLK_a) {
                        chatSelStart = 0;
                        chatSelEnd = (int)strlen(chatInput);
                        chatCaret = chatSelEnd;
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        if (chat_has_selection()) {
                            chat_delete_selection();
                        } else if (chatCaret > 0) {
                            memmove(chatInput + chatCaret - 1, chatInput + chatCaret, strlen(chatInput) - (size_t)chatCaret + 1);
                            chatCaret--;
                        }
                    } else if (e.key.keysym.sym == SDLK_DELETE) {
                        if (chat_has_selection()) {
                            chat_delete_selection();
                        } else {
                            int len = (int)strlen(chatInput);
                            if (chatCaret < len) {
                                memmove(chatInput + chatCaret, chatInput + chatCaret + 1, strlen(chatInput) - (size_t)chatCaret);
                            }
                        }
                    } else if (e.key.keysym.sym == SDLK_LEFT) {
                        chat_set_caret(chatCaret - 1, shift);
                    } else if (e.key.keysym.sym == SDLK_RIGHT) {
                        chat_set_caret(chatCaret + 1, shift);
                    } else if (e.key.keysym.sym == SDLK_HOME || e.key.keysym.sym == SDLK_END || e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN) {
                        int inputPad = 12;
                        int textMaxW = chatInputBox.w - (inputPad * 2);
                        if (textMaxW < 0) textMaxW = 0;
                        int baseW = 0, baseH = 0;
                        text_size_scaled("Ag", 1.0f, &baseW, &baseH);
                        float textScale = 0.85f;
                        if (textScale > 1.0f) textScale = 1.0f;
                        if (textScale < 0.6f) textScale = 0.6f;
                        int lineHeight = (int)((float)baseH * textScale);
                        if (lineHeight < 10) lineHeight = 10;
                        ChatLine linesBuf[CHAT_MSG_LEN];
                        int lineCount = layout_chat_input_lines(chatInput, textMaxW, textScale, linesBuf, CHAT_MSG_LEN);
                        int curLine = 0;
                        for (int i = 0; i < lineCount; i++) {
                            int start = linesBuf[i].start;
                            int end = start + linesBuf[i].len;
                            if (chatCaret >= start && chatCaret <= end) {
                                curLine = i;
                                break;
                            }
                        }
                        if (e.key.keysym.sym == SDLK_HOME) {
                            chat_set_caret(linesBuf[curLine].start, shift);
                        } else if (e.key.keysym.sym == SDLK_END) {
                            chat_set_caret(linesBuf[curLine].start + linesBuf[curLine].len, shift);
                        } else if (e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN) {
                            int dir = (e.key.keysym.sym == SDLK_UP) ? -1 : 1;
                            int targetLine = curLine + dir;
                            if (targetLine < 0) targetLine = 0;
                            if (targetLine >= lineCount) targetLine = lineCount - 1;
                            if (chatCaretPreferredX < 0) {
                                int lineStart = linesBuf[curLine].start;
                                chatCaretPreferredX = chat_input_substring_width(lineStart, chatCaret, textScale);
                            }
                            int targetStart = linesBuf[targetLine].start;
                            int targetLen = linesBuf[targetLine].len;
                            int targetIdx = chat_find_index_at_x(targetStart, targetLen, textScale, chatCaretPreferredX);
                            chat_set_caret(targetIdx, shift);
                        }
                    } else if (e.key.keysym.sym == SDLK_RETURN) {
                        if (e.key.keysym.mod & KMOD_SHIFT) {
                            chat_insert_text("\n");
                        } else {
                            send_chat_from_input();
                        }
                    } else if (e.key.keysym.sym == SDLK_PAGEUP) {
                        chatScrollOffset += 3;
                    } else if (e.key.keysym.sym == SDLK_PAGEDOWN) {
                        chatScrollOffset -= 3;
                    }
                }
                if (!gameOver) {
                    if (e.type == SDL_KEYDOWN) {
                        if (!chatInputFocused) {
                            if (e.key.keysym.sym == SDLK_s) selectedLetter = 'S';
                            if (e.key.keysym.sym == SDLK_o) selectedLetter = 'O';
                        }
                    }
                    if (e.type == SDL_MOUSEBUTTONDOWN) {
                        int col = e.button.x / cellSize;
                        int row = (e.button.y - 100) / cellSize;
                        if (col >= 0 && col < gridSize && row >= 0 && row < gridSize) {
                            if (!isNetworked) {
                                if (board[row][col].letter == ' ') {
                                    board[row][col].letter = selectedLetter;
                                    board[row][col].playerID = currentPlayer;
                                    filledCells++;
                                    
                                    int points = count_sos(row, col);
                                    if (points > 0) {
                                        scores[currentPlayer] += points;
                                        // Player keeps turn
                                    } else {
                                        currentPlayer = (currentPlayer + 1) % numPlayers;
                                    }
                                    turnTimer = TURN_DURATION_SECONDS; // Reset timer regardless
                                    if (filledCells == gridSize * gridSize) {
                                        gameOver = 1;
                                        if (gameOverAt == 0) gameOverAt = SDL_GetTicks();
                                    }
                                }
                            } else if (currentPlayer == localPlayerId) {
                                Move m = {row, col, selectedLetter, localPlayerId};
                                if (isServer) {
                                    int shouldBroadcast = 0;
                                    pthread_mutex_lock(&gameMutex);
                                    if (apply_move(m)) {
                                        shouldBroadcast = 1;
                                    }
                                    pthread_mutex_unlock(&gameMutex);
                                    if (shouldBroadcast) {
                                        broadcast_move(m);
                                    }
                                } else {
                                    Packet p = {0};
                                    p.type = PKT_MOVE;
                                    p.move = m;
                                    send_all(sockfd, &p, sizeof(p));
                                }
                            }
                        }
                    }
                    // Bot logic placeholder
                    if (isBotActive && currentPlayer == 1 && !gameOver) {
                        // Bot logic runs here in the future
                    }
                }
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_l && gameOver && isNetworked && isServer) {
                    return_to_lobby(1);
                }
                if (!isNetworked && e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_l && gameOver) {
                    return_to_lobby(0);
                }
                if (e.type == SDL_MOUSEBUTTONDOWN && gameOver) {
                    int mx = e.button.x;
                    int my = e.button.y;
                    if (point_in_rect(mx, my, gameReturnBtn)) {
                        if (!isNetworked) {
                            return_to_lobby(0);
                        } else if (isServer) {
                            return_to_lobby(1);
                        }
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
        SDL_RenderClear(ren);

        if (transitionActive && transitionTo == LOBBY && transitionFrom == NAME_INPUT) {
            Uint32 now = SDL_GetTicks();
            float t = (float)(now - transitionStart) / (float)transitionDuration;
            if (t >= 1.0f) {
                transitionActive = 0;
                pthread_mutex_lock(&gameMutex);
                draw_lobby(ren);
                pthread_mutex_unlock(&gameMutex);
            } else {
                if (t < 0.5f) {
                    draw_name_input(ren);
                    Uint8 alpha = (Uint8)(t * 2.0f * 255);
                    draw_fade_overlay(ren, alpha);
                } else {
                    pthread_mutex_lock(&gameMutex);
                    draw_lobby(ren);
                    pthread_mutex_unlock(&gameMutex);
                    Uint8 alpha = (Uint8)((1.0f - (t - 0.5f) * 2.0f) * 255);
                    draw_fade_overlay(ren, alpha);
                }
            }
        } else if (currentState == MENU) {
            draw_menu(ren);
        } else if (currentState == NAME_INPUT) {
            draw_name_input(ren);
        } else if (currentState == LOBBY) {
            pthread_mutex_lock(&gameMutex);
            draw_lobby(ren);
            pthread_mutex_unlock(&gameMutex);
        } else if (currentState == GAME) {
            pthread_mutex_lock(&gameMutex);
            draw_live_scoreboard(ren);
            draw_grid(ren);
            
            for(int r=0; r<gridSize; r++) for(int c=0; c<gridSize; c++) {
                if(board[r][c].letter == 'S') draw_s(ren, c*cellSize, 100+r*cellSize, colors[board[r][c].playerID]);
                if(board[r][c].letter == 'O') draw_o(ren, c*cellSize, 100+r*cellSize, colors[board[r][c].playerID]);
            }
            
            draw_strikes(ren);

            if (gameOver) {
                int winner = 0;
                for(int i=1; i<numPlayers; i++) if(scores[i] > scores[winner]) winner = i;

                // 1. Run the Fireworks
                draw_winner_finale(ren, winner);

                // 2. Attractive Winner Text
                char winMsg[100];
                sprintf(winMsg, "--- CHAMPION: %s ---", playerNames[winner]);
                
                // Animate the scale slightly for a "heartbeat" effect
                float beat = 1.0f + sin(SDL_GetTicks() / 200.0f) * 0.1f;
                
                SDL_Color gold = {255, 215, 0, 255};
                draw_text_scaled(ren, winMsg, screenWidth/2, screenHeight/2 - 50, gold, beat);
                if (isNetworked) {
                    int remaining = 0;
                    if (gameOverAt > 0) {
                        int elapsed = (int)((SDL_GetTicks() - gameOverAt) / 1000);
                        remaining = 5 - elapsed;
                        if (remaining < 0) remaining = 0;
                    }
                    char countdown[64];
                    sprintf(countdown, "Returning to lobby in %ds", remaining);
                    draw_text(ren, countdown, screenWidth/2 - 130, screenHeight/2 + 50, (SDL_Color){200,200,200,255});
                    if (isServer) {
                        draw_text(ren, "Host: Press 'L' to Return to Lobby", screenWidth/2 - 170, screenHeight/2 + 80, (SDL_Color){200,200,200,255});
                    }
                } else {
                    draw_text(ren, "Press 'L' to Return to Lobby", screenWidth/2 - 150, screenHeight/2 + 50, (SDL_Color){200,200,200,255});
                }

                // Return to lobby button
                gameReturnBtn = (SDL_Rect){ screenWidth/2 - 90, screenHeight/2 + 110, 180, 34 };
                SDL_SetRenderDrawColor(ren, 60, 120, 80, 220);
                SDL_RenderFillRect(ren, &gameReturnBtn);
                SDL_SetRenderDrawColor(ren, 220, 220, 220, 255);
                SDL_RenderDrawRect(ren, &gameReturnBtn);
                draw_text(ren, "RETURN TO LOBBY", gameReturnBtn.x + 10, gameReturnBtn.y + 8, (SDL_Color){255,255,255,255});
            } else {
                char status[100]; sprintf(status, "%s's Turn (%c)", playerNames[currentPlayer], selectedLetter);
                draw_text(ren, status, 10, 60, colors[currentPlayer]);
            }
            draw_chat_sidebar(ren, screenWidth - chat_sidebar_width(), 0, chat_sidebar_width(), screenHeight);
            pthread_mutex_unlock(&gameMutex);
        }
        if (forceQuit) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 10, 10, 14, 200);
            SDL_Rect overlay = { 0, 0, screenWidth, screenHeight };
            SDL_RenderFillRect(ren, &overlay);
            draw_text_scaled(ren, forceQuitMsg, screenWidth / 2, screenHeight / 2, (SDL_Color){240,240,240,255}, 1.0f);
        }
        SDL_RenderPresent(ren);
    }

    if (board != NULL) {
        for (int i = 0; i < gridSize; i++) free(board[i]);
        free(board);
    }
    if (lines != NULL) free(lines);

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
