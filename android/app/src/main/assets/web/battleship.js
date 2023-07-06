// battleship.js

"use strict";

var curr_game;

var width = 9;
var height = 11;
var small_grid_buttons = [];
var big_grid_buttons = [];
var my_grid = [];               // grid with my boats and the opponent's shot to it
var other_grid = [];            // grid with the opponent's ships (what I know of it)
var my_turn;                    // bool, is it my turn?
var answer_other_shot = null;   // answer for opponent's last shot
var last_shot_b;                // my last shot's button
var last_shot_i;                // my last shot's index
var ships;                      // string descriping the ships' positions
var my_commitment;              // commitment to the placing of the boat (see doc)
var xref;                       // cross reference to the opponent's last message
var accepted = false;           // True if next message is an "accept" instead of "move"

const tile_state = {
    WATER             : { number: "0", letter: ' ', size: 0, color: "blue"     },
    PATROL            : { number: "1", letter: 'P', size: 2, color: "darkblue" },
    SUBMARINE         : { number: "2", letter: 'S', size: 3, color: "darkblue" },
    DESTROYER         : { number: "3", letter: 'D', size: 3, color: "darkblue" },
    BATTLESHIP        : { number: "4", letter: 'B', size: 4, color: "darkblue" },
    CARRIER           : { number: "5", letter: 'C', size: 5, color: "darkblue" },
    MISSED            : { number: "6", letter: 'O', size: 0, color: "darkblue" },
    TOUCHED_PATROL    : { number: "7", letter: 'P', size: 2, color: "red" },
    TOUCHED_SUBMARINE : { number: "8", letter: 'S', size: 3, color: "red" },
    TOUCHED_DESTROYER : { number: "9", letter: 'D', size: 3, color: "red" },
    TOUCHED_BATTLESHIP: { number: "A", letter: 'B', size: 4, color: "red" },
    TOUCHED_CARRIER   : { number: "B", letter: 'C', size: 5, color: "red" },
    TOUCHED           : { number: "C", letter: 'X', size: 0, color: "red" }
};

//--- New game and game-init scenario

function new_game() {
    var recps = []
    for (var m in tremola.contacts) {
        if (document.getElementById(m).checked)
            recps.push(m);
    }
    console.log("game: New game with " + recps);
    if (recps.length > 1) {
        launch_snackbar("Too many recipients");
        console.log("game: Too many recipients");
        return;
    }
    // TODO mid of the first message (=game_id) must be the json key, not the user id, to allow for multiple
    //      games with the same person. It is already the case for the player receiving the invite
    var nm = recps2nm(recps);
    console.log("game recps2nm = " + recps2nm(recps))
    if (nm in tremola.games) {
        if (!tremola.games[nm]['terminated']) {
            launch_snackbar("Game already exists");
            console.log("game: Game already exists");
            return;
        }
    }
    if (!(nm in tremola.games) || tremola.games[nm]['terminated']) {
        my_grid = place_boats();
        other_grid = new Array(width * height + 1).join("0");
        tremola.games[nm] = {
            "game_id": "", "alias": "w/" + id2b32(nm), "posts": {},
            "members": recps, "touched": Date.now(), "timeline": new Timeline(),
            "terminated": false, "my_grid": my_grid, "other_grid": other_grid,
            "xref": null // TODO store my turn? Is posts necessary?
        };
        my_turn = false;
        curr_game = nm;
//        console.log("game: receive_shot TURN ON");
        persist();
        console.log("game : stringified new" + JSON.stringify(tremola.games[nm]));
//        npc_load();  // TODO only for testing
        backend("bts I " + nm + " [" + ships + "]");
    } else
        tremola.game[nm]["touched"] = Date.now();


    load_game_list();
    console.log("game: curr_game " + JSON.stringify(curr_game));
    closeOverlay();
    setScenario("game-init");
}

// Cheat to start a game with myself automatically
function add_game_with_self() {
    my_grid = place_boats();
    other_grid = new Array(width * height + 1).join("0");
    tremola.games[myId] = {
        "game_id": "", "alias": "w/" + id2b32(myId), "posts": {},
        "members": [myId], "touched": Date.now(), "timeline": new Timeline(),
        "terminated": false, "my_grid": my_grid, "other_grid": other_grid,
        "xref": null // TODO store my turn?
    };
    my_turn = true;
//    console.log("game: receive_shot TURN ON");
    persist();
    console.log("game : stringified new" + JSON.stringify(tremola.games[myId]));
    npc_load();  // TODO only for testing
    console.log("game: | bts I " + myId + " [" + ships + "]");
    backend("bts I " + myId + " [" + ships + "]");
    accepted = true;
    load_game_list();
    console.log("game: curr_game " + JSON.stringify(curr_game));
    console.log("game: npc load\n" + JSON.stringify(my_grid) + "\n" + JSON.stringify(grid_from_string(ships)));
}

