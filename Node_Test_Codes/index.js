const net = require('net')

const ESP_IP = '10.128.32.93'   // coloque o IP real que aparece no Serial
const ESP_PORT = 9000

const client = new net.Socket()

client.connect(ESP_PORT, ESP_IP, () => {
    console.log("Conectado ao ESP32!")
})

client.write("Ping/n");

client.on('data', (data) => {
    console.log("Recebido:", data.toString().trim());
})

client.on('close', () => {
    console.log("Conexão encerrada pelo ESP32")
})

client.on('error', (err) => {
    console.log("Erro:", err.message)
})
