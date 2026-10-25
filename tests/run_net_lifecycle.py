#!/usr/bin/env python3
"""Exercise real lobby exits and transport recreation without game assets."""
import sys
sys.dont_write_bytecode = True
from run_net_authority import main

if __name__ == "__main__":
    main("lifecycle")
