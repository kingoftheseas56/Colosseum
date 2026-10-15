"""Deterministic transport fixture. Never used by production or live probes."""
import json
import sys
import time

request=json.load(sys.stdin)
value=request["value"]
if value=="slow":
    time.sleep(2)
if request["operation"]=="resolve":
    result={"ok":False,"error":"Fixture unavailable"}
else:
    result={"ok":True,"data":[{"id":value,"title":value,"browseId":value}]}
print(json.dumps(result))
