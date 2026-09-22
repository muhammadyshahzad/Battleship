# Battleship

This is a Battleship game written in C.

It supports:

* Single-player mode against the computer
* Two-player mode using TCP sockets
* Manual ship placement
* Hit and miss tracking
* Win detection

## Compile

gcc Battleship4.c -o battleship

## Run

Single-player:

./battleship

Two-player server:

./battleship 5000

Two-player client:

./battleship <server-ip> 5000

Example:

./battleship 192.168.1.10 5000

## Important Parts of the Code

The program uses several important C concepts:

* **Enums** are used to store ship types, shot states, and hit/miss results.
* **Structs** are used to store a player's move.
* **Dynamic memory** is used to create the game boards with `malloc()`.
* **Random numbers** are used to place the computer's ships and choose its shots.
* **Functions** are used to separate ship placement, shooting, displaying the board, and checking for a winner.
* **TCP sockets** are used for communication between two players.
* `socket()`, `bind()`, `listen()`, and `accept()` are used for the server.
* `connect()` is used by the client.
* `send()` and `recv()` are used to exchange moves between players.

## Ship Placement

Enter ship locations using three characters.

Examples:

AE4
J37

Ships cannot overlap or go outside the board.

## Playing

Choose a letter from `A-J` and a number from `0-9` to attack a location.

Example:

C5

The game will show whether the shot was a `HIT` or `MISS`.

Use `Q` to quit the game.

## Board Symbols

X = Hit
o = Miss
C = Carrier
B = Battleship
R = Cruiser
S = Submarine
D = Destroyer

## Main Functions

Some of the main functions are:

* `initialize()` - sets up the game
* `accept_input()` - gets the player's move
* `DisplayWorld()` - displays the boards
* `MakeSinglePlayerShot()` - handles a shot against the computer
* `GetSinglePlayerShot()` - creates the computer's shot
* `SetupTwoPlayer()` - sets up two-player mode
* `MakeTwoPlayerShot()` - sends a shot to the other player
* `GetTwoPlayerShot()` - receives the opponent's shot

## File

Battleship4.c
