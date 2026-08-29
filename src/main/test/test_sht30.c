#include "unity.h"
#include "sht30_sensor.h"

void test_sht30_crc8_valid(void)
{
    // Real SHT30 measurement block example:
    // Temp MSB, Temp LSB, Temp CRC
    uint8_t temp_data[2] = {0x65, 0x08}; 
    // Known CRC for 0x65 0x08 is 0x7E (example, assuming standard SHT30)
    // Wait, let's use a known CRC calculation
    uint8_t crc = sht30_crc8(temp_data, 2);
    // We just verify it's deterministic and matches what sht30_crc8 calculates for known data.
    // For 0xbeef, CRC-8/SENSIRION is 0x92
    uint8_t test_beef[2] = {0xbe, 0xef};
    TEST_ASSERT_EQUAL_UINT8(0x92, sht30_crc8(test_beef, 2));
}

void test_sht30_crc8_invalid(void)
{
    uint8_t test_beef[2] = {0xbe, 0xef};
    TEST_ASSERT_NOT_EQUAL(0x00, sht30_crc8(test_beef, 2));
}
