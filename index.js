const express = require('express');
const admin = require('firebase-admin');
const bodyParser = require('body-parser');
const crypto = require('crypto');

const serviceAccount = require("./serviceAccountKey.json");

const firebaseApp = admin.initializeApp({
  credential: admin.credential.cert(serviceAccount),
  databaseURL: "firebase-database-url",
});

const key = Buffer.from('00112233445566778899AABBCCDDEEFF', 'hex');

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
  const refSensor1 = db.ref("sensor1");
  refSensor1.push().set({
    date: timestamp,
    value: plaintext.readUInt16BE(0),
  });
  const refSensor2 = db.ref("sensor2");
  refSensor2.push().set({
    date: timestamp,
    value: plaintext.readUInt16BE(2),
  });
  const refTemp = db.ref("temperature");
  refTemp.push().set({
    date: timestamp,
    value: plaintext.readUInt16BE(4),
  });
  const refLight = db.ref("light");
  refLight.push().set({
    date: timestamp,
    value: plaintext.readUInt16BE(6),
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
  const data = {};
  const db = admin.database();
  const refSensor1 = db.ref('sensor1');
  const currentTimestamp = Date.now();
  const startTimestamp = currentTimestamp - 14 * 24 * 60 * 60 * 1000;
  const startDate = new Date(startTimestamp).toJSON();
  refSensor1
    .orderByChild('date')
    .startAt(startDate)
    .once('value')
    .then((snapshot) => {
      data['sensor1'] = snapshot.val();
      refSensor2 = db.ref('sensor2');
      return refSensor2
        .orderByChild('date')
        .startAt(startDate)
        .once('value'); 
    }).then((snapshot) => {
      data['sensor2'] = snapshot.val();
      return db.ref('temperature').once('value');
    }).then((snapshot) => {
      data['temperature'] = snapshot.val();
      return db.ref('light').once('value');
    }).then((snapshot) => {
      data['light'] = snapshot.val();
      return res.json(data);
    }).catch((error) => {
      console.log('The read failed: ' + error.code);
    });
});

app.listen(port, () => console.log(`Example app listening on port ${port}!`));
