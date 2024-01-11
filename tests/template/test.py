#!/usr/bin/env python3
from __future__ import print_function
import glob
import json
import os
import subprocess

for testfile in glob.glob("*.json"):
    testdoc = json.load(open(testfile))
    for test in testdoc["tests"]:
        if "lambda" in test["data"]:
            continue

        open('data', 'w').write(json.dumps(test["data"]))
        open('template', 'w').write(test["template"])
        if "partials" in test:
            open('partials', 'w').write(json.dumps(test["partials"]))
        else:
            open('partials', 'w').write("{}")

        if os.name == 'nt':
            ret = subprocess.check_output("mustachetest.exe").decode('utf8')
        else:
            ret = subprocess.check_output('./mustachetest').decode('utf8')
        print(testfile, test["name"])

        if ret != test["expected"]:
            if 'partials' in test:
                print('Partials:', json.dumps(test["partials"]))
            print('Data: ', json.dumps(test["data"]))
            print('Template: ', test["template"])
            print('Expected:', repr(test["expected"]))
            print('Actual:', repr(ret))
        assert ret == test["expected"]

        os.unlink('data')
        os.unlink('template')
        os.unlink('partials')
