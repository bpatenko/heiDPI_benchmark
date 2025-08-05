"""Provides a utility to inject environment variables into argparse definitions.
Currently requires explicit naming of env vars to check for"""

import argparse
import os


class EnvDefault(argparse.Action):
    """An argparse action class that auto-sets missing default values from env
    vars. Defaults to requiring the argument."""

    def __init__(self, envvar, required=True, default=None, *args, **kwargs):
        if not default and envvar:
            if envvar in os.environ:
                default = os.environ[envvar]
        if required and default:
            required = False
        argparse.Action.__init__(self, default=default, required=required, *args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, values)

def env_default(envvar):
    def wrapper(*args, **kwargs):
        return EnvDefault(envvar, *args, **kwargs)
    return wrapper

