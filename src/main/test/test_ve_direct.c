#include <string.h>
#include "unity.h"
#include "ve_direct.h"

void test_ve_direct_parse_battery_voltage(void)
{
    ve_direct_data_t data = {0};
    ve_direct_parse_line("V", "12800", &data);
    TEST_ASSERT_EQUAL_INT32(12800, data.battery_voltage_mv);
}

void test_ve_direct_parse_solar_power(void)
{
    ve_direct_data_t data = {0};
    ve_direct_parse_line("PPV", "55", &data);
    TEST_ASSERT_EQUAL_INT32(55, data.panel_power_w);
}

void test_ve_direct_parse_load_output_state(void)
{
    ve_direct_data_t data = {0};
    ve_direct_parse_line("LOAD", "ON", &data);
    TEST_ASSERT_EQUAL_INT32(1, data.load_output_state);

    ve_direct_parse_line("LOAD", "OFF", &data);
    TEST_ASSERT_EQUAL_INT32(0, data.load_output_state);
}

void test_ve_direct_parse_load_current(void)
{
    ve_direct_data_t data = {0};
    ve_direct_parse_line("IL", "1500", &data);
    TEST_ASSERT_EQUAL_INT32(1500, data.load_current);
}

void test_ve_direct_finalize_valid_checksum(void)
{
    ve_direct_data_t data = {0};
    esp_err_t err = ve_direct_finalize_block(&data, 0);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_ve_direct_finalize_invalid_checksum(void)
{
    ve_direct_data_t data = {0};
    esp_err_t err = ve_direct_finalize_block(&data, 42);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, err);
}

void test_ve_direct_feed_valid_block(void)
{
    ve_direct_reset_parser();

    const char *block_prefix = "\r\nPID\t0xA042\r\nV\t12800\r\nI\t1500\r\nChecksum\t";
    uint8_t sum = 0;
    for (size_t i = 0; i < strlen(block_prefix); i++) {
        sum += (uint8_t)block_prefix[i];
    }
    uint8_t valid_checksum_byte = (uint8_t)(256 - (sum % 256));

    bool finished = false;
    for (size_t i = 0; i < strlen(block_prefix); i++) {
        finished = ve_direct_feed_byte((uint8_t)block_prefix[i]);
        TEST_ASSERT_FALSE(finished);
    }

    finished = ve_direct_feed_byte(valid_checksum_byte);
    TEST_ASSERT_TRUE(finished);
}

void test_ve_direct_feed_invalid_checksum(void)
{
    ve_direct_reset_parser();

    const char *block_prefix = "\r\nPID\t0xA042\r\nV\t12800\r\nChecksum\t";
    uint8_t sum = 0;
    for (size_t i = 0; i < strlen(block_prefix); i++) {
        sum += (uint8_t)block_prefix[i];
    }
    // Deliberately wrong checksum
    uint8_t bad_checksum_byte = (uint8_t)(256 - (sum % 256) + 5);

    bool finished = false;
    for (size_t i = 0; i < strlen(block_prefix); i++) {
        finished = ve_direct_feed_byte((uint8_t)block_prefix[i]);
        TEST_ASSERT_FALSE(finished);
    }

    finished = ve_direct_feed_byte(bad_checksum_byte);
    TEST_ASSERT_FALSE(finished);
}

