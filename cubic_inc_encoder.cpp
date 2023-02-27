#include <stdio.h>
#include <iostream>
#include <map>
#include "pico/stdlib.h"
#include "hardware/spi.h"

using namespace std;

// SPIピン設定
#define SPI_PORT spi0
#define PIN_MISO 3
#define PIN_SS 1
#define PIN_SCLK 2
#define PIN_MOSI 0

#define ENC_NUM 8
#define ENC_BYTES 2

#define SPI_FREQ 400000

#define DELAY 1

// ピン番号をキーとするエンコーダ番号の辞書
const map<int, int> Aenc = {
    {23, 0},
    {26, 1},
    {17, 2},
    {20, 3},
    {6,  4},
    {9,  5},
    {12, 6},
    {15, 7}};
const map<int, int> Benc = {
    {22, 0},
    {25, 1},
    {16, 2},
    {19, 3},
    {5,  4},
    {8,  5},
    {11, 6},
    {14, 7}};
// エンコーダのピン設定
const int pinA[ENC_NUM] = {23, 26, 17, 20,  6,  9, 12, 15};
const int pinB[ENC_NUM] = {22, 25, 16, 19,  5,  8, 11, 14};
const int pinZ[ENC_NUM] = {24, 27, 18, 21,  7, 10, 13,  4};
// エンコーダを読んだ生の値
int16_t raw_val[ENC_NUM] = {0, 0, 0, 0, 0, 0, 0, 0};

// 割り込み処理
void callback_readPinA(int num)
{
    if (gpio_get(pinA[num]) == 0)
    {
        if (gpio_get(pinB[num]) == 0)
            --raw_val[num];
        else
            ++raw_val[num];
    }
    else
    {
        if (gpio_get(num) == 0)
            ++raw_val[num];
        else
            --raw_val[num];
    }
}
void callback_readPinB(int num)
{
    if (gpio_get(pinB[num]) == 0)
    {
        if (gpio_get(pinA[num]) == 0)
            ++raw_val[num];
        else
            --raw_val[num];
    }
    else
    {
        if (gpio_get(pinA[num]) == 0)
            --raw_val[num];
        else
            ++raw_val[num];
    }
}

void spi_receive() {
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SS, GPIO_FUNC_SPI);

    spi_write_blocking(SPI_PORT, (uint8_t*)raw_val, ENC_NUM*ENC_BYTES);

    gpio_init(PIN_MISO);
    gpio_set_dir(PIN_MISO, GPIO_IN);
    gpio_init(PIN_SS);
    gpio_set_dir(PIN_SS, GPIO_IN);
    gpio_pull_up(PIN_SS);

    for (int i = 0; i < ENC_NUM; i++)
    {
        raw_val[i] = 0;
    }
}

void c_irq_handler(uint gpio, uint32_t events)
{
    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_FALL, false); // 割り込み処理中は他の割り込み処理は不可

    if (Aenc.find(gpio) != Aenc.end()) // gpioはpinAに設定されているか？
    {
        callback_readPinA(Aenc.at(gpio));
    }
    else if (Benc.find(gpio) != Benc.end()) // gpioはpinBに設定されているか？
    {
        callback_readPinB(Benc.at(gpio));
    }
    else if (gpio == PIN_SS) // gpioはpinSSに設定されているか？
    {
        spi_receive();
    }

    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_FALL, true);
}

void setup_SPI(void)
{   
    /*
        スレーブ動作の際にSSがHIGHのときもMISOがLOWになってしまうSPIライブラリのバグがあるため，
        MISOとSSをGPIOピンとして初期化し，SSの立ち下がりエッジで割り込み処理をしSPI通信を行う
    */
    //gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    //gpio_set_function(PIN_SS, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_MISO);
    gpio_set_dir(PIN_MISO, GPIO_IN);
    gpio_init(PIN_SS);
    gpio_set_dir(PIN_SS, GPIO_IN);
    gpio_pull_up(PIN_SS);

    spi_init(SPI_PORT, SPI_FREQ); // 4MHz
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    // スレーブでSPI通信開始
    spi_set_slave(SPI_PORT, true);

    gpio_set_irq_enabled_with_callback(PIN_SS, GPIO_IRQ_EDGE_FALL, true, c_irq_handler);
}

void setup_enc(int i)
{
    // pinA,pinBを入出力ピンに設定
    gpio_set_function(pinA[i], GPIO_FUNC_SIO);
    gpio_set_function(pinB[i], GPIO_FUNC_SIO);
    // pinA,pinBを入力に設定
    gpio_set_dir(pinA[i], false);
    gpio_set_dir(pinB[i], false);

    /* Encoder Callback*/
    /*
    event_mask
        "LEVEL_LOW",  // 0x1
        "LEVEL_HIGH", // 0x2
        "EDGE_FALL",  // 0x4
        "EDGE_RISE"   // 0x8
    */
    /*
    gpio_set_irq_enabled_with_callback()ではGPIOパラメータが無視され、
    設定したコールバック関数は全てのピンの割込呼出に対して呼び出される
    */
    gpio_set_irq_enabled_with_callback(pinA[i], GPIO_IRQ_EDGE_FALL, true, c_irq_handler);
    gpio_set_irq_enabled(pinB[i], GPIO_IRQ_EDGE_FALL, true);
}

int main()
{
    stdio_init_all();
    for (int i = 0; i < ENC_NUM; i++)
    {   
        setup_enc(i);
    }
    setup_SPI();

    while (1)
    {   
        /*
        for (int i = 0; i < ENC_NUM; i++)
        {
            // usb通信は遅いため，普段はコメントアウト
            //cout << raw_val[i] << ",";
            diff_val[i] = raw_val[i] - old_val[i];
            old_val[i] = raw_val[i];
        }
        //cout << "\n";
        */
 
        sleep_ms(DELAY);
    }

    return 0;
}
