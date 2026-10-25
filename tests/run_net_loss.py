#!/usr/bin/env python3
"""Deterministic loss/reordering delivery through the production packet receiver."""
import sys

sys.dont_write_bytecode = True
from run_net_receive import main

if __name__ == "__main__":
    main("loss")
