/*
 * Battleship4.c
 * Author: Muhammad Shahzad
 * Course: ISCI333
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define GRID_ROWS 10 //rows
#define GRID_COLS 10 //columns

typedef enum {
 SHIP_NONE = 0, // empty
 SHIP_CARRIER,  // 5
 SHIP_BATTLESHIP, //4
 SHIP_CRUISER,  // 3
 SHIP_SUBMARINE, // 2
 SHIP_DESTROYER // 1
} ShipType;

typedef enum {
 SHOT_UNKNOWN = 0, // not fired
 SHOT_HIT,
 SHOT_MISS
} ShotState;

typedef struct { char column; int row; } PlayerMove; // move

typedef enum { OUTCOME_HIT, OUTCOME_MISS } Outcome; // hit or miss

static ShipType  **gShips = NULL; // player's ship grid
static ShotState **gShots = NULL; // player's shots at opponent

static ShipType  **cShips = NULL;   // computer's ships
static ShotState **cShots = NULL;   // computer's shots at player's grid or remote shots

static bool gGameOver = false; // end flag
static const char *gWinMsg = NULL; // winner text

static int  gSocketFd = -1;     // connected socket
static bool gIsTwoPlayer = false; // running two-player mode

static Outcome gLastOpponentOutcome = OUTCOME_MISS;

void initialize(void);
void teardown(void);
bool accept_input(PlayerMove *outMove, bool *shouldQuit); // input
Outcome updatestate(const PlayerMove *move);
void display_state(const PlayerMove *move, Outcome r);

void SetupSinglePlayer(void); // alloc + random place computer ships
void TeardownSinglePlayer(void);
Outcome MakeSinglePlayerShot(const PlayerMove *move); // player's shot vs computer
void GetSinglePlayerShot(PlayerMove *cpuMove); // computer picks random untried square
void SinglePlayerResponce(const PlayerMove *cpuMove, Outcome cpuOutcome);
void SinglePlayerResponse(const PlayerMove *cpuMove, Outcome cpuOutcome);
bool SinglePlayerDidWin(bool checkHuman); // check if human or computer won
void DisplayWorld(void);

void SetupTwoPlayer(void); 
void TeardownTwoPlayer(void); //teardown two-player grids
Outcome MakeTwoPlayerShot(const PlayerMove *move); //send shot over network, get result
void GetTwoPlayerShot(PlayerMove *otherMove);
void TwoPlayerResponce(const PlayerMove *otherMove, Outcome outcome);
void TwoPlayerResponse(const PlayerMove *otherMove, Outcome outcome);
bool TwoPlayerDidWin(bool checkHuman);

static void run_single_player(void);
static void run_two_player(int sockFd, bool isServer);

static int  setup_server_socket(const char *portStr);
static int  setup_client_socket(const char *ipStr, const char *portStr);
static void send_all(int sock, const char *buf);
static int  recv_line(int sock, char *buf, size_t size);

static void *xmalloc(size_t n) { //safe alloc
 void *p = malloc(n);
 if (!p) { fprintf(stderr, "Out of memory\n"); exit(1); }
 return p; // ensure memory allocated
}

static void clear_ships(ShipType **grid) { //clear ships grid
 for (int r = 0; r < GRID_ROWS; ++r)
  for (int c = 0; c < GRID_COLS; ++c)
   grid[r][c] = SHIP_NONE; //reset for placement
}

static void clear_shots(ShotState **grid) { //clear shots grid
 for (int r = 0; r < GRID_ROWS; ++r)
  for (int c = 0; c < GRID_COLS; ++c)
   grid[r][c] = SHOT_UNKNOWN;
}

static char ship_abbr(ShipType t) { //letter for ship
 switch (t) {
  case SHIP_CARRIER: return 'C';
  case SHIP_BATTLESHIP: return 'B';
  case SHIP_CRUISER: return 'R';
  case SHIP_SUBMARINE: return 'S';
  case SHIP_DESTROYER: return 'D';
  default: return ' ';
 }
}

static int ship_length(ShipType t) {
 switch (t) {
  case SHIP_CARRIER: return 5;
  case SHIP_BATTLESHIP: return 4;
  case SHIP_CRUISER: return 3;
  case SHIP_SUBMARINE: return 2;
  case SHIP_DESTROYER: return 1;
  default: return 0;
 }
}

static void trim_newline(char *s) {
 if (!s) return;
 s[strcspn(s, "\n")] = '\0';
}

static bool read_line(const char *prompt, char *buffer, size_t size) {
 printf("%s", prompt);
 if (!fgets(buffer, (int)size, stdin)) return false;
 trim_newline(buffer);
 return true;
}

static bool place_ship_from_input(ShipType **grid, ShipType t, const char *in) { // place ship on provided grid
 while (*in == ' ') ++in; // skip spaces
 int n = (int)strlen(in);
 if (n < 3 || n > 3) {
  printf("Location specification is not 3 chars\n");
  return false;
 }
 char a = (char)toupper((unsigned char)in[0]);
 char b = (char)toupper((unsigned char)in[1]);
 char d = in[2];
 if (a < 'A' || a > 'J') { printf("Location out of bounds\n"); return false; }
 if (!isdigit((unsigned char)d)) { printf("Column must be 0-9\n"); return false; }
 int col = d - '0';
 if (col < 0 || col >= GRID_COLS) { printf("Column out of range\n"); return false; }
 int need = ship_length(t);
 if (isalpha((unsigned char)b)) { // vertical
  char c = b;
  if (c < 'A' || c > 'J') { printf("Out of bounds\n"); return false; }
  int r0 = a - 'A';
  int r1 = c - 'A';
  int span = (r1 >= r0 ? (r1 - r0 + 1) : (r0 - r1 + 1));
  if (span != need) { printf("Ship is %d in size, should be %d.\n", span, need); return false; }
  int step = (r1 >= r0) ? 1 : -1;
  for (int r = r0; r != r1 + step; r += step)
   if (grid[r][col] != SHIP_NONE) { printf("Ship found at %c, %d.\n", 'A'+r, col); return false; }
  for (int r = r0; r != r1 + step; r += step) grid[r][col] = t; // place
  return true;
 } else if (isdigit((unsigned char)b)) { // horizontal
  int c0 = b - '0';
  int c1 = d - '0';
  int span = (c1 >= c0 ? (c1 - c0 + 1) : (c0 - c1 + 1));
  if (span != need) { printf("Ship is %d in size, should be %d.\n", span, need); return false; }
  int row = a - 'A';
  if (row < 0 || row >= GRID_ROWS) { printf("Out of bounds\n"); return false; }
  int step = (c1 >= c0) ? 1 : -1;
  for (int c = c0; c != c1 + step; c += step) {
   if (c < 0 || c >= GRID_COLS) { printf("Out of range\n"); return false; }
   if (grid[row][c] != SHIP_NONE) { printf("Ship found at %c, %d.\n", 'A'+row, c); return false; }
  }
  for (int c = c0; c != c1 + step; c += step) grid[row][c] = t;
  return true;
 }
 printf("Invalid format. Use C37 or CF4.\n");
 return false;
}

static void print_player_board(void) {
 printf("\n      ");
 for (int c = 0; c < GRID_COLS; ++c) printf("  %d  ", c);
 printf("\n");
 for (int r = 0; r < GRID_ROWS; ++r) {
  printf("%c  ", 'A' + r);
  for (int c = 0; c < GRID_COLS; ++c) {
   char ch = ship_abbr(gShips[r][c]);
   printf(" %c  |", ch ? ch : ' ');
  }
  printf("\n");
 }
}

static void place_all_ships_player(void) {
 struct { ShipType t; const char *name; } order[] = {
  { SHIP_CARRIER, "carrier (5)" },
  { SHIP_BATTLESHIP, "battleship (4)" },
  { SHIP_CRUISER, "cruiser (3)" },
  { SHIP_SUBMARINE, "submarine (2)" },
  { SHIP_DESTROYER, "destroyer (1)" }
 };
 printf("Place your ships. The format is: AE4 (A4-E4) or J37 (J3-J7).\n");
 print_player_board();
 char buf[128];
 for (size_t i = 0; i < sizeof(order)/sizeof(order[0]); ++i) {
  int need = ship_length(order[i].t);
  while (1) { //repeat until valid
   printf("Please enter a location for a ship of %d squares: ", need);
   if (!fgets(buf, sizeof(buf), stdin)) { printf("Input error\n"); continue; }
   size_t L = strlen(buf); if (L && buf[L-1]=='\n') buf[L-1]='\0';
   if (place_ship_from_input(gShips, order[i].t, buf)) { print_player_board(); break; }
   else { print_player_board(); }
  }
 }
 printf("Ship placement complete.\n");
}

static bool can_place_here(ShipType **grid, int r, int c, int dr, int dc, int len) { //check fit/overlap
 for (int i = 0; i < len; ++i) {
  int rr = r + dr * i;
  int cc = c + dc * i;
  if (rr < 0 || rr >= GRID_ROWS || cc < 0 || cc >= GRID_COLS) return false;
  if (grid[rr][cc] != SHIP_NONE) return false;
 }
 return true;
}

static void place_ship_random(ShipType **grid, ShipType t) { //place one ship at random
 int len = ship_length(t);
 while (1) {
  int vertical = rand() % 2; // 0 horiz, 1 vert
  int r = rand() % GRID_ROWS;
  int c = rand() % GRID_COLS;
  int dr = vertical ? 1 : 0;
  int dc = vertical ? 0 : 1;
  if (rand() % 2) { dr = -dr; dc = -dc; }
  if (can_place_here(grid, r, c, dr, dc, len)) {
   for (int i = 0; i < len; ++i) grid[r + dr * i][c + dc * i] = t;
   return;
  }
 }
}

void SetupSinglePlayer(void) {
 cShips = (ShipType**)xmalloc(sizeof(ShipType*) * GRID_ROWS);
 cShots = (ShotState**)xmalloc(sizeof(ShotState*) * GRID_ROWS);
 for (int r = 0; r < GRID_ROWS; ++r) {
  cShips[r] = (ShipType*)xmalloc(sizeof(ShipType) * GRID_COLS);
  cShots[r] = (ShotState*)xmalloc(sizeof(ShotState) * GRID_COLS);
 }
 clear_ships(cShips);
 clear_shots(cShots);
 // place all ships randomly on computer grid
 place_ship_random(cShips, SHIP_CARRIER);
 place_ship_random(cShips, SHIP_BATTLESHIP);
 place_ship_random(cShips, SHIP_CRUISER);
 place_ship_random(cShips, SHIP_SUBMARINE);
 place_ship_random(cShips, SHIP_DESTROYER);
}

void TeardownSinglePlayer(void) {
 if (cShips) { for (int r = 0; r < GRID_ROWS; ++r) free(cShips[r]); free(cShips); cShips = NULL; }
 if (cShots) { for (int r = 0; r < GRID_ROWS; ++r) free(cShots[r]); free(cShots); cShots = NULL; }
}

Outcome MakeSinglePlayerShot(const PlayerMove *move) {
 if (!move) return OUTCOME_MISS;
 int r = move->column - 'A';
 int c = move->row;
 if (r < 0 || r >= GRID_ROWS || c < 0 || c >= GRID_COLS) return OUTCOME_MISS;
 // prevent re-shoot ambiguity: if already shot, return MISS again (no state change)
 if (gShots[r][c] != SHOT_UNKNOWN) return OUTCOME_MISS;
 if (cShips[r][c] != SHIP_NONE) {
  gShots[r][c] = SHOT_HIT; //record on player's shot grid
  cShips[r][c] = SHIP_NONE; //remove piece from computer grid
  return OUTCOME_HIT;
 } else {
  gShots[r][c] = SHOT_MISS;
  return OUTCOME_MISS;
 }
}

void GetSinglePlayerShot(PlayerMove *cpuMove) {
 if (!cpuMove) return;
 while (1) {
  int r = rand() % GRID_ROWS;
  int c = rand() % GRID_COLS;
  if (cShots[r][c] == SHOT_UNKNOWN) { cpuMove->column = (char)('A' + r); cpuMove->row = c; return; }
 }
}

void SinglePlayerResponce(const PlayerMove *cpuMove, Outcome cpuOutcome) {
 SinglePlayerResponse(cpuMove, cpuOutcome);
}

void SinglePlayerResponse(const PlayerMove *cpuMove, Outcome cpuOutcome) {
 if (!cpuMove) return;
 int r = cpuMove->column - 'A';
 int c = cpuMove->row;
 if (r < 0 || r >= GRID_ROWS || c < 0 || c >= GRID_COLS) return;
 cShots[r][c] = (cpuOutcome == OUTCOME_HIT) ? SHOT_HIT : SHOT_MISS; //mark on computer's shot grid
}

bool SinglePlayerDidWin(bool checkHuman) {
 ShipType **grid = checkHuman ? cShips : gShips; //winner if opponent has no ships
 for (int r = 0; r < GRID_ROWS; ++r)
  for (int c = 0; c < GRID_COLS; ++c)
   if (grid[r][c] != SHIP_NONE) return false;
 return true;
}

void DisplayWorld(void) {
 printf("\nYour board (ships shown; X = hit on you, o = miss on you)\n");
 printf("      ");
 for (int c = 0; c < GRID_COLS; ++c) printf("  %d  ", c);
 printf("\n");
 for (int r = 0; r < GRID_ROWS; ++r) {
  printf("%c  ", 'A' + r);
  for (int c = 0; c < GRID_COLS; ++c) {
   char base = ship_abbr(gShips[r][c]);
   char mark = ' ';
   if (cShots && cShots[r][c] == SHOT_HIT) mark = 'X';
   else if (cShots && cShots[r][c] == SHOT_MISS) mark = 'o';
   char toprint = (mark != ' ') ? mark : (base ? base : ' ');
   printf(" %c  |", toprint);
  }
  printf("\n");
 }
 printf("\nYour shots at the enemy (X = hit on enemy, o = miss)\n");
 printf("      ");
 for (int c = 0; c < GRID_COLS; ++c) printf("  %d  ", c);
 printf("\n");
 for (int r = 0; r < GRID_ROWS; ++r) {
  printf("%c  ", 'A' + r);
  for (int c = 0; c < GRID_COLS; ++c) {
   char ch = ' ';
   if (gShots && gShots[r][c] == SHOT_HIT) ch = 'X';
   else if (gShots && gShots[r][c] == SHOT_MISS) ch = 'o';
   printf(" %c  |", ch);
  }
  printf("\n");
 }
}

void initialize(void) { // setup game (single player)
 srand((unsigned)time(0)); // seed RNG
 gShips = (ShipType**)xmalloc(sizeof(ShipType*) * GRID_ROWS);
 gShots = (ShotState**)xmalloc(sizeof(ShotState*) * GRID_ROWS);
 for (int r = 0; r < GRID_ROWS; ++r) {
  gShips[r] = (ShipType*)xmalloc(sizeof(ShipType) * GRID_COLS);
  gShots[r] = (ShotState*)xmalloc(sizeof(ShotState) * GRID_COLS);
 }
 clear_ships(gShips); //reset before placing
 clear_shots(gShots); //clear player's shots at computer
 place_all_ships_player(); //user setup
 SetupSinglePlayer();
 gGameOver = false; gWinMsg = NULL;
}

void teardown(void) {
 printf("Board is complete.\n"); //board complete
 if (gShips) { for (int r = 0; r < GRID_ROWS; ++r) free(gShips[r]); free(gShips); gShips = NULL; }
 if (gShots) { for (int r = 0; r < GRID_ROWS; ++r) free(gShots[r]); free(gShots); gShots = NULL; }
 TeardownSinglePlayer();
}

bool accept_input(PlayerMove *outMove, bool *shouldQuit) { //input
 if(!outMove || !shouldQuit) return false;
 *shouldQuit = false;
 char line[64];
 if (!read_line("Choose a column (A-J) or Q to quit: ", line, sizeof line)) return false;
 if (line[0] == '\0') { printf("No input. Please try again. \n"); return false; }
 char c = (char)toupper((unsigned char)line[0]);
 if (c == 'Q') { *shouldQuit = true; return false; }
 if (c < 'A' || c > 'J') { printf("Invalid. Please choose A-J. \n"); return false; }
 if (!read_line("Choose a row (0-9) or Q to quit: ", line, sizeof line)) return false;
 if (line[0] == '\0') { printf("No input. Please try again. \n"); return false; }
 if (toupper((unsigned char)line[0]) == 'Q') { *shouldQuit = true; return false; }
 if (strlen(line) != 1 || line[0] < '0' || line[0] > '9') { printf("Invalid. Please choose (0-9). \n"); return false; }
 outMove->column = c; //save input
 outMove->row = line[0] - '0'; //convert
 return true;
}

Outcome updatestate(const PlayerMove *move) {
 if (!move) return OUTCOME_MISS;
 Outcome playerOutcome = MakeSinglePlayerShot(move);
 if (SinglePlayerDidWin(true)) { gGameOver = true; gWinMsg = "You win!"; return playerOutcome; }
 PlayerMove cpuMove;
 GetSinglePlayerShot(&cpuMove);
 int rr = cpuMove.column - 'A';
 int cc = cpuMove.row;
 Outcome cpuOutcome = OUTCOME_MISS;
 if (rr >= 0 && rr < GRID_ROWS && cc >= 0 && cc < GRID_COLS) {
  if (gShips[rr][cc] != SHIP_NONE) { cpuOutcome = OUTCOME_HIT; gShips[rr][cc] = SHIP_NONE; }
 }
 SinglePlayerResponce(&cpuMove, cpuOutcome);
 if (SinglePlayerDidWin(false)) { gGameOver = true; gWinMsg = "Computer wins!"; }
 return playerOutcome;
}

void display_state(const PlayerMove *move, Outcome result) {
 if(!move) return;
 const char *msg = (result == OUTCOME_HIT) ? "HIT" : "MISS";
 printf("You chose %c%d  --> %s\n", move->column, move->row, msg);
 DisplayWorld();
}

static void alloc_player_grids_two_player(void) {
 gShips = (ShipType**)xmalloc(sizeof(ShipType*) * GRID_ROWS);
 gShots = (ShotState**)xmalloc(sizeof(ShotState*) * GRID_ROWS);
 cShots = (ShotState**)xmalloc(sizeof(ShotState*) * GRID_ROWS);
 for (int r = 0; r < GRID_ROWS; ++r) {
  gShips[r] = (ShipType*)xmalloc(sizeof(ShipType) * GRID_COLS);
  gShots[r] = (ShotState*)xmalloc(sizeof(ShotState) * GRID_COLS);
  cShots[r] = (ShotState*)xmalloc(sizeof(ShotState) * GRID_COLS);
 }
 clear_ships(gShips);
 clear_shots(gShots);
 clear_shots(cShots);
}

void SetupTwoPlayer(void) {
 srand((unsigned)time(0)); 
 alloc_player_grids_two_player();
 place_all_ships_player();
 gGameOver = false;
 gWinMsg = NULL;
 gIsTwoPlayer = true;
}

void TeardownTwoPlayer(void) {
 printf("Board is complete.\n");
 if (gShips) { for (int r = 0; r < GRID_ROWS; ++r) free(gShips[r]); free(gShips); gShips = NULL; }
 if (gShots) { for (int r = 0; r < GRID_ROWS; ++r) free(gShots[r]); free(gShots); gShots = NULL; }
 if (cShots) { for (int r = 0; r < GRID_ROWS; ++r) free(cShots[r]); free(cShots); cShots = NULL; }
 gIsTwoPlayer = false;
}

Outcome MakeTwoPlayerShot(const PlayerMove *move) {
 if (!move || gSocketFd < 0) return OUTCOME_MISS;
 int r = move->column - 'A';
 int c = move->row;
 if (r < 0 || r >= GRID_ROWS || c < 0 || c >= GRID_COLS) return OUTCOME_MISS;
 if (gShots[r][c] != SHOT_UNKNOWN) return OUTCOME_MISS;
 char buf[64];
 snprintf(buf, sizeof(buf), "M %c %d\n", move->column, move->row);
 send_all(gSocketFd, buf);
 char resp[64];
 int n = recv_line(gSocketFd, resp, sizeof(resp));
 if (n <= 0) {
  printf("Connection closed by remote.\n");
  gGameOver = true;
  gWinMsg = "Connection lost.";
  return OUTCOME_MISS;
 }
 char tag;
 char code;
 if (sscanf(resp, "%c %c", &tag, &code) != 2 || tag != 'R') {
  printf("Bad response from remote: %s\n", resp);
  return OUTCOME_MISS;
 }
 Outcome out = (code == 'H' || code == 'X') ? OUTCOME_HIT : OUTCOME_MISS;
 gShots[r][c] = (out == OUTCOME_HIT) ? SHOT_HIT : SHOT_MISS;

 if (code == 'X') { //remote reports all ships sunk
  gGameOver = true;
  gWinMsg = "You win!";
 }
 return out;
}

void GetTwoPlayerShot(PlayerMove *otherMove) {
 if (!otherMove || gSocketFd < 0) return;
 char line[64];
 int n = recv_line(gSocketFd, line, sizeof(line));
 if (n <= 0) {
  printf("Connection closed by remote.\n");
  gGameOver = true;
  gWinMsg = "Connection lost.";
  return;
 }
 char tag;
 char col;
 int row;
 if (sscanf(line, "%c %c %d", &tag, &col, &row) != 3 || tag != 'M') {
  printf("Bad move from remote: %s\n", line);
  return;
 }
 otherMove->column = col;
 otherMove->row = row;

 int r = otherMove->column - 'A';
 int c = otherMove->row;
 Outcome outcome = OUTCOME_MISS;
 if (r >= 0 && r < GRID_ROWS && c >= 0 && c < GRID_COLS) {
  if (gShips[r][c] != SHIP_NONE) {
   outcome = OUTCOME_HIT;
   gShips[r][c] = SHIP_NONE;
  }
 }
 TwoPlayerResponce(otherMove, outcome);
 gLastOpponentOutcome = outcome;
 bool lost = true;
 for (int rr = 0; rr < GRID_ROWS; ++rr) {
  for (int cc = 0; cc < GRID_COLS; ++cc) {
   if (gShips[rr][cc] != SHIP_NONE) { lost = false; break; }
  }
  if (!lost) break;
 }
 char resp[64];
 char code = 'M';
 if (outcome == OUTCOME_HIT) {
  if (lost) code = 'X';
  else code = 'H';
 }
 snprintf(resp, sizeof(resp), "R %c\n", code);
 send_all(gSocketFd, resp);
 if (lost) {
  gGameOver = true;
  gWinMsg = "Remote player wins!";
 }
}
void TwoPlayerResponce(const PlayerMove *otherMove, Outcome outcome) {
 TwoPlayerResponse(otherMove, outcome);
}
void TwoPlayerResponse(const PlayerMove *otherMove, Outcome outcome) {
 if (!otherMove) return;
 int r = otherMove->column - 'A';
 int c = otherMove->row;
 if (r < 0 || r >= GRID_ROWS || c < 0 || c >= GRID_COLS) return;
 if (!cShots) return;
 cShots[r][c] = (outcome == OUTCOME_HIT) ? SHOT_HIT : SHOT_MISS;
}
bool TwoPlayerDidWin(bool checkHuman) {
 if (!gGameOver || !gWinMsg) return false;
 if (checkHuman) return strcmp(gWinMsg, "You win!") == 0;
 else return strcmp(gWinMsg, "Remote player wins!") == 0;
}
static void send_all(int sock, const char *buf) {
 size_t len = strlen(buf);
 while (len > 0) {
  ssize_t n = send(sock, buf, len, 0);
  if (n <= 0) {
   perror("send");
   exit(1);
  }
  buf += n;
  len -= (size_t)n;
 }
}
static int recv_line(int sock, char *buf, size_t size) {
 size_t pos = 0;
 while (pos + 1 < size) {
  char ch;
  ssize_t n = recv(sock, &ch, 1, 0);
  if (n <= 0) break;
  if (ch == '\n') break;
  buf[pos++] = ch;
 }
 buf[pos] = '\0';
 return (int)pos;
}

static int setup_server_socket(const char *portStr) {
 int port = atoi(portStr);
 if (port <= 0 || port > 65535) {
  fprintf(stderr, "Invalid port: %s\n", portStr);
  exit(1);
 }
 int s = socket(AF_INET, SOCK_STREAM, 0);
 if (s < 0) { perror("socket"); exit(1); }

 int opt = 1;
 if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
  perror("setsockopt");
 }

 struct sockaddr_in addr;
 memset(&addr, 0, sizeof(addr));
 addr.sin_family = AF_INET;
 addr.sin_addr.s_addr = htonl(INADDR_ANY);
 addr.sin_port = htons((unsigned short)port);

 if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
  perror("bind");
  exit(1);
 }
 if (listen(s, 1) < 0) {
  perror("listen");
  exit(1);
 }

 printf("Waiting for connection on port %d...\n", port);
 struct sockaddr_in client;
 socklen_t clen = sizeof(client);
 int cs = accept(s, (struct sockaddr*)&client, &clen);
 if (cs < 0) {
  perror("accept");
  exit(1);
 }
 close(s);
 printf("Client connected.\n");
 return cs;
}

static int setup_client_socket(const char *ipStr, const char *portStr) {
 int port = atoi(portStr);
 if (port <= 0 || port > 65535) {
  fprintf(stderr, "Invalid port: %s\n", portStr);
  exit(1);
 }
 int s = socket(AF_INET, SOCK_STREAM, 0);
 if (s < 0) { perror("socket"); exit(1); }

 struct sockaddr_in addr;
 memset(&addr, 0, sizeof(addr));
 addr.sin_family = AF_INET;
 addr.sin_port = htons((unsigned short)port);
 if (inet_pton(AF_INET, ipStr, &addr.sin_addr) <= 0) {
  fprintf(stderr, "Invalid IP address: %s\n", ipStr);
  exit(1);
 }
 printf("Connecting to %s:%d...\n", ipStr, port);
 if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
  perror("connect");
  exit(1);
 }
 printf("Connected.\n");
 return s;
}

static void run_single_player(void) {
 initialize();
 bool shouldQuit = false;
 while (!shouldQuit && !gGameOver) {
  PlayerMove move;
  if (!accept_input(&move, &shouldQuit)) continue;
  Outcome r = updatestate(&move); //result
  display_state(&move, r); //show
 }
 if (gGameOver && gWinMsg) printf("%s\n", gWinMsg);
 teardown(); //end
}

static void run_two_player(int sockFd, bool isServer) {
 gSocketFd = sockFd;
 SetupTwoPlayer();
 bool shouldQuit = false;
 bool myTurn = isServer; //server = player 1, starts

 printf("Two-player mode started. You are %s.\n",
        isServer ? "Player 1 (server, goes first)" : "Player 2 (client, goes second)");

 while (!gGameOver && !shouldQuit) {
  if (myTurn) {
   PlayerMove move;
   if (!accept_input(&move, &shouldQuit)) continue;
   Outcome r = MakeTwoPlayerShot(&move);
   const char *msg = (r == OUTCOME_HIT) ? "HIT" : "MISS";
   printf("You chose %c%d  --> %s\n", move.column, move.row, msg);
   DisplayWorld();
   if (gGameOver && gWinMsg) break;
   myTurn = false;
  } else {
   PlayerMove opp;
   printf("Waiting for opponent move...\n");
   GetTwoPlayerShot(&opp);
   if (gGameOver && gWinMsg && strcmp(gWinMsg, "Connection lost.") == 0) break;
   const char *msg = (gLastOpponentOutcome == OUTCOME_HIT) ? "HIT" : "MISS";
   printf("Opponent chose %c%d  --> %s\n", opp.column, opp.row, msg);
   DisplayWorld();
   if (gGameOver && gWinMsg) break;
   myTurn = true;
  }
 }

 if (gGameOver && gWinMsg) printf("%s\n", gWinMsg);
 TeardownTwoPlayer();
 if (gSocketFd >= 0) { close(gSocketFd); gSocketFd = -1; }
}

int main(int argc, char **argv) {
 if (argc == 1) {
  run_single_player();
  return 0;
 } else if (argc == 2) {
  int sock = setup_server_socket(argv[1]);
  run_two_player(sock, true);
  return 0;
 } else if (argc == 3) {
  int sock = setup_client_socket(argv[1], argv[2]);  run_two_player(sock, false);
  return 0;
 } else {
  printf("Usage:\n");
  printf("  %s              (single player)\n", argv[0]);
  printf("  %s <port>       (two-player server)\n", argv[0]);
  printf("  %s <ip> <port>  (two-player client)\n", argv[0]);
  return 1;
 }
}