function menu_edit_game_name() {
    console.log("game: Editing game name");
    menu_edit('gameNameTarget', "Edit game name:<br>(only you can see this name)", tremola.games[curr_game].alias);
}

// load the list of chats from the "game-init" scenario
function load_game_list() {
    document.getElementById('lst:game').innerHTML = '';
    var lop = [];
    for (var p in tremola.games) {
        if (!tremola.games[p]['terminated'])
            lop.push(p)
    }
    lop.sort(function (a, b) {
        return tremola.games[b]["touched"] - tremola.games[a]["touched"]
    })
    lop.forEach(function (p) {
        load_game_item(p)
    })
    // forgotten chats: unsorted
    if (!tremola.settings.hide_forgotten_conv)
        for (var p in tremola.games)
            if (tremola.games[p]['terminated'])
                load_game_item(p)
}

// load one item from the "chat" scenario
function load_game_item(nm) { // appends a button for conversation with name nm to the conv list
    var cl, mem, item, bg, row, badge, badgeId, cnt;
    cl = document.getElementById('lst:game');
    mem = recps2display(tremola.games[nm].members);
    item = document.createElement('div');
    // reuse chat item class
    item.setAttribute('class', 'chat_item_div'); // old JS (SDK 23)
    if (tremola.games[nm].forgotten) bg = ' gray'; else bg = ' light';
    row = "<button class='chat_item_button w100" + bg + "' onclick='load_game(\"" + nm + "\");' style='overflow: hidden; position: relative;'>";
    row += "<div style='white-space: nowrap;'><div style='text-overflow: ellipsis; overflow: hidden;'>" + tremola.games[nm].alias + "</div>";
    row += "<div style='text-overflow: clip; overflow: ellipsis;'><font size=-2>" + escapeHTML(mem) + "</font></div></div>";
    badgeId = nm + "-badge"
    badge = "<div id='" + badgeId + "' style='display: none; position: absolute; right: 0.5em; bottom: 0.9em; text-align: center; border-radius: 1em; height: 2em; width: 2em; background: var(--red); color: white; font-size: small; line-height:2em;'>&gt;9</div>";
    row += badge + "</button>";
    row += ""
    item.innerHTML = row;
    cl.appendChild(item);
}

function load_game(members) {
    console.log("game: Loading game with " + members);
    if (curr_game != members) {
        curr_game = members
        var gridContainer = document.getElementById('game:main-grid');
        var smallGridContainer = document.getElementById('game:small-grid');
        console.log("game: curr_game " + JSON.stringify(tremola.games));
        while (gridContainer.hasChildNodes()) {
            gridContainer.removeChild(gridContainer.firstChild);
        }
        while (smallGridContainer.hasChildNodes()) {
            smallGridContainer.removeChild(smallGridContainer.firstChild);
        }
        small_grid_buttons = [];
        big_grid_buttons = [];
        create_grid(gridContainer, smallGridContainer);
    }
    console.log("game: Starting game");
    setScenario('game');
}

function create_grid(gridContainer, smallGridContainer) {
    gridContainer.style.setProperty('--cols', width); // Set custom CSS variable for column count
    smallGridContainer.style.setProperty('--cols', width); // Set custom CSS variable for column count
    console.log("game : create grid " + JSON.stringify(tremola.games[curr_game]));

    // Generate the buttons dynamically
    for (var j = 0; j < height; j++) {
        for (var i = 0; i < width; i++) {
            var gridItem = document.createElement('div');
            gridItem.className = 'game-grid-item';

            var button = document.createElement('button');
            var state = get_tile_state_by_number(tremola.games[curr_game].other_grid.charAt(i + j * width));
//            console.log("game: state[" + (i+j*width) + "] = " + JSON.stringify(state));
            button.innerHTML = state["letter"];
            button.style.backgroundColor = state.color;
            button.addEventListener('click',
                function(index, button) {
                    return function() {
                        send_shot(index, button);
                    };
                }(i + j * width, button));

            big_grid_buttons.push(button);
            gridItem.appendChild(button);
            gridContainer.appendChild(gridItem);
        }
    }

    // Generate the buttons dynamically for the small grid
    for (var j = 0; j < height; j++) {
        for (var i = 0; i < width; i++) {
            var gridItem = document.createElement('div');
            gridItem.className = 'game-grid-item';

            var button = document.createElement('div');
            var state = get_tile_state_by_number(tremola.games[curr_game].my_grid.charAt(i + j * width));
//            console.log("game: state[" + (i+j*width) + "] = " + JSON.stringify(state));
            button.innerHTML = state.letter;
            button.style.backgroundColor = state.color;
            button.disabled = true;

            small_grid_buttons.push(button);
            gridItem.appendChild(button);
            smallGridContainer.appendChild(gridItem);
        }
    }
}

