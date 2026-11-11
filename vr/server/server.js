// The Flaux VR server: rooms over the internet.
//
// Every headset connects here with a WebSocket and names a room code.  Everyone who gives the
// same code is in the same room: they share one scene and see each other.  The server keeps each
// room's scene, so it's still there when the last player leaves (for a while: see EMPTY_ROOM_MINUTES).
//
//     node server.js            (needs Node 18 or newer; nothing to install)
//
// It listens on the port in the PORT environment variable, or 8080.  Cloud hosts such as Render
// set PORT themselves and put https in front, so the address to type in the headset is
// wss://your-app.onrender.com (or just your-app.onrender.com).  On your own computer it's
// ws://<its address>:8080.
//
// What travels is plain text lines (see ../src/net.c):
//     room <code>, hello <name>           from a headset when it arrives
//     you <id>, full, obj..., endfull     to it: its player number and the room's scene
//     obj <id> ...   del <id>   pose <id> ...     passed on to everyone else in the room
'use strict';
const http = require('http');
const crypto = require('crypto');

const PORT = Number(process.env.PORT) || 8080;
const MAX_ROOMS = 200, MAX_PLAYERS = 8, MAX_OBJECTS = 2048, MAX_LINE = 4000;
const EMPTY_ROOM_MINUTES = 30;

const rooms = new Map();          // code -> { objects: Map(id -> line), players: Set(client), emptySince, nextId }

function sendFrame(socket, opcode, payload) {
  const data = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  let head;
  if (data.length < 126) head = Buffer.from([0x80 | opcode, data.length]);
  else if (data.length < 65536) { head = Buffer.alloc(4); head[0] = 0x80 | opcode; head[1] = 126; head.writeUInt16BE(data.length, 2); }
  else { head = Buffer.alloc(10); head[0] = 0x80 | opcode; head[1] = 127; head.writeBigUInt64BE(BigInt(data.length), 2); }
  if (!socket.destroyed) socket.write(Buffer.concat([head, data]));
}
const say = (client, text) => sendFrame(client.socket, 1, text);

function leave(client) {
  const room = client.room;
  if (!room) return;
  client.room = null;
  room.players.delete(client);
  if (room.players.size === 0) room.emptySince = Date.now();
}

function onLine(client, line) {
  if (line.length > MAX_LINE) return;
  const space = line.indexOf(' ');
  const word = space < 0 ? line : line.slice(0, space), rest = space < 0 ? '' : line.slice(space + 1);

  if (word === 'room' && !client.room) {
    const code = rest.trim().toUpperCase();
    if (!/^[A-Z0-9_-]{1,20}$/.test(code)) { say(client, 'sorry Room codes are letters and numbers, like FLAUX\n'); return client.socket.end(); }
    let room = rooms.get(code);
    if (!room) {
      if (rooms.size >= MAX_ROOMS) { say(client, 'sorry This server is full. Try again later.\n'); return client.socket.end(); }
      room = { objects: new Map(), players: new Set(), emptySince: 0, nextId: 2 };
      rooms.set(code, room);
    }
    if (room.players.size >= MAX_PLAYERS) { say(client, `sorry Room ${code} is full (${MAX_PLAYERS} players)\n`); return client.socket.end(); }
    client.room = room; client.code = code;
    room.players.add(client);
    return;
  }
  const room = client.room;
  if (!room) return;

  if (word === 'hello' && !client.id) {
    client.id = room.nextId++;
    client.name = rest.trim().slice(0, 19);
    let all = `you ${client.id}\nfull\n`;
    for (const obj of room.objects.values()) all += obj + '\n';
    say(client, all + 'endfull\n');
    console.log(`${client.name || 'someone'} joined ${client.code} (${room.players.size} there, ${room.objects.size} shapes)`);
    return;
  }
  if (!client.id) return;

  if (word === 'obj') {
    const id = rest.split(' ', 1)[0];
    if (!/^\d+$/.test(id)) return;
    if (!room.objects.has(id) && room.objects.size >= MAX_OBJECTS) return;
    room.objects.set(id, line);
  } else if (word === 'del') {
    room.objects.delete(rest.trim());
  } else if (word !== 'pose') {
    return;                                  // nothing else is passed on
  }
  for (const other of room.players) if (other !== client && other.id) say(other, line + '\n');
}

function onData(client, chunk) {
  client.buffer = Buffer.concat([client.buffer, chunk]);
  if (client.buffer.length > 1 << 20) return client.socket.destroy();
  for (;;) {
    const b = client.buffer;
    if (b.length < 2) return;
    const opcode = b[0] & 15, masked = b[1] & 0x80;
    let len = b[1] & 127, head = 2;
    if (len === 126) { if (b.length < 4) return; len = b.readUInt16BE(2); head = 4; }
    else if (len === 127) return client.socket.destroy();
    if (!masked) return client.socket.destroy();         // headsets (and browsers) always mask
    if (b.length < head + 4 + len) return;
    const key = b.subarray(head, head + 4), payload = Buffer.from(b.subarray(head + 4, head + 4 + len));
    for (let i = 0; i < len; i++) payload[i] ^= key[i & 3];
    client.buffer = b.subarray(head + 4 + len);
    if (opcode === 8) return client.socket.end();
    if (opcode === 9) { sendFrame(client.socket, 10, payload); continue; }
    if (opcode > 2) continue;
    client.text += payload.toString('utf8');
    let end;
    while ((end = client.text.indexOf('\n')) >= 0) {
      const line = client.text.slice(0, end).replace(/\r$/, '');
      client.text = client.text.slice(end + 1);
      if (line) onLine(client, line);
    }
    if (client.text.length > MAX_LINE) client.text = '';
  }
}

// On your own computer you can serve wss:// yourself by giving it a certificate:
//     TLS_CERT=fullchain.pem TLS_KEY=privkey.pem node server.js
const tls = process.env.TLS_CERT && process.env.TLS_KEY
  ? { cert: require('fs').readFileSync(process.env.TLS_CERT), key: require('fs').readFileSync(process.env.TLS_KEY) } : null;
const createServer = handler => tls ? require('https').createServer(tls, handler) : http.createServer(handler);

const server = createServer((req, res) => {             // what a web browser sees
  let players = 0;
  for (const room of rooms.values()) players += room.players.size;
  res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end(`This is a Flaux VR server. ${rooms.size} room${rooms.size === 1 ? '' : 's'}, ${players} player${players === 1 ? '' : 's'}.\n` +
          'In Flaux VR, open Rooms, type this address and a room code, and press Join.\n');
});

server.on('upgrade', (req, socket) => {
  const key = req.headers['sec-websocket-key'];
  if (!key || String(req.headers.upgrade).toLowerCase() !== 'websocket') return socket.destroy();
  const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
               `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
  socket.setNoDelay(true);
  const client = { socket, buffer: Buffer.alloc(0), text: '', room: null, id: 0, name: '', code: '' };
  socket.on('data', chunk => onData(client, chunk));
  socket.on('close', () => leave(client));
  socket.on('error', () => leave(client));
});

setInterval(() => {                                      // forget rooms nobody has been in for a while
  for (const [code, room] of rooms)
    if (room.players.size === 0 && Date.now() - room.emptySince > EMPTY_ROOM_MINUTES * 60000) rooms.delete(code);
}, 60000).unref();

server.listen(PORT, () => console.log(`Flaux VR server listening on port ${PORT}${tls ? ' (wss)' : ''}`));
