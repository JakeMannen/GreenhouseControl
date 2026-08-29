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
