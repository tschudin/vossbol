# Description of the protocol for Battleship
et.mettaz@unibas.ch
-- 06.08.2023

## Protocol summary

This describes the TinyTremola implementation of [Battleship](https://en.wikipedia.org/wiki/Battleship).

To start a game, Alice sends Bob an "invite" (the game is restricted to two player, but a multiplayer could be implemented). Bob can either "accept", "refuse" or discard the invite. Accept contains the first move. Upon receipt of a "move" message, a player answers it with another "move" except in two cases. If the last received move drowns the last ship, the player sends a "win" message that contains a declaration on where the ships were placed. Otherwise each player can send a "surrender" message at any time, with the same declaration.
It is also possible to answer to a move with "refuse" in case a player think a move is illegal.

## Message layouts

All messages are encoded as bipf dictionaries with a one letter tag and a bipf list as value.

| Action    | Code | Parameters                              | Example                                                           |
|-----------|------|-----------------------------------------|-------------------------------------------------------------------|
| Invite    | "I"  | commitment                              | {"I": [(array of bytes)] }                                        |
| Accept    | "A"  | game_id, prev, commitment, move, answer | {"A": [game_id, prev, (array of bytes), 54, "O"] }                |
| Decline   | "D"  | game_id, prev                           | {"D": [game_id, prev] }                                           |
| Move      | "M"  | game_id, prev, move, answer             | {"M": [game_id, prev, 26, "X"] }                                  |
| Surrender | "S"  | game_id, prev, ships, nonce, message    | {"S": [game_id, prev, "PH12 SH81 DV7 BV51 CH40", "Well played"] } |
| Win       | "W"  | game_id, prev, ships, nonce, message    | {"W": [game_id, prev, "PH12 SH81 DV7 BV51 CH40", "Good game!"] }  |
| Loose     | "L"  | game_id, prev, ships, nonce, message    | {"L": [game_id, prev, "PH12 SH81 DV7 BV51 CH40", "Congrats!"] }   |

### Description of the parameters

| Parameter  | Type             | Description                                                        |
|------------|------------------|--------------------------------------------------------------------|
| game_id    | ByteArray (20B)  | (tinySSB) message id of the "Invite"                               |
| prev       | ByteArray (20B)  | (tinySSB) message id of the previous message (for this game)       |
| commitment | string (64 char) | sha256 checksum of the "ships" concatenated to the nonce           |
| move       | int              | the targeted tile (x = move % width, y = move / height)            |
| answer     | char             | the result of the last attempt: "X" for TOUCHED and "O" for MISSED |
| ships      | string           | position of the ships (see bellow), coma separated                 |
| nonce      | ByteArray (??B)  | nonce for randomness in the commitment                             |
| message    | string           | a user given chat message                                          |


#### Ships position description

To encode the position of the ship, we need 3 parameters:

- the boat, a char describing which boat is used: "P" (patrol), "S" (submarine), "D" (destroyer), "B" (battleship) or "C"(carrier)
- the direction of the boat: "H" (horizontal) or "V" (vertical)
- the first tile (from top left) of the boat, an int

Example: "PH12 SH81 DV7 BV51 CH40"
The grid is 9×11 (width × height)

| String | Boat       | Direction  | First tile | Tiles covered  |
|--------|------------|------------|------------|----------------|
| PH12   | Patrol     | Horizontal | 12         | 12-13          |
| SH81   | Submarine  | Horizontal | 81         | 81-82-83       |
| DV7    | Destroyer  | Vertical   | 7          | 7-16-25        |
| BV51   | Battleship | Vertical   | 51         | 51-60-69-78    |
| CH40   | Carrier    | Horizontal | 40         | 40-41-42-43-44 |

Boat strings are space separated

## Commitment

To avoid cheating, each player commits to where he/she placed his/her boats, but does not do it in plain text. The principle is simple. After the boats have been placed, a string encoding the position of the boats is created. The user creates a bipf list containing this string and a nonce (as byte array), then computes the sha256 hash of this list and sends the result as commitment. At the end of the game, players send the boat layout with the nonce to let the opponent check that the commitment was respected.
