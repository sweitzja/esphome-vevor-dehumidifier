"""Text sensor platform for the VEVOR dehumidifier component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import CONF_VEVOR_DEHUMIDIFIER_ID, VevorDehumidifier

DEPENDENCIES = ["vevor_dehumidifier"]

CONF_MODE = "mode"
CONF_ERROR_CODE = "error_code"
CONF_STATUS = "status"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_VEVOR_DEHUMIDIFIER_ID): cv.use_id(VevorDehumidifier),
        cv.Optional(CONF_MODE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_ERROR_CODE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
    }
)


_SETTERS = {
    CONF_MODE: "set_mode_text_sensor",
    CONF_ERROR_CODE: "set_error_code_text_sensor",
    CONF_STATUS: "set_status_text_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_VEVOR_DEHUMIDIFIER_ID])
    for key, setter in _SETTERS.items():
        if key in config:
            ts = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(parent, setter)(ts))
