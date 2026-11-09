#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <time.h>

// wifi
const char* ssid = "iPhone";
const char* password = "maltaca120";

// pinos
const int PINO_SENSOR = 34;
const int PINO_RELE = 26;
const int PINO_LED = 27;

// config
const int LIMITE_ALCOOL = 15;
const int ZONA_MORTA = 30;
const int FAIXA_SENSOR = 1200;

const unsigned long DURACAO_TESTE = 30000;
const unsigned long INTERVALO_LEITURA = 500;
const unsigned long INTERVALO_LOG = 1000;

// Ajuste se o rele funcionar invertido
const bool RELE_ATIVO_EM_LOW = true;

const char* ARQUIVO_LOG = "/historico.csv";

WebServer server(80);

enum Estado {
  EM_TESTE,
  LIBERADO,
  BLOQUEADO
};

Estado estado = EM_TESTE;

int referencia = 0;
int leituraBruta = 0;
int nivelAlcool = 0;
int picoAlcool = 0;

unsigned long inicioTeste = 0;
unsigned long ultimaLeitura = 0;
unsigned long ultimoLog = 0;

bool armazenamentoOK = false;
bool horarioSincronizado = false;

//ler data e hora 
String dataHora() {
  struct tm tempo;

  if (!getLocalTime(&tempo, 50)) {
    return "SEM_HORARIO";
  }

  char buffer[25];

  strftime(
    buffer,
    sizeof(buffer),
    "%Y-%m-%d %H:%M:%S",
    &tempo
  );

  return String(buffer);
}

// logs
void registrarLog(const String& evento) {
  if (!armazenamentoOK) return;

  File arquivo = LittleFS.open(
    ARQUIVO_LOG,
    "a"
  );

  if (!arquivo) {
    Serial.println("Erro ao gravar log!");
    return;
  }

  arquivo.print(dataHora());
  arquivo.print(",");
  arquivo.print(evento);
  arquivo.print(",");
  arquivo.print(nivelAlcool);
  arquivo.print(",");
  arquivo.print(picoAlcool);
  arquivo.print(",");

  switch (estado) {
    case EM_TESTE:
      arquivo.println("EM_TESTE");
      break;

    case LIBERADO:
      arquivo.println("LIBERADO");
      break;

    case BLOQUEADO:
      arquivo.println("BLOQUEADO");
      break;
  }

  arquivo.close();
}

void iniciarArmazenamento() {
  armazenamentoOK = LittleFS.begin(true);

  if (!armazenamentoOK) {
    Serial.println("Erro no LittleFS!");
    return;
  }

  if (!LittleFS.exists(ARQUIVO_LOG)) {
    File arquivo = LittleFS.open(
      ARQUIVO_LOG,
      "w"
    );

    if (arquivo) {
      arquivo.println(
        "data_hora,evento,alcool_pct,pico_pct,estado"
      );
      arquivo.close();
    }
  }
}

// relé e led
void atualizarSaidas() {
  bool acionar = estado != LIBERADO;

  digitalWrite(
    PINO_RELE,
    acionar
      ? (RELE_ATIVO_EM_LOW ? LOW : HIGH)
      : (RELE_ATIVO_EM_LOW ? HIGH : LOW)
  );

  digitalWrite(
    PINO_LED,
    acionar ? HIGH : LOW
  );
}

//mq03
int lerMediaSensor() {
  long soma = 0;

  for (int i = 0; i < 20; i++) {
    soma += analogRead(PINO_SENSOR);
    delay(2);
  }

  return soma / 20;
}

void calibrarSensor() {
  Serial.println("Calibrando em ar limpo...");

  long soma = 0;

  for (int i = 0; i < 100; i++) {
    soma += lerMediaSensor();
    delay(20);
  }

  referencia = soma / 100;
  leituraBruta = referencia;
  nivelAlcool = 0;
  picoAlcool = 0;

  Serial.print("Referencia: ");
  Serial.println(referencia);
}

