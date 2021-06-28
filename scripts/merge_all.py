#!/usr/bin/env python3
"""Merges all the header files."""
#This is the actual license for this file.
lsc = '''/*
Copyright (c) 2014-2017, ipkn
              2020-2021, CrowCpp
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the author nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

'''
from glob import glob
from os import path as pt, name as osname
from os.path import sep
import re
from collections import defaultdict
import sys, getopt

if len(sys.argv) < 3:
    print("Usage: {} <CROW_HEADERS_DIRECTORY_PATH> <CROW_OUTPUT_HEADER_PATH> (-i(include) OR -e(exclude) item1,item2...)".format(sys.argv[0]))
    print("Available middlewares are in `include/crow/middlewares`. Do NOT type the `.h` when including or excluding")
    sys.exit(1)

header_path = sys.argv[1]
output_path = sys.argv[2]

middlewares = [x.rsplit(sep, 1)[-1][:-2] for x in glob(pt.join(header_path, ('crow'+sep+'middlewares'+sep+'*.h*')))]


middlewares_actual = []
if len(sys.argv) > 3:
    opts, args = getopt.getopt(sys.argv[3:],"i:e:",["include=","exclude="])
    if (len(opts) > 1):
        print("Error:Cannot simultaneously include and exclude middlewares.")
        sys.exit(1)
    if (opts[0][0] in ("-i", "--include")):
        middleware_list = opts[0][1].split(',')
        for item in middlewares:
            if (item in middleware_list):
                middlewares_actual.append(item)
    elif (opts[0][0] in ("-e", "--exclude")):
        middleware_list = opts[0][1].split(',')
        for item in middlewares:
            if (item not in middleware_list):
                middlewares_actual.append(item)
    else:
        print("ERROR:Unknown argument " + opts[0][0])
        sys.exit(1)
else:
    middlewares_actual = middlewares
print("Middlewares: " + str(middlewares_actual))

re_depends = re.compile('^#include "(.*)"', re.MULTILINE)
headers = [x.rsplit(sep, 1)[-1] for x in glob(pt.join(header_path, '*.h*'))]
headers += ['crow'+sep + x.rsplit(sep, 1)[-1] for x in glob(pt.join(header_path, 'crow'+sep+'*.h*'))]
headers += [('crow'+sep+'middlewares'+sep + x + '.h') for x in middlewares_actual]
print(headers)
edges = defaultdict(list)
for header in headers:
    d = open(pt.join(header_path, header)).read()
    match = re_depends.findall(d)
    for m in match:
        actual_m = m
        if (osname == 'nt'): #Windows
            actual_m = m.replace('/', '\\')
        # m should included before header
        edges[actual_m].append(header)

visited = defaultdict(bool)
order = []



def dfs(x):
    """Ensure all header files are visited."""
    visited[x] = True
    for y in edges[x]:
        if not visited[y]:
            dfs(y)
    order.append(x)

for header in headers:
    if not visited[header]:
        dfs(header)

order = order[::-1]
for x in edges:
    print(x, edges[x])
for x in edges:
    for y in edges[x]:
        assert order.index(x) < order.index(y), 'cyclic include detected'

print(order)
build = []
lsc_inc = False
for header in order:
    d = open(pt.join(header_path, header)).read()
    if d.startswith(lsc):
        if lsc_inc:
            print ("Found redundant license in \"" + pt.join(header_path, header) + "\".. Removing")
            d = d[(len(lsc)-1):]
    build.append(re_depends.sub(lambda x: '\n', d))
    build.append('\n')
    lsc_inc = True

open(output_path, 'w').write('\n'.join(build))
