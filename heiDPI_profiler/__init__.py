import os
import confuse
import logging

from confuse.core import ConfigView


class App:
    __conf = None
    def __init__(self, path) -> None:
        source = confuse.YamlSource(path)
        App.__conf = confuse.RootView([source])

        logging.basicConfig(**App.config()["logging"].get())

    @staticmethod
    def config() -> ConfigView:
        return App.__conf