void atualizarSensor() {
  leituraBruta = lerMediaSensor();

  int diferenca = leituraBruta - referencia;

  if (diferenca <= ZONA_MORTA) {
    nivelAlcool = 0;
  } else {
    nivelAlcool = map(
      diferenca - ZONA_MORTA,
      0,
      FAIXA_SENSOR,
      0,
      100
    );

    nivelAlcool = constrain(
      nivelAlcool,
      0,
      100
    );
  }

  // Captura o maior valor 
  if (estado == EM_TESTE && nivelAlcool > picoAlcool) {
    picoAlcool = nivelAlcool;

    registrarLog("NOVO_PICO");
  }

  Serial.print("Alcool: ");
  Serial.print(nivelAlcool);
  Serial.print("% | Pico: ");
  Serial.print(picoAlcool);
  Serial.println("%");
}

// teste
void iniciarTeste() {
  estado = EM_TESTE;

  nivelAlcool = 0;
  picoAlcool = 0;

  atualizarSaidas();

  inicioTeste = millis();
  ultimaLeitura = millis();
  ultimoLog = millis();

  registrarLog("INICIO_TESTE");

  Serial.println("Teste iniciado: 30 segundos");
}

void verificarTeste() {
  if (estado != EM_TESTE) return;

  if (millis() - inicioTeste < DURACAO_TESTE) {
    return;
  }
  if (picoAlcool >= LIMITE_ALCOOL) {
    estado = BLOQUEADO;
    Serial.println("TESTE REPROVADO!");
  } else {
    estado = LIBERADO;
    Serial.println("TESTE APROVADO!");
  }

  atualizarSaidas();

  registrarLog("FIM_TESTE");
}

// página web
const char pagina[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport"
content="width=device-width, initial-scale=1">

<title>Bafometro ESP32</title>

<style>
* { box-sizing: border-box; }

body {
  margin: 0;
  background: #0b1220;
  color: white;
  font-family: Arial, sans-serif;
  padding: 20px;
}

.card {
  max-width: 520px;
  margin: 30px auto;
  padding: 26px;
  background: #182338;
  border-radius: 20px;
  text-align: center;
}

h1 { margin-top: 0; }

.subtitulo, .info {
  color: #94a3b8;
}

.numero {
  font-size: 76px;
  font-weight: bold;
  margin: 20px 0;
}

.barra {
  height: 18px;
  background: #334155;
  border-radius: 20px;
  overflow: hidden;
}

#progresso {
  height: 100%;
  width: 0%;
  background: #22c55e;
  transition: width .4s;
}

.pico {
  font-size: 24px;
  margin: 20px 0;
}

.tempo {
  font-size: 35px;
  font-weight: bold;
  margin: 18px 0;
}

.status {
  font-size: 21px;
  font-weight: bold;
  margin: 20px 0;
}

.indicador {
  background: #0f172a;
  padding: 14px;
  border-radius: 12px;
  margin: 12px 0;
}

.led {
  display: inline-block;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #475569;
  margin-right: 8px;
}

.led.aceso {
  background: #ef4444;
  box-shadow: 0 0 14px #ef4444;
}

button, a.botao {
  display: block;
  width: 100%;
  padding: 13px;
  margin-top: 12px;
  border: 0;
  border-radius: 10px;
  color: white;
  background: #2563eb;
  text-decoration: none;
  font-size: 15px;
  cursor: pointer;
}

button:disabled {
  opacity: .45;
  cursor: not-allowed;
}

