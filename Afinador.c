// Inclusão de bibliotecas
#include <stdio.h>              // Biblioteca padrão de entrada/saída
#include "pico/stdlib.h"        // Biblioteca padrão do Raspberry Pi Pico
#include "hardware/adc.h"       // Biblioteca do conversor analógico-digital (ADC)
#include "kissfft/kiss_fftr.h"  // Biblioteca para cálculo da FFT
#include "ssd1306/ssd1306.h"    // Biblioteca para controle do display OLED
#include "hardware/i2c.h"

// Definições de hardware
#define ADC_PIN 28       // Pino GPIO28 conectado ao microfone
#define RED_LED 13       // LED vermelho no GPIO13
#define BLUE_LED 12      // LED azul no GPIO12
#define SAMPLE_RATE 8000 // Taxa de amostragem de 8 kHz
#define FFT_SIZE 512     // Tamanho da FFT (potência de 2)

// Configuração do display OLED
#define I2C_PORT i2c1          // Porta I2C utilizada
#define I2C_SDA 14             // Pino SDA (GPIO14)
#define I2C_SCL 15             // Pino SCL (GPIO15)
#define endereco 0x3C          // Endereço I2C do display
#define DISPLAY_WIDTH 128      // Largura do display em pixels
#define DISPLAY_HEIGHT 64      // Altura do display em pixels

// Variáveis globais
ssd1306_t ssd;                // Estrutura de controle do display
bool cor = true;              // Variável para controle de cores do display
const uint limiar_1 = 2080;   // Limiar inferior para ativação do LED
const uint limiar_2 = 2200;   // Limiar superior para ativação do LED

// Função de configuração do ADC
void setup_adc() {
    adc_init();                     // Inicializa o hardware ADC
    adc_gpio_init(ADC_PIN);        // Configura o pino GPIO28 para entrada analógica
    adc_select_input(2);           // Seleciona o canal ADC2 (correspondente ao GPIO28)
}

// Função de inicialização do display OLED
void display_init(){
    i2c_init(I2C_PORT, 400 * 1000); // Inicializa I2C a 400 kHz
    
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Configura pino SDA
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Configura pino SCL
    gpio_pull_up(I2C_SDA);          // Habilita resistor pull-up no SDA
    gpio_pull_up(I2C_SCL);          // Habilita resistor pull-up no SCL
    
    ssd1306_init(&ssd, DISPLAY_WIDTH, DISPLAY_HEIGHT, false, endereco, I2C_PORT); // Inicializa display
    ssd1306_fill(&ssd, false);      // Preenche o display com preto
    ssd1306_send_data(&ssd);        // Envia dados para o display

    // Limpa o display. O display inicia com todos os pixels apagados.
  ssd1306_fill(&ssd, false);
  ssd1306_send_data(&ssd);
}

// Função de configuração dos GPIOs dos LEDs
void gpio_setup(){
    gpio_init(RED_LED);            // Inicializa pino do LED vermelho
    gpio_set_dir(RED_LED, GPIO_OUT); // Configura como saída
    gpio_init(BLUE_LED);           // Inicializa pino do LED azul
    gpio_set_dir(BLUE_LED, GPIO_OUT); // Configura como saída
}

// Função para capturar amostras do ADC
void capture_samples(float *samples) {
    for (int i = 0; i < FFT_SIZE; i++) {
        samples[i] = adc_read();  // Lê valor do ADC
        sleep_us(1000000 / SAMPLE_RATE); // Espera tempo correspondente à taxa de amostragem
    }
}

