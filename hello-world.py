#!/usr/bin/env python3

import os
import sys
from urllib.parse import parse_qs

vars = parse_qs(os.getenv("QUERY_STRING"))
sys.stdout.write("Hello %s %s!\n")
