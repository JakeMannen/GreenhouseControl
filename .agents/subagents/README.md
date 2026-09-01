# Subagent Roles & Definitions

In Antigravity 2.0, subagents are spawned concurrently in the background to handle specialized, parallel, or deep-dive tasks.

## Runtime Registration
Subagents can be registered dynamically during conversations using the `define_subagent` tool or by referencing the role templates below.

## Available Role Templates

| Role File | Name | Scope & Responsibilities |
| :--- | :--- | :--- |
| [`firmware-architect.md`](./firmware-architect.md) | `firmware_architect` | FreeRTOS task concurrency, stack budgets, memory management, ESP32-C6 SoC configuration. |
| [`zigbee-specialist.md`](./zigbee-specialist.md) | `zigbee_specialist` | Zigbee 3.0 cluster modeling, endpoint mapping, attribute reporting, and Zigbee2MQTT sync. |
| [`sensor-driver-engineer.md`](./sensor-driver-engineer.md) | `sensor_driver_engineer` | SHT30 I2C sensor, Victron VE.Direct UART parser, GPIO relay control, and CRC error recovery. |
| [`qa-test-engineer.md`](./qa-test-engineer.md) | `qa_test_engineer` | Unity unit test suites, build integrity checks, edge case testing, and mocking. |

## How to Invoke
When complex work requires a specialist:
1. Define the subagent if not already defined:
   ```json
   {
     "name": "zigbee_specialist",
     "description": "Specialist in Zigbee 3.0 clusters and Zigbee2MQTT converters",
     "system_prompt": "... (loaded from role file)",
     "enable_write_tools": true,
     "enable_mcp_tools": true
   }
   ```
2. Spawn using `invoke_subagent`:
   ```json
   {
     "Subagents": [
       {
         "TypeName": "zigbee_specialist",
         "Role": "Zigbee Cluster Specialist",
         "Prompt": "Add Load Switch cluster to Endpoint 4 and synchronize greenhouse_controller.js"
       }
     ]
   }
   ```
