const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');

const e = exposes.presets;
const ea = exposes.access;

const definition = {
    // Matches the Model ID we configured in the Basic Cluster on Endpoint 1
    zigbeeModel: ['GreenHouse Controller'],
    model: 'GreenhouseController',
    vendor: 'Espressif',
    description: 'Solar Powered Greenhouse Irrigation & Environmental Monitor',
    ota: true,
    
    // Convert incoming Zigbee reports to Home Assistant states
    fromZigbee: [
        {
            cluster: 'genOnOff',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                if (msg.data.hasOwnProperty('onOff')) {
                    if (msg.endpoint && msg.endpoint.ID === 4) {
                        return { load_state: msg.data['onOff'] === 1 ? 'ON' : 'OFF' };
                    }
                    return { pump_1: msg.data['onOff'] === 1 ? 'ON' : 'OFF' };
                }
            },
        },
        fz.temperature,
        fz.humidity,
        {
            cluster: 'genPowerCfg',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                const result = {};
                // Battery Voltage (0x0020 = 32)
                if (msg.data.hasOwnProperty('batteryVoltage') || msg.data.hasOwnProperty(32)) {
                    let val = msg.data['batteryVoltage'] !== undefined ? msg.data['batteryVoltage'] : msg.data[32];
                    result.battery_voltage = val / 10.0;
                }
                // Battery Percentage (0x0021 = 33)
                if (msg.data.hasOwnProperty('batteryPercentageRemaining') || msg.data.hasOwnProperty(33)) {
                    let val = msg.data['batteryPercentageRemaining'] !== undefined ? msg.data['batteryPercentageRemaining'] : msg.data[33];
                    if (val !== 255) {
                        result.battery = val / 2.0;
                    }
                }
                return Object.keys(result).length > 0 ? result : undefined;
            },
        },
        {
            cluster: 'haElectricalMeasurement',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                const result = {};
                if (msg.endpoint && msg.endpoint.ID === 4) {
                    if (msg.data.hasOwnProperty('rmsCurrent') || msg.data.hasOwnProperty(1288)) {
                        let val = msg.data['rmsCurrent'] !== undefined ? msg.data['rmsCurrent'] : msg.data[1288];
                        result.load_current = val / 100.0;
                    }
                    return Object.keys(result).length > 0 ? result : undefined;
                }

                // Solar Endpoint (Endpoint 3)
                // We switched the firmware to use RMS Voltage (1285), RMS Current (1288), and Active Power (1291)
                if (msg.data.hasOwnProperty('rmsVoltage') || msg.data.hasOwnProperty(1285)) {
                    let val = msg.data['rmsVoltage'] !== undefined ? msg.data['rmsVoltage'] : msg.data[1285];
                    result.solar_voltage = val / 100.0;
                }
                if (msg.data.hasOwnProperty('rmsCurrent') || msg.data.hasOwnProperty(1288)) {
                    let val = msg.data['rmsCurrent'] !== undefined ? msg.data['rmsCurrent'] : msg.data[1288];
                    result.solar_current = val / 100.0;
                }
                if (msg.data.hasOwnProperty('activePower') || msg.data.hasOwnProperty(1291)) {
                    let val = msg.data['activePower'] !== undefined ? msg.data['activePower'] : msg.data[1291];
                    result.solar_power = val / 10.0;
                }
                return Object.keys(result).length > 0 ? result : undefined;
            },
        },
        {
            cluster: 'genOnOffSwitchCfg',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                if (msg.data.hasOwnProperty('switchActions')) {
                    return { switch_mode: msg.data['switchActions'] === 0 ? 'hold' : 'press' };
                }
                if (msg.data.hasOwnProperty('switchType')) {
                    return { switch_mode: msg.data['switchType'] === 1 ? 'hold' : 'press' };
                }
            },
        }
    ],
    
    configure: async (device, coordinatorEndpoint) => {
        const endpoint_pump = device.getEndpoint(1);
        const endpoint_climate = device.getEndpoint(2);
        const endpoint_solar = device.getEndpoint(3);
        const endpoint_load = device.getEndpoint(4);
        
        // Bind all clusters so the coordinator receives the reports
        await endpoint_pump.bind('genOnOff', coordinatorEndpoint);
        await endpoint_pump.bind('genOnOffSwitchCfg', coordinatorEndpoint);
        await endpoint_pump.bind('genPowerCfg', coordinatorEndpoint);
        await endpoint_climate.bind('msTemperatureMeasurement', coordinatorEndpoint);
        await endpoint_climate.bind('msRelativeHumidity', coordinatorEndpoint);
        await endpoint_solar.bind('haElectricalMeasurement', coordinatorEndpoint);
        if (endpoint_load) {
            await endpoint_load.bind('genOnOff', coordinatorEndpoint);
            await endpoint_load.bind('haElectricalMeasurement', coordinatorEndpoint);
        }

        // Read switch mode configuration
        try {
            await endpoint_pump.read('genOnOffSwitchCfg', ['switchActions', 'switchType']);
        } catch (e) {
            console.error(`Failed to read switch configuration: ${e}`);
        }

        // Configure reporting for the battery cluster
        try {
            await endpoint_pump.configureReporting('genPowerCfg', [
                {attribute: 'batteryVoltage', minimumReportInterval: 10, maximumReportInterval: 3600, reportableChange: 1},
                {attribute: 'batteryPercentageRemaining', minimumReportInterval: 10, maximumReportInterval: 3600, reportableChange: 1}
            ]);
        } catch (e) {
            console.error(`Failed to configure battery reporting: ${e}`);
        }

        // Configure reporting for climate (temperature & humidity) clusters
        try {
            await endpoint_climate.configureReporting('msTemperatureMeasurement', [
                {attribute: 'measuredValue', minimumReportInterval: 10, maximumReportInterval: 3600, reportableChange: 50} // 0.50 °C
            ]);
            await endpoint_climate.configureReporting('msRelativeHumidity', [
                {attribute: 'measuredValue', minimumReportInterval: 10, maximumReportInterval: 3600, reportableChange: 100} // 1.00 %
            ]);
        } catch (e) {
            console.error(`Failed to configure climate reporting: ${e}`);
        }

        // Read current OTA file version
        try {
            await endpoint_pump.read('genOta', ['currentFileVersion'], { direction: 1 });
        } catch (e) {
            console.error(`Failed to read currentFileVersion: ${e}`);
        }
    },
    
    // Convert Home Assistant commands to outgoing Zigbee packets
    toZigbee: [
        {
            key: ['pump_1'],
            convertSet: async (entity, key, value, meta) => {
                const state = value.toLowerCase() === 'on' ? 'on' : 'off';
                await entity.command('genOnOff', state, {}, meta.options);
                return { state: { pump_1: value.toUpperCase() } };
            },
            convertGet: async (entity, key, meta) => {
                await entity.read('genOnOff', ['onOff']);
            },
        },
        {
            key: ['switch_mode'],
            convertSet: async (entity, key, value, meta) => {
                const isHold = value.toLowerCase() === 'hold';
                const switchActions = isHold ? 0 : 2;
                await entity.write('genOnOffSwitchCfg', { switchActions: switchActions }, meta.options);
                return { state: { switch_mode: isHold ? 'hold' : 'press' } };
            },
            convertGet: async (entity, key, meta) => {
                await entity.read('genOnOffSwitchCfg', ['switchActions', 'switchType']);
            },
        }
    ],
    
    // Expose entities to Home Assistant
    exposes: [
        exposes.binary('pump_1', ea.ALL, 'ON', 'OFF').withValueToggle('TOGGLE').withDescription('Pump 1'),
        exposes.enum('switch_mode', ea.ALL, ['press', 'hold']).withDescription('External pump switch mode: press to run for set duration or hold to run'),
        exposes.binary('load_state', ea.STATE, 'ON', 'OFF').withDescription('Load Output State'),
        e.temperature().withDescription('Temperature'),
        e.humidity().withDescription('Humidity'),
        exposes.numeric('battery', ea.STATE).withUnit('%').withDescription('Battery Percentage'),
        exposes.numeric('battery_voltage', ea.STATE).withUnit('V').withDescription('Battery Voltage'),
        exposes.numeric('solar_power', ea.STATE).withUnit('W').withDescription('Solar Power'),
        exposes.numeric('solar_voltage', ea.STATE).withUnit('V').withDescription('Solar Voltage'),
        exposes.numeric('solar_current', ea.STATE).withUnit('A').withDescription('Solar Current'),
        exposes.numeric('load_current', ea.STATE).withUnit('A').withDescription('Load Current')
    ],
};

module.exports = definition;
