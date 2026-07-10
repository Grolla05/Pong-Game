/*
 * Pong
 * ----
 * Jogo clássico de Pong para 2 jogadores na OLED 128x64. Cada jogador
 * controla sua raquete com um potenciômetro. A bola reflete nas paredes
 * e nas raquetes com ângulo variável conforme o ponto de impacto, e
 * acelera um pouco a cada rebatida. Buzzer sinaliza rebatidas e pontos.
 * Placar fica na faixa amarela do display, campo de jogo na faixa azul.
 * Ao ligar, mostra uma tela de loading com barra de progresso antes do
 * primeiro saque.
 *
 * Pinout:
 *   POT Jogador 1 (raquete esquerda) -> A0
 *   POT Jogador 2 (raquete direita)  -> A1
 *   Buzzer                            -> D8
 *   OLED SDA                          -> A4 (I2C)
 *   OLED SCL                          -> A5 (I2C)
 *
 * Bibliotecas:
 *   Adafruit SSD1306
 *   Adafruit GFX
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Pinout ----------
#define PIN_POT_P1  A0
#define PIN_POT_P2  A1
#define PIN_BUZZER  8

// ---------- OLED ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- Layout (faixa amarela = placar, faixa azul = campo) ----------
const uint8_t HEADER_DIVIDER_Y = 15;
const uint8_t CAMPO_Y0         = 16;
const uint8_t CAMPO_Y1         = SCREEN_HEIGHT - 1; // 63

// ---------- Raquetes ----------
const uint8_t PADDLE_WIDTH  = 3;
const uint8_t PADDLE_HEIGHT = 14;
const uint8_t PADDLE_MARGIN = 4;
const uint8_t PADDLE_X_P1   = PADDLE_MARGIN;
const uint8_t PADDLE_X_P2   = SCREEN_WIDTH - PADDLE_MARGIN - PADDLE_WIDTH;
const uint8_t PADDLE_Y_MIN  = CAMPO_Y0;
const uint8_t PADDLE_Y_MAX  = CAMPO_Y1 - PADDLE_HEIGHT;

float paddleP1Y = (PADDLE_Y_MIN + PADDLE_Y_MAX) / 2.0;
float paddleP2Y = (PADDLE_Y_MIN + PADDLE_Y_MAX) / 2.0;
const float PADDLE_SUAVIZACAO = 0.35; // filtro pra suavizar a leitura do potenciômetro

// ---------- Bola ----------
const uint8_t BALL_SIZE              = 3;
const float   BALL_SPEED_INICIAL     = 1.6;
const float   BALL_SPEED_INCREMENTO  = 0.18;
const float   BALL_SPEED_MAX         = 4.2;
const float   BALL_VY_MAX            = 2.6; // limite de inclinação vertical no rebote

float ballX, ballY;
float ballVX, ballVY;
float ballSpeedAtual = BALL_SPEED_INICIAL;

// ---------- Placar ----------
uint8_t placarP1 = 0;
uint8_t placarP2 = 0;

// ---------- Estado de saque ----------
bool aguardandoSaque = true;
unsigned long saqueEm = 0;
const unsigned long ATRASO_SAQUE_MS = 900;

// ---------- Timing do jogo (não-bloqueante) ----------
unsigned long proximoFrame = 0;
const unsigned long FRAME_MS = 30;

// ---------- Tela de loading (ao ligar) ----------
const unsigned long LOADING_DURACAO_MS = 1800; // tempo total até a barra encher 100%
const unsigned long LOADING_SEGURAR_MS = 400;  // pausa mostrando 100% antes de iniciar o jogo
const uint8_t BARRA_LARGURA = 100;
const uint8_t BARRA_ALTURA  = 10;
const uint8_t BARRA_X       = (SCREEN_WIDTH - BARRA_LARGURA) / 2;
const uint8_t BARRA_Y       = 40;

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    // Trava aqui se a OLED não inicializar — sinal de erro de fiação/endereço I2C
    for (;;) {}
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  randomSeed(analogRead(A2)); // pino analógico flutuante (não usado) garante uma seed variável

  exibirTelaLoading();
  prepararSaque(+1);
}

void loop() {
  unsigned long agora = millis();
  if (agora < proximoFrame) return;
  proximoFrame = agora + FRAME_MS;

  lerRaquetes();

  if (aguardandoSaque) {
    if (agora >= saqueEm) {
      aguardandoSaque = false;
    }
  } else {
    atualizarBola();
  }

  desenharTela();
}

// ---------- Leitura das raquetes ----------
void lerRaquetes() {
  int leituraP1 = analogRead(PIN_POT_P1);
  int leituraP2 = analogRead(PIN_POT_P2);

  float alvoP1 = map(leituraP1, 0, 1023, PADDLE_Y_MIN, PADDLE_Y_MAX);
  float alvoP2 = map(leituraP2, 0, 1023, PADDLE_Y_MIN, PADDLE_Y_MAX);

  paddleP1Y += (alvoP1 - paddleP1Y) * PADDLE_SUAVIZACAO;
  paddleP2Y += (alvoP2 - paddleP2Y) * PADDLE_SUAVIZACAO;
}

// ---------- Física da bola ----------
void atualizarBola() {
  ballX += ballVX;
  ballY += ballVY;

  // Colisão com teto/chão do campo (faixa azul)
  if (ballY <= CAMPO_Y0) {
    ballY = CAMPO_Y0;
    ballVY = -ballVY;
    tocarBeep(800, 15);
  } else if (ballY + BALL_SIZE >= CAMPO_Y1) {
    ballY = CAMPO_Y1 - BALL_SIZE;
    ballVY = -ballVY;
    tocarBeep(800, 15);
  }

  // Colisão com a raquete do Jogador 1 (esquerda)
  if (ballVX < 0 &&
      ballX <= PADDLE_X_P1 + PADDLE_WIDTH &&
      ballX + BALL_SIZE >= PADDLE_X_P1 &&
      ballY + BALL_SIZE >= paddleP1Y &&
      ballY <= paddleP1Y + PADDLE_HEIGHT) {
    rebaterNaRaquete(PADDLE_X_P1 + PADDLE_WIDTH, paddleP1Y, +1);
  }

  // Colisão com a raquete do Jogador 2 (direita)
  if (ballVX > 0 &&
      ballX + BALL_SIZE >= PADDLE_X_P2 &&
      ballX <= PADDLE_X_P2 + PADDLE_WIDTH &&
      ballY + BALL_SIZE >= paddleP2Y &&
      ballY <= paddleP2Y + PADDLE_HEIGHT) {
    rebaterNaRaquete(PADDLE_X_P2 - BALL_SIZE, paddleP2Y, -1);
  }

  // Ponto: bola saiu pela esquerda ou pela direita da tela
  if (ballX + BALL_SIZE < 0) {
    placarP2++;
    tocarBeep(300, 250);
    prepararSaque(-1); // saque continua na mesma direção que a bola saiu (P1 recebe)
  } else if (ballX > SCREEN_WIDTH) {
    placarP1++;
    tocarBeep(300, 250);
    prepararSaque(+1); // saque continua na mesma direção (P2 recebe)
  }
}

// Aplica o rebote com ângulo variável conforme o ponto de impacto na raquete,
// e acelera levemente a bola a cada rebatida (até o limite BALL_SPEED_MAX)
void rebaterNaRaquete(float novoX, float paddleY, int8_t sentido) {
  ballX = novoX;

  float centroRaquete = paddleY + (PADDLE_HEIGHT / 2.0);
  float centroBola = ballY + (BALL_SIZE / 2.0);
  float intersecaoRelativa = (centroBola - centroRaquete) / (PADDLE_HEIGHT / 2.0);
  intersecaoRelativa = constrain(intersecaoRelativa, -1.0, 1.0);

  ballSpeedAtual = min(ballSpeedAtual + BALL_SPEED_INCREMENTO, BALL_SPEED_MAX);
  ballVX = sentido * ballSpeedAtual;
  ballVY = intersecaoRelativa * BALL_VY_MAX;

  tocarBeep(1200, 20);
}

// ---------- Saque ----------
void prepararSaque(int8_t direcao) {
  ballSpeedAtual = BALL_SPEED_INICIAL;
  ballX = (SCREEN_WIDTH - BALL_SIZE) / 2.0;
  ballY = (CAMPO_Y0 + CAMPO_Y1) / 2.0;
  ballVX = direcao * ballSpeedAtual;
  ballVY = (random(-100, 101) / 100.0) * (BALL_VY_MAX * 0.5);

  aguardandoSaque = true;
  saqueEm = millis() + ATRASO_SAQUE_MS;
}

// ---------- Som ----------
void tocarBeep(unsigned int frequencia, unsigned int duracaoMs) {
  tone(PIN_BUZZER, frequencia, duracaoMs); // tone() com duração não bloqueia o loop
}

// ---------- Tela de loading (ao ligar) ----------
// Roda uma única vez no setup(): mostra "PONG" + barra de progresso enchendo
// de 0% a 100% ao longo de LOADING_DURACAO_MS, com um pequeno bip a cada
// 25% e um bip final mais longo quando termina.
void exibirTelaLoading() {
  unsigned long inicio = millis();
  uint8_t progressoAnterior = 255; // valor inválido pra forçar o primeiro desenho/beep

  uint8_t progresso = 0;
  while (progresso < 100) {
    unsigned long decorrido = millis() - inicio;
    progresso = (uint8_t)constrain(map(decorrido, 0, LOADING_DURACAO_MS, 0, 100), 0, 100);

    if (progresso != progressoAnterior) {
      desenharTelaLoading(progresso);
      if (progresso > 0 && progresso % 25 == 0) {
        tocarBeep(1000, 25);
      }
      progressoAnterior = progresso;
    }
  }

  tocarBeep(1500, 120); // bip de "pronto"

  // Segura a barra em 100% por um instante antes de iniciar o jogo
  unsigned long seguraAte = millis() + LOADING_SEGURAR_MS;
  while (millis() < seguraAte) {
    // aguardando, sem delay() — só passando o tempo até a barra sumir
  }
}

void desenharTelaLoading(uint8_t progresso) {
  display.clearDisplay();

  // "PONG" ocupa só a faixa amarela (y=0-15): size2 tem 16px de altura,
  // então cursor em y=0 preenche exatamente 0-15, sem invadir a faixa azul.
  display.setTextSize(2);
  const char *titulo = "PONG";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 0);
  display.print(titulo);

  // Linha divisória no mesmo y do header do jogo (HEADER_DIVIDER_Y),
  // mantendo a mesma convenção visual: header amarelo / conteúdo azul.
  display.drawFastHLine(0, HEADER_DIVIDER_Y, SCREEN_WIDTH, SSD1306_WHITE);

  // Barra de progresso e percentual ficam na faixa azul (y>=CAMPO_Y0),
  // igual ao campo de jogo do resto do minigame.
  display.drawRect(BARRA_X, BARRA_Y, BARRA_LARGURA, BARRA_ALTURA, SSD1306_WHITE);
  uint8_t larguraPreenchida = map(progresso, 0, 100, 0, BARRA_LARGURA - 4);
  display.fillRect(BARRA_X + 2, BARRA_Y + 2, larguraPreenchida, BARRA_ALTURA - 4, SSD1306_WHITE);

  display.setTextSize(1);
  char textoPct[6];
  sprintf(textoPct, "%d%%", progresso);
  display.getTextBounds(textoPct, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, BARRA_Y + BARRA_ALTURA + 6);
  display.print(textoPct);

  display.display();
}

// ---------- Desenho ----------
void desenharTela() {
  display.clearDisplay();

  desenharPlacar();
  display.drawFastHLine(0, HEADER_DIVIDER_Y, SCREEN_WIDTH, SSD1306_WHITE);

  display.fillRect(PADDLE_X_P1, (int16_t)round(paddleP1Y), PADDLE_WIDTH, PADDLE_HEIGHT, SSD1306_WHITE);
  display.fillRect(PADDLE_X_P2, (int16_t)round(paddleP2Y), PADDLE_WIDTH, PADDLE_HEIGHT, SSD1306_WHITE);

  // Linha central pontilhada (referência visual do campo)
  for (uint8_t y = CAMPO_Y0; y < CAMPO_Y1; y += 6) {
    display.drawFastVLine(SCREEN_WIDTH / 2, y, 3, SSD1306_WHITE);
  }

  if (aguardandoSaque) {
    desenharAguardandoSaque();
  } else {
    display.fillRect((int16_t)round(ballX), (int16_t)round(ballY), BALL_SIZE, BALL_SIZE, SSD1306_WHITE);
  }

  display.display();
}

void desenharPlacar() {
  display.setTextSize(1);

  char textoP1[8];
  char textoP2[8];
  sprintf(textoP1, "P1:%d", placarP1);
  sprintf(textoP2, "P2:%d", placarP2);

  display.setCursor(4, 4);
  display.print(textoP1);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(textoP2, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w - 4, 4);
  display.print(textoP2);
}

void desenharAguardandoSaque() {
  display.setTextSize(1);
  const char *msg = "SAQUE...";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, (CAMPO_Y0 + CAMPO_Y1) / 2 - h / 2);
  display.print(msg);
}