function place_boats() {
    var grid = new Array(width * height + 1).join("0");
    ships = width + "," + height;
    for (var i = 0; i < 2; i++) {
        grid = replace_at(grid, i, tile_state.PATROL.number);
    }
    ships += ",PH0";
    for (var i = 0; i < 3; i++) {
        grid = replace_at(grid, i+57, tile_state.SUBMARINE.number);
    }
    ships += ",SH57";
    for (var i = 0; i < 3 * width; i += width) {
        grid = replace_at(grid, i+4, tile_state.DESTROYER.number);
    }
    ships += ",DV4";
    for (var i = 0; i < 4 * width; i += width) {
        grid = replace_at(grid, i+17, tile_state.BATTLESHIP.number);
    }
    ships += ",BV17";
    for (var i = 0; i < 5; i++) {
        grid = replace_at(grid, i+85, tile_state.CARRIER.number);
    }
    ships += ",CH85";
//    console.log("game: " + grid);
    return grid;
}

//--- private game logic

function reveal_grid_lost() {  // TODO
    for (var i = 0; i < width * height; i++) {
        var state = get_tile_state_by_number(tremola.games[curr_game].my_grid[i])
        tremola.games[curr_game].other_grid =
            replace_at(tremola.games[curr_game].other_grid, i, state.number);
        console.log("game: c = " + small_grid_buttons[i].innerHTML + " and " + (small_grid_buttons[i].innerHTML != "O"));
        if (big_grid_buttons[i].innerHTML != "O") {
            big_grid_buttons[i].innerHTML = small_grid_buttons[i].innerHTML;
        }
    }
    launch_snackbar("You won!");
}

function reveal_grid() {
    for (var i = 0; i < width * height; i++) {
        var state = get_tile_state_by_number(npc_grid[i])
        tremola.games[curr_game].other_grid =
            replace_at(tremola.games[curr_game].other_grid, i, state.number);
        console.log("game: c = " + small_grid_buttons[i].innerHTML + " and " + (small_grid_buttons[i].innerHTML != "O"));
        var ts = get_tile_state_by_number(npc_grid[i]);
        if (ts.letter != "O") {
            big_grid_buttons[i].innerHTML = ts.letter;
        }
    }
    launch_snackbar("You won!");
}

function set_tile(index, state, mine) {
//    console.log("game: set tile :" + index + ": :" + state.letter + ":");
    if (curr_game == null)
        return;
    if (mine) {  // TODO delete?
        tremola.games[curr_game].my_grid = replace_at(tremola.games[curr_game].my_grid, index, state.number);
//        console.log("game : set my tile 1 " + index + " " + JSON.stringify(tremola.games[curr_game].my_grid));
        if (small_grid_buttons[0] != undefined) {
            small_grid_buttons[index].innerHTML = state.letter;
            small_grid_buttons[index].style.backgroundColor = state.color;
        }
        var c = 0;
        var s = tremola.games[curr_game].other_grid;
        for (var i = 0; i < s.length; i++) {
            if (s[i] == tile_state.TOUCHED.number) c++;
        }
        console.log("game: count = " + c);
        if (c >= 17) reveal_grid();
    } else {
//        console.log("game : set other tile " + state.number + ", " + index + " " + JSON.stringify(tremola.games[curr_game].other_grid));
        tremola.games[curr_game].other_grid = replace_at(tremola.games[curr_game].other_grid, index, state.number);
        if (big_grid_buttons[0] != undefined) {
            big_grid_buttons[index].innerHTML = state.letter;  // TODO
            big_grid_buttons[index].style.backgroundColor = state.color;
        }
        var c = 0;
        var s = tremola.games[curr_game].other_grid;
        for (var i = 0; i < s.length; i++) {
//            console.log("game: :" + s[i] + ": and :" + tile_state.TOUCHED.number + ":");
            if (s[i] === tile_state.TOUCHED.number) c++;
        }
        console.log("game: count = " + c);
        if (c >= 17) reveal_grid();
    }

    persist();

}

