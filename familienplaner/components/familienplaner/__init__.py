import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@dein-github-name"]
DEPENDENCIES = []
AUTO_LOAD = []

familienplaner_ns = cg.esphome_ns.namespace("familienplaner")
FamilienplanerPlugin = familienplaner_ns.class_("FamilienplanerPlugin", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FamilienplanerPlugin),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
