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
  const ciphertext = data.slice(0, 16);
  const tag = data.slice(16, 32);
  const iv = data.slice(32, 44);
  const decipher = crypto.createDecipheriv('aes-128-gcm', key, iv);
  decipher.setAuthTag(tag);
  decipher.setAutoPadding(false);
  let decrypted = decipher.update(ciphertext);
  let plaintext = Buffer.concat([decrypted, decipher.final()]);
  const db = admin.database();
  const timeStamp = new Date().toJSON();
  const refSensor1 = db.ref("sensor1");
  refSensor1.push().set({
    date: timeStamp,
    value: plaintext.readUInt16BE(0),
  });
  const refSensor2 = db.ref("sensor2");
  refSensor2.push().set({
    date: timeStamp,
    value: plaintext.readUInt16BE(2),
  });
  res.send('Values Pushed');
});

app.get('/data', (req, res) => {
  const data = {};
  const db = admin.database();
  const refSensor1 = db.ref('sensor1');
  refSensor1.once('value').then((snapshot) => {
    data['sensor1'] = snapshot.val();
    refSensor2 = db.ref('sensor2');
    return refSensor2.once('value'); 
  }).then((snapshot) => {
    data['sensor2'] = snapshot.val();
    return res.json(data);
  }).catch((error) => {
    console.log('The read failed: ' + error.code);
  });
});

app.listen(port, () => console.log(`Example app listening on port ${port}!`));