function handle_receive_shot(i) {
    console.log("game: grid[" + i + "] is " + tremola.games[curr_game].my_grid[i]);
    var ts = parseInt(tremola.games[curr_game].my_grid[i], 16);  // tile state
//    console.log("game: grid[" + i + "] has state " + ts);
    var new_ts = state_from_received_shot(ts);
    small_grid_buttons[i].innerHTML = new_ts.letter;
    small_grid_buttons[i].style.backgroundColor = new_ts.color;
    tremola.games[curr_game].my_grid = replace_at(tremola.games[curr_game].my_grid, i, new_ts.number);
    persist();
    return new_ts;
}

//--- public game logic

function bts_b2f(e) {
    console.log("game", "bts receive from backend: " + JSON.stringify(e))
    if (e.header.fid === myId) { // message comes from me
        if (e.I !== null) {
            console.log("game", "You sent an invite. Commitment = " + e.I);
            console.log("game", "current game: " + curr_game);
            console.log("game", JSON.stringify(tremola.games) + " and " + JSON.stringify(e.header));
            my_commitment = e.I;
            tremola.games[curr_game]["game_id"] = e.header.mid;
        }
        return;
    }
    if (e.I !== null) {
        console.log("game", "invite received by " + e.header.fid + " (I am " + myId) + ")";
        receive_invite(e);
    } else if (e.D !== null) {
        // TODO implement decline
    } else if (e.A !== null) {

    }
}

function receive_invite(e) {
    //  TODO Ask user for confirmation
    launch_snackbar("Received invite to battleship. Accepting...");
    console.log("game Received invite to battleship. Accepting...");
    // TODO let the user choose where to put the boats
    my_grid = place_boats();
    other_grid = new Array(width * height + 1).join("0");
    console.log("game recps2nm = " + recps2nm(recps) + " " + e.header.mid)
    tremola.games[e.header.mid] = {
        "game_id": e.header.mid, "alias": "w/" + id2b32(e.header.fid.replace(/@/g, '')), "posts": {},
        "members": e.header.fid, "touched": Date.now(), "timeline": new Timeline(),
        "terminated": false, "my_grid": my_grid, "other_grid": other_grid,
        "xref": e.header.mid// TODO store my turn?
    };
    curr_game = e.header.mid;
    xref = e.header.mid;
    accepted = true;
    my_turn = true;
    persist();
}

function terminate() {
    small_grid_buttons = [];
    big_grid_buttons = [];
    my_grid = [];
    other_grid = [];

    tremola.games[curr_game]['terminated'] = true;
    persist();
    load_game_list();
    closeOverlay();
    setScenario("game-init");
}

function send_shot(i, button) {
    console.log("game: send_shot");
    if (!my_turn) {
        console.log("game: send_shot DENIED");
        launch_snackbar("game: send_shot DENIED");
        return;
    }
    last_shot_b = button;
    last_shot_i = i;
    answer_other_shot = null;
    my_turn = false;
//    console.log("game: receive_shot TURN OFF");
//    npc_receive_shot(i, answer_other_shot);
    if (accepted) {
        console.log("game send bts A")
        accepted = false;
        backend("bts A " + curr_game + " " + xref + " " + tremola.games[curr_game].members + " " + grid_from_string(ships) + " " + i);
    } else {
        console.log("game send bts M")
        backend("bts M " + curr_game + " " + xref + " " + i + " " + answer_other_shot);
    }
//        backend("bts I " + myId + " [" + ships + "]");
}

function receive_shot(i, ts_response) {
    console.log("game: receive_shot");
    set_tile(last_shot_i, ts_response, false);
//    console.log("game: i = " + i + " and is " + typeof(i))
    var ts = handle_receive_shot(i);
    console.log("game: new state = " + ts.number);
    if (ts == tile_state.MISSED) {
        answer_other_shot = tile_state.MISSED;  // TODO message layout with answer
    } else {
        answer_other_shot = tile_state.TOUCHED;
    }
    my_turn = true;
//    console.log("game: receive_shot TURN ON");
}

