---
name: esp-docs
description: Search and retrieve official Espressif ESP-IDF and esp_zigbee_lib documentation using the espressif-docs MCP server. Use when looking up APIs, hardware peripherals, or migration guides.
---

# Espressif Documentation Lookup Skill

Use this skill when needing technical references, API signatures, migration guides, or hardware errata from official Espressif sources.

## Using the `espressif-docs` MCP Server

When specific ESP-IDF API details or Zigbee SDK usage is needed, query the `search_espressif_sources` tool from the `espressif-docs` MCP server:

- **Target Query**: Search for specific terms such as `esp_zb_zcl_attr_update_value`, `esp_zb_cluster_add_attr`, `uart_driver_install`, `i2c_master_bus_config_t`.
- **Version Awareness**: This project uses **ESP-IDF $\ge$ v6.1** and **esp_zigbee_lib $\ge$ v2.0.1**. Ensure APIs referenced adhere to the v2.x architecture.
