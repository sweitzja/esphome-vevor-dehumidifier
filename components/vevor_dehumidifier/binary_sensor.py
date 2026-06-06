"""Binary sensor platform for the VEVOR dehumidifier component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_MOISTURE,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_VEVOR_DEHUMIDIFIER_ID, VevorDehumidifier

DEPENDENCIES = ["vevor_dehumidifier"]

CONF_POWER = "power"
CONF_COMPRESSOR_RUNNING = "compressor_running"
CONF_COMPRESSOR_ALLOWED = "compressor_allowed"
CONF_CALLING_FOR_COOLING = "calling_for_cooling"
CONF_LOCKOUT_SAFETY = "lockout_safety"
CONF_FLOOD = "flood"
CONF_ALARM_LATCHED = "alarm_latched"
CONF_HUM_SENSOR_FAULT = "hum_sensor_fault"
CONF_COIL_SENSOR_FAULT = "coil_sensor_fault"
CONF_PANEL_PRESENT = "panel_present"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_VEVOR_DEHUMIDIFIER_ID): cv.use_id(VevorDehumidifier),
        cv.Optional(CONF_POWER): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_POWER,
        ),
        cv.Optional(CONF_COMPRESSOR_RUNNING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING,
        ),
        cv.Optional(CONF_COMPRESSOR_ALLOWED): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_CALLING_FOR_COOLING): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_LOCKOUT_SAFETY): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
        ),
        cv.Optional(CONF_FLOOD): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_MOISTURE,
        ),
        cv.Optional(CONF_ALARM_LATCHED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
        ),
        cv.Optional(CONF_HUM_SENSOR_FAULT): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_COIL_SENSOR_FAULT): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_PANEL_PRESENT): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


_SETTERS = {
    CONF_POWER: "set_power_bs",
    CONF_COMPRESSOR_RUNNING: "set_compressor_running_bs",
    CONF_COMPRESSOR_ALLOWED: "set_compressor_allowed_bs",
    CONF_CALLING_FOR_COOLING: "set_calling_for_cooling_bs",
    CONF_LOCKOUT_SAFETY: "set_lockout_safety_bs",
    CONF_FLOOD: "set_flood_bs",
    CONF_ALARM_LATCHED: "set_alarm_latched_bs",
    CONF_HUM_SENSOR_FAULT: "set_hum_sensor_fault_bs",
    CONF_COIL_SENSOR_FAULT: "set_coil_sensor_fault_bs",
    CONF_PANEL_PRESENT: "set_panel_present_bs",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_VEVOR_DEHUMIDIFIER_ID])
    for key, setter in _SETTERS.items():
        if key in config:
            bs = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(parent, setter)(bs))
