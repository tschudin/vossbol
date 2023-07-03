// battleship.js

"use strict";

var curr_game;

var width = 9;
var height = 11;
var small_grid_buttons = [];
var big_grid_buttons = [];
var my_grid = [];
var other_grid = [];
var my_turn;
var answer_other_shot = null;
var last_shot_b;
var last_shot_i;
var ships;

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
    var nm = recps2nm(recps);
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
            "alias": "w/" + id2b32(nm), "posts": {},
            "members": recps, "touched": Date.now(), "timeline": new Timeline(),
            "terminated": false, "my_grid": my_grid, "other_grid": other_grid // TODO store my turn?
        };
        my_turn = true;
//        console.log("game: receive_shot TURN ON");
        persist();
        console.log("game : stringified new" + JSON.stringify(tremola.games[nm]));
//        npc_load();  // TODO only for testing
        backend("bts I " + nm);
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
        "alias": "w/" + id2b32(myId), "posts": {},
        "members": [myId], "touched": Date.now(), "timeline": new Timeline(),
        "terminated": false, "my_grid": my_grid, "other_grid": other_grid // TODO store my turn?
    };
    my_turn = true;
//    console.log("game: receive_shot TURN ON");
    persist();
    console.log("game : stringified new" + JSON.stringify(tremola.games[myId]));
    npc_load();  // TODO only for testing

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
    ships = width + " " + height;
    for (var i = 0; i < 2; i++) {
        grid = replace_at(grid, i, tile_state.PATROL.number);
    }
    ships += " 1H0";
    for (var i = 0; i < 3; i++) {
        grid = replace_at(grid, i+57, tile_state.SUBMARINE.number);
    }
    ships += " 2H57";
    for (var i = 0; i < 3 * width; i += width) {
        grid = replace_at(grid, i+4, tile_state.DESTROYER.number);
    }
    ships += " 3V4";
    for (var i = 0; i < 4 * width; i += width) {
        grid = replace_at(grid, i+17, tile_state.BATTLESHIP.number);
    }
    ships += " 4V17";
    for (var i = 0; i < 5; i++) {
        grid = replace_at(grid, i+85, tile_state.CARRIER.number);
    }
    ships += " 5H85";
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
    console.log("1 game: send_shot");
    if (!my_turn) {
        console.log("game: send_shot DENIED");
        return;
    }
    last_shot_b = button;
    last_shot_i = i;
    answer_other_shot = null;
    my_turn = false;
//    console.log("game: receive_shot TURN OFF");
    npc_receive_shot(i, answer_other_shot);
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
    var boats = boat_string.split(' '); // convert string to array
    console.log("game: " + JSON.stringify(boats));
    if (parseInt(boats[0]) != width ||parseInt(boats[1]) != height) {
        console.log("game: received grid but dim do not match");
    }
    var grid = new Array(width * height + 1).join("0");
    boats = boats.slice(2);
    boats.forEach(function (b) {
        var ts = get_tile_state_by_number(b[0]);
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