// Função para encontrar a nota mais próxima da frequência detectada
const char *find_closest_note(float freq) {
    static const char *NOTES[] = {"La", "La sustenido ", "Si", "Do sustenido ", "Do", 
        "Re", "Re sustenido ", "Mi", "Fa", "Fa sustenido ", 
        "Sol", "Sol sustenido "};
    static char note_str[16];
    const float A4 = 440.0f;

    if(freq <= 0) return "N/A";

    // Cálculo do número de semitons a partir de A4
    float semitones = 12.0f * log2f(freq / A4); // precisao de float
    int nearest = (int)round(semitones);

    // Cálculo da oitava (A4 = oitava 4)
    int octave = 4 + (int)floorf((nearest + 9.0f)) / 12.0f; // floorf para divisao inteira
    
    // Ajuste para índice circular (0-11)
    int note_index = nearest % 12;
    if(note_index < 0) note_index += 12;

    // Formatação da string no padrão original
    switch(note_index) {
        case 3:  // Do
        case 4:  // Do#
        case 5:  // Re
        case 6:  // Re#
        case 7:  // Mi
        case 8:  // Fa
        case 9:  // Fa#
        case 10: // Sol
        case 11: // Sol#
            octave += (note_index >= 3 && octave == 4) ? 0 : -1;
            break;
    }

    snprintf(note_str, sizeof(note_str), "%s%d", NOTES[note_index], octave);
    return note_str;
}

// Função principal
int main() {
    stdio_init_all();              // Inicializa todas as funcionalidades I/O
    setup_adc();                   // Configura ADC
    display_init();                // Inicializa display
    gpio_setup();                  // Configura LEDs
    
    // Variáveis para processamento de FFT
    float samples[FFT_SIZE];       // Buffer para armazenar amostras
    kiss_fft_scalar in[FFT_SIZE];  // Buffer de entrada da FFT
    kiss_fft_cpx out[FFT_SIZE / 2 + 1]; // Buffer de saída complexa da FFT
    kiss_fftr_cfg cfg = kiss_fftr_alloc(FFT_SIZE, 0, NULL, NULL); // Configuração da FFT

    // Loop principal
    while (true) {
        capture_samples(samples);  // Captura amostras do microfone
        
        // Pré-processamento dos dados para FFT
        for (int i = 0; i < FFT_SIZE; i++) {
            in[i] = samples[i];    // Copia amostras para buffer de entrada
        }

        kiss_fftr(cfg, in, out);   // Executa a transformada FFT

        // Encontra o pico de magnitude no espectro
        int peak_index = 0;
        float peak_magnitude = 0;
        for (int i = 1; i < FFT_SIZE / 2; i++) {
            float magnitude = sqrt(out[i].r * out[i].r + out[i].i * out[i].i); // Calcula magnitude
            if (magnitude > peak_magnitude) { // Atualiza se encontrar magnitude maior
                peak_magnitude = magnitude;
                peak_index = i;
            }
        }
        
        // Calcula frequência correspondente ao pico
        float freq = peak_index * (SAMPLE_RATE / FFT_SIZE);
        const char *note = find_closest_note(freq); // Identifica nota musical

        // Atualização do display OLED
        ssd1306_fill(&ssd, !cor);  // Limpa o display
        ssd1306_draw_string(&ssd, note, 4, 10); // Exibe nome da nota
        ssd1306_send_data(&ssd); 
        
        // Exibe frequência no display
        char freq_str[16];
        sprintf(freq_str, "Freq %.2f Hz", freq); // Formata string de frequência
        ssd1306_draw_string(&ssd, freq_str, 4, 30); 
        ssd1306_send_data(&ssd); 
        
        printf("Frequência detectada: %.2f Hz -> Nota: %s\n", freq, note); // Log via serial

        sleep_ms(300); // Intervalo entre atualizações

        // Controle dos LEDs baseado na leitura do ADC
        uint adc_val = adc_read(); // Lê valor atual do ADC
        if(adc_val > limiar_1 && adc_val < limiar_2) {
            gpio_put(RED_LED, 1);  // Acende LED vermelho
            gpio_put(BLUE_LED, 0);
        } else if(adc_val > limiar_2) {
            gpio_put(RED_LED, 0);
            gpio_put(BLUE_LED, 1); // Acende LED azul
        } else {
            gpio_put(RED_LED, 0);  // Apaga ambos LEDs
            gpio_put(BLUE_LED, 0);
        }
    }
    
    kiss_fftr_free(cfg); // Libera recursos da FFT (nunca executado devido ao loop infinito)
    return 0;
}