function win(grid) {

}

//--- Helper functions

function get_tile_state_by_letter(n) {
    for (const key in tile_state) {
        if (tile_state.hasOwnProperty(key) && tile_state[key].letter == n) {
            return tile_state[key];
        }
    }
    console.log("game: Tile " + n + " not found");
    return null; // Member with the given number not found
}

function get_tile_state_by_number(n) {
    for (const key in tile_state) {
//        console.log("game: key = " + key + " and number = " + n + ", tile_state[key].number = " + tile_state[key].number);
//        console.log("game:  " + (tile_state[key].number == n) + ", " + (tile_state[key].number === n) + " and typeof = " + typeof(key) + ", " + typeof(n));
        if (tile_state.hasOwnProperty(key) && tile_state[key].number == n) {
            return tile_state[key];
        }
    }
  return null; // Member with the given number not found
}

function grid_from_string(boat_string) {
    var boats = boat_string.split(','); // convert string to array
    console.log("game: " + JSON.stringify(boats));
    if (parseInt(boats[0]) != width ||parseInt(boats[1]) != height) {
        console.log("game: received grid but dim do not match");
    }
    var grid = new Array(width * height + 1).join("0");
    boats = boats.slice(2);
    boats.forEach(function (b) {
        var ts = get_tile_state_by_letter(b[0]);
        var indent = (b[1] == 'H') ? 1 : width;
        console.log("game: received: " + b + " and extracted " + ts + ", " + indent + ", " + parseInt(b.slice(2)));
        var i = parseInt(b.slice(2));
        for (var j = 0; j < ts.size; j += 1) {
            grid = replace_at(grid, i, ts.number);
            i += indent;
        }
    });
    return grid;
}

function replace_at(str, index, replacement) {
    var chars = str.split(''); // convert string to array
    chars[index] = replacement;
    return chars.join('');
}

function state_from_received_shot(ts) {
    switch(ts) {
        case 0: return tile_state.MISSED;
        case 1: return tile_state.TOUCHED_PATROL;
        case 2: return tile_state.TOUCHED_SUBMARINE;
        case 3: return tile_state.TOUCHED_DESTROYER;
        case 4: return tile_state.TOUCHED_BATTLESHIP;
        case 5: return tile_state.TOUCHED_CARRIER;
        default: return get_tile_state_by_number(ts);
    }
}

/*
        1/ store elements in set_tile
  TODO: 2/ add boats
        3/ add logic for click regarding the boats
*/

/*
    my_grid:    The small one with my ships, showing everything
    other_grid: The big one with my opponent's ships
               (for now when I check I use my grid), showing only WATER or TOUCHED

    Each nodes knows only the minimum and reveals everything to the user

    Placing the boats: we want one message that describes the whole layout of the boat.
                       the message is sent only at the end of the game.
    [int_width, int_height, [BID]*]
    where B: Boat (one letter)
          I: index of the tile covered by the boat closest to top left tile (int)
          D: direction, H for Horizontal (to the right) and V for Vertical (to the bottom)


Process from user:
    1/ user click button, this calls function send_shot  // TODO delete handle_click and keep send_shot?
    2/ send_shot sends the shot and the answer from last shot to npc (calls npc_receive_shot) // 3 and 4 will be done in one
    3/ my_turn = false, answer_other_shot = null

Process from npc:
    1/ receive new shot on npc_receive_shot
    2/ store answer from npc's previous shot
    3/ prepare answer for opponent's last shot
    4/ prepare next shot
    5/ send answer and new shot to receive_shot

Process from user:
    1/ when receive shot, deal with answer from last shot and prepare answer from npc's shot and store it
*/

/**
 * Status
 * The GUI works (although very buggy) for a single player. add_game_with_self is there for debugging
 * that. But currently the place of the boats is hard-coded, and not set by the user.
 * I started implementing the communication protocol and wrote a document about it, but the
 * implementation is only done for the commands Invite, Accept and Move and likely very buggy and
 * non functional. Here are a few points for a future work:
 * - Implementation should be straight forward although not trivial
 * - The space at the bottom left of the screen is thought to be a text area describing the state of the game
 *   and helping the user to know what he has to do
 * - The size of the grid is only hard coded on top of this file, it should be easy to let the user modify it
 *     (it is included in the "invite" message)
 * - The number of boats could be modified as well
 * - A future development could be a multiplayer version where everybody puts its boat on the same grid
 *
