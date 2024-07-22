#include "hardware/spi.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <iostream>
#include <map>
#include <stdio.h>
#include <string.h>

using namespace std;

// SPIピン設定
#define SPI_PORT spi0
#define PIN_MISO 3
#define PIN_SS 1
#define PIN_SCLK 2
#define PIN_MOSI 0

#define ENC_NUM 8
#define ENC_BYTES 4

#define SPI_FREQ 4000000

#define DELAY_US 100

bool rotation_dir = true;

// ピン番号をキーとするエンコーダ番号の辞書
const map<int, int> Aenc = {{23, 0}, {26, 1}, {17, 2}, {20, 3},
                            {6, 4},  {9, 5},  {12, 6}, {15, 7}};
const map<int, int> Benc = {{22, 0}, {25, 1}, {16, 2}, {19, 3},
                            {5, 4},  {8, 5},  {11, 6}, {14, 7}};
const map<int, int> Zenc = {{24, 0}, {27, 1}, {18, 2}, {21, 3},
                            {7, 4},  {10, 5}, {13, 6}, {4, 7}};

// エンコーダのピン設定
const int pinA[ENC_NUM] = {23, 26, 17, 20, 6, 9, 12, 15};
const int pinB[ENC_NUM] = {22, 25, 16, 19, 5, 8, 11, 14};
const int pinZ[ENC_NUM] = {24, 27, 18, 21, 7, 10, 13, 4};
// エンコーダを読んだ生の値
int32_t raw_val[ENC_NUM * 2] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// エンコーダーの過去の出力の値, 前半がA相で後半がB相
uint8_t pre_gpio_status[ENC_NUM * 2] = {0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0};

// エンコーダーが正回転したとわかったときの処理
void true_rotation_processing(int num) {
    ++raw_val[num];
    if (!rotation_dir)
        rotation_dir = true;
}
// エンコーダーが負回転したとわかったときの処理
void false_rotation_processing(int num) {
    --raw_val[num];
    if (rotation_dir)
        rotation_dir = false;
}

// エンコーダーの読み取り
void readPinAB(int num) {
    int cur_pinA_status = gpio_get(pinA[num]);
    int cur_pinB_status = gpio_get(pinB[num]);
    if (pre_gpio_status[num] == 1 && cur_pinA_status == 0) {
        if (cur_pinB_status == 0) {
            false_rotation_processing(num); // Aが立ち下がった時にBが0
        } else {
            true_rotation_processing(num); // Aが立ち下がった時にBが1
        }
        pre_gpio_status[num] = 0;
    } else if (pre_gpio_status[num + 8] == 1 && cur_pinB_status == 0) {
        if (cur_pinA_status == 0) {
            true_rotation_processing(num); // Bが立ち下がった時にAが0
        } else {
            false_rotation_processing(num); // Bが立ち下がった時にAが1
        }
        pre_gpio_status[num + 8] = 0;
    } else if (pre_gpio_status[num] == 0 && cur_pinA_status == 1) {
        if (cur_pinB_status == 0) {
            true_rotation_processing(num); // Aが立ち上がった時にBが0
        } else {
            false_rotation_processing(num); // Aが立ち上がった時にBが1
        }
        pre_gpio_status[num] = 1;
    } else if (pre_gpio_status[num + 8] == 0 && cur_pinB_status == 1) {
        if (cur_pinA_status == 0) {
            false_rotation_processing(num); // Bが立ち上がった時にAが0
        } else {
            true_rotation_processing(num); // Bが立ち上がった時にAが1
        }
        pre_gpio_status[num + 8] = 1;
    }
}

// Z相割り込み処理
void callback_readPinZ(int num) {
    if (rotation_dir)
        ++raw_val[num + 8];
    if (!rotation_dir)
        --raw_val[num + 8];
}

void c_irq_handler(uint gpio, uint32_t events) {
    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_FALL,
                         false); // 割り込み処理中は他の割り込み処理は不可
    if (Zenc.find(gpio) != Zenc.end()) // gpioはpinZに設定されているか？
    {
        callback_readPinZ(Zenc.at(gpio));
    }
    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_FALL, true);
}

void setup_SPI(void) {
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SS, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    spi_init(SPI_PORT, SPI_FREQ); // 4MHz
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    // スレーブでSPI通信開始
    spi_set_slave(SPI_PORT, true);
}

void setup_enc(int i) {
    // pinA,pinBを入出力ピンに設定
    gpio_set_function(pinA[i], GPIO_FUNC_SIO);
    gpio_set_function(pinB[i], GPIO_FUNC_SIO);
    gpio_set_function(pinZ[i], GPIO_FUNC_SIO);
    // pinA,pinBを入力に設定
    gpio_set_dir(pinA[i], false);
    gpio_set_dir(pinB[i], false);
    gpio_set_dir(pinZ[i], false);
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
    gpio_set_irq_enabled_with_callback(pinZ[i], GPIO_IRQ_EDGE_FALL, true,
                                       c_irq_handler);
}

// サブコアの処理、エンコーダの値を読み取る
void core1_main() {
    for (int i = 0; i < ENC_NUM; i++) {
        setup_enc(i);
    }
    while (1) {
        for (int i = 0; i < ENC_NUM; i++) {
            readPinAB(i);
        }
    }
}

// メインコアの処理、SPI通信によりマスターにデータを送信
void core0_main() {
    setup_SPI();
    while (1) {
        spi_write_blocking(SPI_PORT, (uint8_t *)raw_val,
                           ENC_NUM * ENC_BYTES * 2);
        // cout << raw_val[0] << "," << raw_val[8] << endl;

        /*
        for (int i = 0; i < ENC_NUM; i++)
        {
            // usb通信は遅いため，普段はコメントアウト
            // cout << raw_val[i] << ",";
            // raw_val[i] = 0;
        }
        // cout << "\n";
        */

        sleep_us(DELAY_US);
    }
}

int main() {

    stdio_init_all();
    multicore_launch_core1(core1_main);
    core0_main();
    return 0;
}
