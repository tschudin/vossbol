// tremola_settings.js

"use strict";

function get_default_settings() {
    return {
        'enable_preview': false,
        'background_map': true,
        'websocket': false,  // TODO back to true
        'show_shortnames': true,
        'hide_forgotten_conv': true,
        'hide_forgotten_contacts': true,
        'udp_multicast': true,
        'ble': true
    }
}

function toggle_changed(e) {
    // console.log("toggle ", e.id);
    tremola.settings[e.id] = e.checked;
    backend("settings:set " + e.id + " " + e.checked)
    persist()
    applySetting(e.id, e.checked);
}

function getSetting(nm) {
    return document.getElementById(nm).checked
}

function applySetting(nm, val) {
    if (nm == 'background_map') {
        if (val)
            document.body.style.backgroundImage = "url('img/splash-as-background.jpg')";
        else
            document.body.style.backgroundImage = null;
    } else if (nm == 'hide_forgotten_conv') {
        load_chat_list();
    } else if (nm == 'hide_forgotten_contacts') {
        load_contact_list();
    }
}

function setSetting(nm, val) {
    // console.log("setting", nm, val)
    applySetting(nm, val);
    console.log("DEBUG: " + nm)
    document.getElementById(nm).checked = val;
}

/* async */
function settings_wipe() {
    closeOverlay();
    backend("wipe"); // will not return
    /*
    window.localStorage.setItem("tremola", "null");
    backend("ready"); // will call initialize()
    await new Promise(resolve => setTimeout(resolve, 500));
    // resetTremola();
    menu_redraw();
    setScenario('chats');
    */
}

// eof
