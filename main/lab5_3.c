/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include <string.h>

const static char *TAG = "EXAMPLE";

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
//ADC1 Channels
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_0

#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12

static int adc_raw[2][10];
static int voltage[2][10];
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);

// Morse code dictionary
char* morse_code_dict[36][2] = {
    {".-", "A"}, {"-...", "B"}, {"-.-.", "C"}, {"-..", "D"}, {".", "E"},
    {"..-.", "F"}, {"--.", "G"}, {"....", "H"}, {"..", "I"}, {".---", "J"},
    {"-.-", "K"}, {".-..", "L"}, {"--", "M"}, {"-.", "N"}, {"---", "O"},
    {".--.", "P"}, {"--.-", "Q"}, {".-.", "R"}, {"...", "S"}, {"-", "T"},
    {"..-", "U"}, {"...-", "V"}, {".--", "W"}, {"-..-", "X"}, {"-.--", "Y"},
    {"--..", "Z"}, {"-----", "0"}, {".----", "1"}, {"..---", "2"}, {"...--", "3"},
    {"....-", "4"}, {".....", "5"}, {"-....", "6"}, {"--...", "7"}, {"---..", "8"},
    {"----.", "9"}
};

// Function to convert morse code to text
void morse_to_text(const char *morse_code, char *text) {
    char morse_char[10];
    int morse_index = 0;
    int text_index = 0;
    int i = 0;
    int len = strlen(morse_code);

    while (i <= len) {
        if (morse_code[i] == ' ' || morse_code[i] == '\0' || morse_code[i] == '/') {
            morse_char[morse_index] = '\0'; // Null-terminate the morse_char string
            for (int j = 0; j < 36; j++) {
                if (strcmp(morse_char, morse_code_dict[j][0]) == 0) {
                    text[text_index++] = morse_code_dict[j][1][0];
                    break;
                }
            }
            morse_index = 0; // Reset morse_char index for the next character
            if (morse_code[i] == '/') {
                text[text_index++] = ' '; // Add space for word separation
                i++; // Skip the '/' character
            }
        } else {
            morse_char[morse_index++] = morse_code[i]; // Add to current morse character
        }
        i++;
    }
    text[text_index] = '\0'; // Null-terminate the text string
}

int decipher_msg(char *msg, int msg_len) {
    // hyperparameters
    int dot_threshold = 1;
    int dash_threshold = 3;
    int inter_char_threshold = 3;
    int inter_word_threshold = 7;
    // end hyperparameters

    int high_count = 0;
    int low_count = 0;
    char text[500] = "";     // holds morse code translated from binary data
    int text_len = 0;

    for (int i = 0; i < msg_len; i++) {
        int tick = msg[i] - 48;  // 48 represents char '0'

        if (tick == 1) {
            high_count++;
            if (low_count >= inter_word_threshold) {
                strcat(text, " / ");
                text_len += 3;
            } else if (low_count >= inter_char_threshold) {
                strcat(text, " ");
                text_len++;
            }
            low_count = 0;
        } else {
            low_count++;
            if (high_count >= dash_threshold) {
                strcat(text, "-");
                text_len++;
            } else if (high_count >= dot_threshold) {
                strcat(text, ".");
                text_len++;
            }
            high_count = 0;
        }
        
    }
    text[text_len] = '\0';
    //printf("text: %s\n", text);     // prints morse code conversion from raw data

    char text_deciphered[500] = "";
    morse_to_text(text, text_deciphered);

    if (strlen(text_deciphered) != 0) {
        printf("%s\n", text_deciphered);      // prints deciphered message
        return 0;
    }
    return 1;
}

void app_main(void)
{
    //-------------ADC1 Init---------------//
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = EXAMPLE_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));

    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;
    bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN, &adc1_cali_chan0_handle);

    // hyperparameters
    int threshold = 600;
    int sampling_frequency = 10;
    // end hyperparameters

    bool msg_active = false;
    int idle = 0;

    int msg_len = 0;
    char msg[2000] = "";
    while (1) {
        int V = voltage[0][0];
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw[0][0]));
        //ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw[0][0]);
        if (do_calibration1_chan0) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw[0][0], &V));
            //ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, V);
        }

        if (V > threshold) {
            msg_active = true;
            idle = 0;
        } else if (idle == (30)) {
            msg_active = false;
        } else {
            idle++;
        }
        //ESP_LOGI(TAG, "msg_active: %d", msg_active);

        if (msg_active == true) {
            if (V > threshold) {
                strcat(msg, "1");
            } else {
                strcat(msg, "0");
            }
            msg_len++;
        } else {
            msg[msg_len] = '\0';

            //printf("msg_len: %d\n", msg_len);
            decipher_msg(msg, msg_len);
            // if (ret == 0) {
            //     printf("%s\n", msg);      // prints raw binary buffer
            // }
            msg_len = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(sampling_frequency));
    }

    //Tear Down
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
    if (do_calibration1_chan0) {
        example_adc_calibration_deinit(adc1_cali_chan0_handle);
    }
}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}