.vermelho { background: #dc2626; }

.historico {
  text-align: left;
  margin-top: 25px;
}

pre {
  background: #0f172a;
  color: #cbd5e1;
  padding: 12px;
  border-radius: 10px;
  overflow: auto;
  font-size: 11px;
  max-height: 280px;
}
</style>
</head>

<body>
<div class="card">

<h1>Bafometro ESP32</h1>
<p class="subtitulo">Monitoramento em tempo real</p>

<div class="numero">
  <span id="nivel">0</span>%
</div>

<div class="barra">
  <div id="progresso"></div>
</div>

<div class="pico">
  Pico registrado:
  <strong id="pico">0%</strong>
</div>

<p class="info">Tempo restante do teste</p>
<div class="tempo" id="tempo">30s</div>

<div class="status" id="status">
  Iniciando...
</div>

<div class="indicador">
  <span class="led" id="led"></span>
  <span id="estadoLed">Aguardando</span>
</div>

<div class="indicador" id="rele">
  Rele: aguardando
</div>

<p class="info" id="adc">ADC: --</p>
<p class="info" id="horario">Horario: --</p>

<button id="btnCalibrar" onclick="calibrar()">
  Zerar sensor (somente apos teste aprovado)
</button>

<button class="vermelho" onclick="reiniciar()">
  Reiniciar ESP32 e iniciar novo teste
</button>

<div class="historico">
  <h3>Historico de medicoes</h3>

  <a class="botao" href="/logs" download="historico.csv">
    Baixar historico CSV
  </a>

  <button onclick="carregarHistorico()">
    Atualizar historico
  </button>

  <pre id="historico">Carregando...</pre>
</div>

</div>

<script>
let ultimoEstado = "";

async function atualizar() {
  try {
    const resposta = await fetch("/dados", {
      cache: "no-store"
    });

    if (!resposta.ok) throw new Error("Falha");

    const dados = await resposta.json();

    document.getElementById("nivel").textContent =
      dados.nivel;

    document.getElementById("pico").textContent =
      dados.pico + "%";

    document.getElementById("tempo").textContent =
      dados.tempoRestante + "s";

    document.getElementById("adc").textContent =
      "ADC: " + dados.adc +
      " | Referencia: " + dados.referencia;

    document.getElementById("horario").textContent =
      "Horario: " + dados.horario;

    const barra = document.getElementById("progresso");
    const status = document.getElementById("status");
    const led = document.getElementById("led");
    const estadoLed = document.getElementById("estadoLed");
    const rele = document.getElementById("rele");

    barra.style.width = dados.nivel + "%";

    if (dados.estado === "EM_TESTE") {
      status.textContent = "TESTE EM ANDAMENTO";
      status.style.color = "#fbbf24";
      barra.style.background = "#fbbf24";
    }

    if (dados.estado === "LIBERADO") {
      status.textContent = "TESTE APROVADO - ACESSO LIBERADO";
      status.style.color = "#22c55e";
      barra.style.background = "#22c55e";
    }

    if (dados.estado === "BLOQUEADO") {
      status.textContent = "TESTE REPROVADO - BLOQUEIO TRAVADO";
      status.style.color = "#ef4444";
      barra.style.background = "#ef4444";
    }

    const acionado = dados.estado !== "LIBERADO";

    led.classList.toggle("aceso", acionado);

    estadoLed.textContent =
      acionado ? "LED aceso" : "LED apagado";

    rele.textContent =
      acionado ? "RELE ACIONADO" : "RELE DESACIONADO";

    document.getElementById("btnCalibrar").disabled =
      dados.estado !== "LIBERADO";

    if (ultimoEstado !== dados.estado) {
      ultimoEstado = dados.estado;
      carregarHistorico();
    }

  } catch (erro) {
    document.getElementById("status").textContent =
      "Sem comunicacao com ESP32";
  }
}

async function carregarHistorico() {
  try {
    const resposta = await fetch("/logs", {
      cache: "no-store"
    });

    const csv = await resposta.text();
    const linhas = csv.trim().split("\n");

    document.getElementById("historico").textContent =
      linhas.slice(-21).join("\n");

  } catch (erro) {
    document.getElementById("historico").textContent =
      "Erro ao carregar historico";
  }
}

async function calibrar() {
  const resposta = await fetch("/calibrar", {
    method: "POST"
  });

  if (!resposta.ok) {
    alert("Calibracao indisponivel.");
    return;
  }

  atualizar();
}

async function reiniciar() {
  if (!confirm("Reiniciar e iniciar novo teste?")) {
    return;
  }

  try {
    const resposta = await fetch("/reiniciar", {
      method: "POST"
    });

    if (!resposta.ok) throw new Error("Falha");

    document.getElementById("status").textContent =
      "Reiniciando...";

    setTimeout(() => location.reload(), 7000);

  } catch (erro) {
    alert("Falha ao reiniciar.");
  }
}

setInterval(atualizar, 1000);
setInterval(carregarHistorico, 5000);

atualizar();
carregarHistorico();
</script>
</body>
</html>
)HTML";

