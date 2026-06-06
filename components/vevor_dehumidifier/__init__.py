"""VEVOR commercial dehumidifier (Modbus RTU display bus) custom component."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@jjsweitzer"]
DEPENDENCIES = ["uart"]
MULTI_CONF = False

vevor_dehumidifier_ns = cg.esphome_ns.namespace("vevor_dehumidifier")
VevorDehumidifier = vevor_dehumidifier_ns.class_(
    "VevorDehumidifier", cg.Component, uart.UARTDevice
)

CONF_VEVOR_DEHUMIDIFIER_ID = "vevor_dehumidifier_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(VevorDehumidifier),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
