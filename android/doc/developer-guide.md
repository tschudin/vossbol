# TinyTremola - Developer Guide

TinyTremola is a decentralized Android chat app that replicates messages between users using append-only logs based on the tinySSB protocol, without the need for a centralized server.

This documentation provides a brief overview of the software architecture of the TinyTremola app, as well as some initial guidance for extending the application.

The IDE [Android Studio](https://developer.android.com/studio) is recommended for working on this project.

- [Overview](#overview)
- [Frontend](#frontend)
  - [Scenarios](#scenarios)
  - [Web Storage](#web-storage)
  - [Communication with Backend](#communication-with-backend)
  - [Browser-Only Testing](#browser-only-testing)
- [Backend](#backend)
  - [Communication with Frontend](#communication-with-frontend)
  - [Replication](#replication)
<a name="overview"></a>
## Overview

The TinyTremola app is divided into two layers: the backend and the frontend.

The backend is written in Kotlin and forms the core of the application. It handles the Android environment (intents, permissions, etc.) and loads the frontend. Additionally, it is responsible for storing and replicating data based on the tinySSB protocol.
The frontend serves as the presentation layer. It processes the data received and validated by the backend to present it to the user. Furthermore, the user can input data through the provided user interface. This data is then passed back to the backend, which encodes it to a valid log entry and replicates it.

The following sections present important concepts and methods of both the frontend and backend.

<a name="frontend"></a>
##  Frontend

The code and associated resources for the frontend can be found under [android/app/src/main/assets/web](/android/app/src/main/assets/web/).
At startup, the backend loads the HTML file `tremola.html` and presents it to the user. This allows to include JavaScript and CSS as is common with HTML.

### Scenarios

The user interface of TinyTremola is based on a scenario system implemented in `tremola_ui.js`. The idea is that the user can switch between different scenarios, where only the HTML elements defined in the selected scenario are displayed, while all others are hidden.

In order to create a new scenario, first all the IDs of the HTML elements that should be displayed when the scenario is selected must be entered in the `display_or_not` list. Then a new entry is added to `scenarioDisplay`, where the key corresponds to the name of the new scenario and the value is an array of IDs of the HTML elements to be displayed for this scenario.
For each scenario the entries of the menu (the three overlapping bars at the top right corner of the UI) can be configured.
This is done by adding a new entry for the new scenario in `scenarioMenu` and creating a tuple per menu entry. The first value of the tuple represents the name of the menu entry, while the second value represents the method to be called when the option is selected.

Now the new scenario can be called with `setScenario()` and all previously defined elements will be displayed.

<a name="web-storage"></a>
### Web Storage

Frontend applications can store processed data or auxiliary data structures using the HTML web storage.
For this reason, there is the object `tremola` that can be accessed through JavaScript. Data for the frontend can be stored in this dictionary data structure.
To make the changes persistent, so that they remain available even after restarting the app, the `persist()` method needs to be called after every modification.

> Note: Only the frontend can access the web storage. The data stored there is not replicated by the backend and therefore not accessible to other users.

<a name="communication-with-backend"></a>
### Communication with Backend

The Frontend can call functions provided by the backend via a bridge in order to, for example, store and replicate new data in the user's append-only log.
To achieve this, the method `backend()` in `tremola.js` can be used. The provided string is then forwarded to the backend counterpart.

Conversely, the backend calls the `b2f_new_event()` method when a new tinySSB message is received. There, the received data can be processed based on the corresponding frontend application.

<a name="browser-only-testing"></a>
### Browser-Only Testing

Since the frontend is implemented entirely in HTML and JavaScript, `tremola.html` can be run in a standard web browser. Here, you can interact with the user interface just as if it were running on the Android app. Additionally, you have access to additional tools of the web browser, such as the console, that can assist you in debugging. Since the backend is not present in this test setup, it can be helpful to simulate backend responses to frontend requests in the `backend()` method.

> Browser-only testing is very useful for testing the frontend part of an application. However, it should always be ensured by testing the app on Android devices, that the communication with the backend and thus the replication of the application data has been implemented correctly.

<a name="backend"></a>
## Backend

The Kotlin Code for the Tremola backend can be found under [android/app/src/main/java/nz/scuttlebutt/tremolavossbol](/android/app/src/main/java/nz/scuttlebutt/tremolavossbol/). When the app is launched, `MainActivity.kt` is executed, which initializes all necessary components such as TinySSB feeds, UDP broadcast, Bluetooth Low Energy, as well as the frontend.

<a name="#communication-with-frontend"></a>
### Communication with Frontend

As described in the frontend section, the frontend can invoke backend functions using the `backend()` method. This method calls the `onFrontendRequest()` function located in `WebAppInterface.kt`. Within this function, the specified actions for the corresponding command and arguments provided by the passed string are then executed in the backend.

To invoke frontend functions from the backend, the backend calls `eval()`. Within this function, JavaScript methods can be called along with the specified parameters.
When the backend receives a new TinySSB event, it calls the `b2f_new_event()` function of the frontend, passing a string containing all attributes of the log entry to the frontend.

<a name="#replication"></a>
### Replication

In order to exchange data between multiple users in a decentralized manner, the tinySSB protocol was implemented in Tremola. New data is appended as a new entry to the user's append-only-log and replicated to other users via WiFi or Bluetooth low energy.

For this purpose, the data is converted into the BIPF format. The necessary functions for this can be found in `Bipf.kt`. The data, combined in a BIPF list, can then be added to the user's feed using `publish_public_content()`. The backend implementation takes care of packaging the data into a valid log entry and replicating the new data to other users.

> To enable replication via Bluetooth Low Energy, location and Bluetooth needs to be enabled, and the app must be granted location permissions.
