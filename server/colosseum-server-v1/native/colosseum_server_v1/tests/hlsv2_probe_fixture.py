#!/usr/bin/env python3
import os, sys
if sys.argv[-1] == "fixture:fail":
    sys.exit(7)
print(os.environ['HLSV2_FIXTURE_RAW'])