//setup 
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Estado inicial: teste bloqueado
  digitalWrite(
    PINO_RELE,
    RELE_ATIVO_EM_LOW ? LOW : HIGH
  );
  pinMode(PINO_RELE, OUTPUT);

  digitalWrite(PINO_LED, HIGH);
  pinMode(PINO_LED, OUTPUT);

  estado = EM_TESTE;
  atualizarSaidas();

  analogReadResolution(12);
  analogSetPinAttenuation(
    PINO_SENSOR,
    ADC_11db
  );

  iniciarArmazenamento();

  calibrarSensor();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Conectando WiFi");

  // Evita espera infinita
  unsigned long inicioWiFi = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicioWiFi < 15000
  ) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Horario de Brasilia: UTC-3
    configTime(
      -3 * 3600,
      0,
      "pool.ntp.org",
      "time.google.com"
    );

    struct tm tempo;
    horarioSincronizado = getLocalTime(&tempo, 5000);

    Serial.println(
      horarioSincronizado
        ? "Horario sincronizado"
        : "Horario ainda nao sincronizado"
    );
  } else {
    Serial.println("WiFi indisponivel");
  }

  //rotas
  server.on("/", []() {
    server.send_P(
      200,
      "text/html; charset=utf-8",
      pagina
    );
  });

  server.on("/dados", []() {
    unsigned long restante = 0;

    if (estado == EM_TESTE) {
      unsigned long decorrido =
        millis() - inicioTeste;

      if (decorrido < DURACAO_TESTE) {
        restante = (
          DURACAO_TESTE - decorrido + 999
        ) / 1000;
      }
    }

    String nomeEstado;

    if (estado == EM_TESTE) {
      nomeEstado = "EM_TESTE";
    } else if (estado == LIBERADO) {
      nomeEstado = "LIBERADO";
    } else {
      nomeEstado = "BLOQUEADO";
    }

    String json = "{";

    json += "\"nivel\":" + String(nivelAlcool);
    json += ",\"pico\":" + String(picoAlcool);
    json += ",\"adc\":" + String(leituraBruta);
    json += ",\"referencia\":" + String(referencia);
    json += ",\"tempoRestante\":" + String(restante);
    json += ",\"estado\":\"" + nomeEstado + "\"";
    json += ",\"horario\":\"" + dataHora() + "\"";

    json += "}";

    server.send(
      200,
      "application/json",
      json
    );
  });

  server.on("/logs", HTTP_GET, []() {
    if (!armazenamentoOK) {
      server.send(500, "text/plain", "Sem armazenamento");
      return;
    }

    File arquivo = LittleFS.open(
      ARQUIVO_LOG,
      "r"
    );

    if (!arquivo) {
      server.send(404, "text/plain", "Sem historico");
      return;
    }

    server.streamFile(
      arquivo,
      "text/csv; charset=utf-8"
    );

    arquivo.close();
  });

  server.on("/calibrar", HTTP_POST, []() {
    if (estado != LIBERADO) {
      server.send(
        423,
        "text/plain",
        "Calibracao indisponivel durante teste ou bloqueio"
      );
      return;
    }

    calibrarSensor();
    registrarLog("RECALIBRACAO");

    server.send(200, "text/plain", "Calibrado");
  });

  server.on("/reiniciar", HTTP_POST, []() {
    registrarLog("REINICIO_SOLICITADO");

    server.send(
      200,
      "text/plain",
      "Reiniciando"
    );

    delay(300);
    ESP.restart();
  });

  server.begin();

  iniciarTeste();

  Serial.println("Servidor iniciado!");
}

// ========== LOOP ==========
void loop() {
  server.handleClient();

  unsigned long agora = millis();

  if (
    agora - ultimaLeitura >= INTERVALO_LEITURA
  ) {
    ultimaLeitura = agora;

    atualizarSensor();
  }

  if (estado == EM_TESTE) {
    if (
      agora - ultimoLog >= INTERVALO_LOG
    ) {
      ultimoLog = agora;
      registrarLog("LEITURA");
    }

    verificarTeste();
  }
}
