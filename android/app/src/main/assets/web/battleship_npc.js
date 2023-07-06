// battleship_npc.js

// simulator (NPC - Non Player Character) for battleship game
"use strict";
var npc_grid;
var npc_other_grid;
var npc_next_move; // the next shot I will do
var npc_last_shot; // the last shot I sent
var npc_ships;

//--- public functions

function npc_load(x, y) {
    npc_other_grid = new Array(width * height + 1).join("0");
    npc_grid = npc_place_boats();
    var g = grid_from_string(npc_ships);
    console.log("game: npc load\n" + JSON.stringify(g) + "\n" + JSON.stringify(npc_grid));
    npc_next_move = 34;
}

/**
 * Receive the new shot from the opponent.
 *
 * i:  the index of the shot from the opponent
 * ts: the result (tile_state) of the last shot I fired (null for first shot)
 */
function npc_receive_shot(i, ts) {
    console.log("game: npc_receive_shot");
    if (ts != null) {
        npc_other_grid = replace_at(npc_other_grid, npc_last_shot, ts.number);
        var c = 0;
        var s = npc_other_grid;
        for (var i = 0; i < s.length; i++) {
            if (s[i] == tile_state.TOUCHED.number) c++;
        }
//        console.log("game: count = " + c);
        if (c >= 17) npc_wins();
    }
    var ts = parseInt(npc_grid[i], 16);  // tile state
    var answer_ts = state_from_received_shot(ts);
    npc_grid = replace_at(npc_grid, i, answer_ts.number);

    npc_next_move = (npc_next_move + 1) % (width * height);
    if (answer_ts != tile_state.MISSED) {
        answer_ts = tile_state.TOUCHED;
    }
    receive_shot(npc_next_move, answer_ts);
}

function npc_loose() {

}

function npc_wins() {
//    reveal_grid_lost(npc_grid);  // TODO
    launch_snackbar("You lost!")
}

//--- private functions

function npc_place_boats() {
    var grid = new Array(width * height + 1).join("0");
    npc_ships = width + " " + height;
    for (var i = 0; i < 2; i++) {
        grid = replace_at(grid, i+12, tile_state.PATROL.number);
    }
    npc_ships += " PH12";
    for (var i = 0; i < 3; i++) {
        grid = replace_at(grid, i+81, tile_state.SUBMARINE.number);
    }
    npc_ships += " SH81";
    for (var i = 0; i < 3 * width; i += width) {
        grid = replace_at(grid, i+7, tile_state.DESTROYER.number);
    }
    npc_ships += " DV7";
    for (var i = 0; i < 4 * width; i += width) {
        grid = replace_at(grid, i+51, tile_state.BATTLESHIP.number);
    }
    npc_ships += " BV51";
    for (var i = 0; i < 5; i++) {
        grid = replace_at(grid, i+40, tile_state.CARRIER.number);
    }
    npc_ships += " CH40";
//    console.log("game: " + grid);
    return grid;
}