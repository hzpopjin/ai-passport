#!/usr/bin/env python3
"""Persistent genuine v15 public-WASM bridge; does not inspect binary internals."""
import os
from pathlib import Path

os.execvp("node", ["node", str(Path(__file__).with_suffix(".mjs"))])
