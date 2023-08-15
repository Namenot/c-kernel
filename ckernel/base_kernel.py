"""Implements BaseKernel"""

import asyncio
import logging
import os
from pathlib import Path
from typing import Coroutine

from ipykernel.ipkernel import IPythonKernel

from .util import STDERR, STDOUT, Stream, Trigger


class BaseKernel(IPythonKernel):
    """Common functionality for our kernels"""

    language = "c"
    language_version = "C11"
    language_info = {
        "name": "c",
        "codemirror_mode": "text/x-csrc",
    }

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.debug = os.getenv("CKERNEL_DEBUG") is not None

        if self.debug and self.log.hasHandlers():
            # set the handler for our logger to INFO. Don't use DEBUG, IPython
            # is too verbose to be useful!
            handler = self.log.handlers[0]
            current = str(handler)
            handler.setLevel(logging.INFO)
            self.log.info("update handler level: %s is now %s", current, handler)

    @property
    def cwd(self):
        return Path(os.getcwd())

    def log_info(self, msg: str, *args):
        """Log an info message with the class of self prepended"""
        _msg = f"[{self.__class__.__name__}] {msg}"
        self.log.info(_msg, *args)

    def log_error(self, msg: str, *args):
        """Log an info message with the class of self prepended"""
        _msg = f"[{self.__class__.__name__}] {msg}"
        self.log.error(_msg, *args)

    @property
    def banner(self):
        return "\n".join(
            [
                "A basic Jupyter kernel which provides C/C++ syntax highlighting",
                "and a little more magic",
                "",
                "Copyright (c) 2023, Adam Tuft",
                "",
                "github.com/adamtuft/c-kernel",
            ]
        )

    async def stream_data(
        self, dest: Stream, reader: asyncio.StreamReader, end: str = ""
    ) -> None:
        """Decode and stream data from reader to dest"""
        async for data in reader:
            self.print(data.decode(), dest=dest, end=end)

    async def gather_data(self, dest: list[str], reader: asyncio.StreamReader) -> None:
        """Gather data into a list of str"""
        async for data in reader:
            dest.append(data.decode())

    def stream_stdout(
        self, reader: asyncio.StreamReader
    ) -> Coroutine[None, None, None]:
        """Return a coroutine which streams data from reader to stdout"""
        return self.stream_data(STDOUT, reader, end="")

    def stream_stderr(
        self, reader: asyncio.StreamReader
    ) -> Coroutine[None, None, None]:
        """Return a coroutine which streams data from reader to stderr"""
        return self.stream_data(STDERR, reader, end="")

    def write_input(
        self, writer: asyncio.StreamWriter, trigger: Trigger, prompt: str = ""
    ):
        """Get input and send to writer. Wait for input request from trigger"""
        while True:
            self.log_info("waiting for input on %s", trigger)
            msg = trigger.wait()
            self.log_info("got message: %s", msg)
            data = (
                self.raw_input(prompt=prompt) + "\n"
            )  # add a newline because self.raw_input does not
            self.log_info("got data: %s", data.encode())
            writer.write(data.encode())

    def print(self, text: str, dest: Stream = STDOUT, end: str = "\n"):
        """Print to the kernel's stream dest"""
        self.send_response(
            self.iopub_socket, "stream", {"name": dest, "text": text + end}
        )

    def debug_msg(self, text: str):
        if self.debug:
            self.print(f"[DEBUG] {text}", dest=STDERR)
