const express = require('express');
const admin = require('firebase-admin');
const bodyParser = require('body-parser');
const crypto = require('crypto');

const serviceAccount = require("./serviceAccountKey.json");

const firebaseApp = admin.initializeApp({
  credential: admin.credential.cert(serviceAccount),
  databaseURL: "{{DATABASE_URL}}",
});

const key = Buffer.from('{{JS_PASS}}', 'hex');

const app = express();
app.use(bodyParser.raw());
app.use(express.static('public'));
const port = process.env.PORT || 3000;

app.post('/push', (req, res) => {
  const data = req.body;
  const ciphertextFromArduino = data.slice(0, 16);
  const signatureFromArduino = data.slice(16, 32);
  const ivFromArduino = data.slice(32, 44); // Randoms sent on last response
  const decipher = crypto.createDecipheriv('aes-128-gcm', key, ivFromArduino);
  decipher.setAuthTag(signatureFromArduino);
  decipher.setAutoPadding(false);
  let partialPlaintext = decipher.update(ciphertextFromArduino);
  let plaintext = Buffer.concat([partialPlaintext, decipher.final()]);
  const db = admin.database();
  const timestamp = new Date().toJSON();
  db.ref().push().set({
    date: timestamp,
    moisture1: plaintext.readUInt16BE(0),
    moisture2: plaintext.readUInt16BE(2),
    moisture3: plaintext.readUInt16BE(4),
    moisture4: plaintext.readUInt16BE(6),
    temperature1: plaintext.readUInt16BE(8),
    temperature2: plaintext.readUInt16BE(10),
    light1: plaintext.readUInt16BE(12),
    light2: plaintext.readUInt16BE(14),
  });
  const randomsForArduino = Buffer.alloc(12);
  crypto.randomFillSync(randomsForArduino);
  const ivForResponse = Buffer.alloc(12);
  crypto.randomFillSync(ivForResponse);
  const cipher = crypto.createCipheriv('aes-128-gcm', key, ivForResponse);
  cipher.setAAD(randomsForArduino);
  cipher.final();
  const signatureOfResponse = cipher.getAuthTag();
  res.send(Buffer.concat([
    randomsForArduino, // 12 bytes
    signatureOfResponse, // 16 bytes
    ivForResponse, // 12 bytes
  ]));
});

app.get('/data', (req, res) => {
  const currentTimestamp = Date.now();
  const startTimestamp = currentTimestamp - 14 * 24 * 60 * 60 * 1000;
  const startDate = new Date(startTimestamp).toJSON();
  admin
    .database()
    .ref()
    .orderByChild('date')
    .startAt(startDate)
    .once('value')
    .then((snapshot) => {
      data = snapshot.val();
      const moisture1 = [];
      const moisture2 = [];
      const moisture3 = [];
      const moisture4 = [];
      const temperature1 = [];
      const temperature2 = [];
      const light1 = [];
      const light2 = [];
        Object.values(data).forEach(data => {
          moisture1.push({ value: data.moisture1, date: data.date });
          moisture2.push({ value: data.moisture2, date: data.date });
          moisture3.push({ value: data.moisture3, date: data.date });
          moisture4.push({ value: data.moisture4, date: data.date });
          temperature1.push({ value: data.temperature1, date: data.date });
          temperature2.push({ value: data.temperature2, date: data.date });
          light1.push({ value: data.light1, date: data.date });
          light2.push({ value: data.light2, date: data.date });
        })
      return res.json({
        moisture1, moisture2, moisture3, moisture4,
        temperature1, temperature2, light1, light2
      });
    }).catch((error) => {
      console.error(error);
    });
});

app.listen(port, () => console.log(`Plant-monitor listening on port ${port}!`));
