"""Numeric sensor platform for the VEVOR dehumidifier component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

from . import CONF_VEVOR_DEHUMIDIFIER_ID, VevorDehumidifier

DEPENDENCIES = ["vevor_dehumidifier"]

CONF_TARGET_HUMIDITY = "target_humidity"
CONF_CURRENT_HUMIDITY = "current_humidity"
CONF_AMBIENT_TEMPERATURE = "ambient_temperature"
CONF_COIL_TEMPERATURE = "coil_temperature"
CONF_STATUS_WORD = "status_word"
CONF_ERROR_WORD = "error_word"
CONF_BUS_POLLS_PER_MINUTE = "bus_polls_per_minute"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_VEVOR_DEHUMIDIFIER_ID): cv.use_id(VevorDehumidifier),
        cv.Optional(CONF_TARGET_HUMIDITY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_HUMIDITY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
        cv.Optional(CONF_CURRENT_HUMIDITY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_HUMIDITY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
        cv.Optional(CONF_AMBIENT_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
        cv.Optional(CONF_COIL_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_STATUS_WORD): sensor.sensor_schema(
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ERROR_WORD): sensor.sensor_schema(
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BUS_POLLS_PER_MINUTE): sensor.sensor_schema(
            unit_of_measurement="polls/min",
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


_SETTERS = {
    CONF_TARGET_HUMIDITY: "set_target_humidity_sensor",
    CONF_CURRENT_HUMIDITY: "set_current_humidity_sensor",
    CONF_AMBIENT_TEMPERATURE: "set_ambient_temperature_sensor",
    CONF_COIL_TEMPERATURE: "set_coil_temperature_sensor",
    CONF_STATUS_WORD: "set_status_word_sensor",
    CONF_ERROR_WORD: "set_error_word_sensor",
    CONF_BUS_POLLS_PER_MINUTE: "set_bus_polls_per_minute_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_VEVOR_DEHUMIDIFIER_ID])
    for key, setter in _SETTERS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(parent, setter)(sens))
