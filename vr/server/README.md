# The Flaux VR server

This small program lets people build together in Flaux VR **over the
internet**. Every headset connects to it and types a **room code**; everyone
with the same code is in the same room, sharing one scene.

It's one file, [`server.js`](server.js), and needs nothing installed except
[Node.js](https://nodejs.org) 18 or newer.

There are two ways to run it. Pick one.

## A. On a free cloud host (Render)

Works for friends anywhere, with nothing to set up at home.

1. Make a free account at [render.com](https://render.com) and connect your
   GitHub account.
2. Click **New**, then **Blueprint**, and pick this repository. Render reads
   [`render.yaml`](../../render.yaml) and sets everything up. Click **Apply**.
3. When it says **Live**, copy the address it gives you, such as
   `flaux-vr-server.onrender.com`.
4. In Flaux VR, open **Rooms**. Under **Over the internet**, type that address
   as the **Server address**, pick a **Room code** (any letters and numbers,
   like `FLAUX`), and press **Join**. Your friends type the same two things.

> Render's free servers go to sleep when nobody has used them for a while.
> The first person to join afterwards waits up to a minute; if it says
> "stopped answering", press **Join** again.

Any other host that runs Node.js works the same way: the command is
`node server.js`, and it listens on the port in the `PORT` variable.

## B. On your own computer

```
node server.js
```

It says `Flaux VR server listening on port 8080`. In Flaux VR, the server
address is then `ws://` + that computer's address + `:8080`, for example
`ws://192.168.1.50:8080` (on the keyboard, `:` and `/` are on the **?123**
page).

- Friends on **your Wi-Fi** can join straight away.
- Friends **somewhere else** need your router to send port 8080 to that
  computer ("port forwarding"), and they type your public address instead. A
  cloud host (way A) is easier.

To use another port: `PORT=9000 node server.js`.

## Good to know

- A room is made the first time someone joins it, and holds up to 8 players
  and 2,048 shapes. The server remembers a room's scene for 30 minutes after
  the last player leaves; after that, or if the server restarts, the room
  starts empty. **Save your build in Projects** on a headset to keep it.
- Anyone who knows the server address and the room code can join and build.
  Pick a room code that's hard to guess if that matters to you.
- Opening the server's address in a web browser shows how many rooms and
  players it has.
- `wss://` addresses (what cloud hosts give you) are encrypted, and the
  headset checks the server's certificate. `ws://` addresses are not
  encrypted. To serve `wss://` from your own computer, give the server a
  certificate: `TLS_CERT=fullchain.pem TLS_KEY=privkey.pem node server.js`.
