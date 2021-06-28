"""
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
"""

import urllib
assert "Hello World!" ==  urllib.urlopen('http://localhost:18080').read()
assert "About Crow example." ==  urllib.urlopen('http://localhost:18080/about').read()
assert 404 == urllib.urlopen('http://localhost:18080/list').getcode()
assert "3 bottles of beer!" == urllib.urlopen('http://localhost:18080/hello/3').read()
assert "100 bottles of beer!" == urllib.urlopen('http://localhost:18080/hello/100').read()
assert 400 == urllib.urlopen('http://localhost:18080/hello/500').getcode()
assert "3" == urllib.urlopen('http://localhost:18080/add_json', data='{"a":1,"b":2}').read()
assert "3" == urllib.urlopen('http://localhost:18080/add/1/2').read()

# test persistent connection
import socket
import time
s = socket.socket()
s.connect(('localhost', 18080))
for i in xrange(10):
    s.send('''GET / HTTP/1.1
Host: localhost\r\n\r\n''');
    assert 'Hello World!' in s.recv(1024)

# test large
s = socket.socket()
s.connect(('localhost', 18080))
s.send('''GET /large HTTP/1.1
Host: localhost\r\nConnection: close\r\n\r\n''')
r = ''
while True:
     d = s.recv(1024*1024)
     if not d:
         break;
     r += d
     print len(r), len(d)
print len(r), r[:100]
assert len(r) > 512*1024

# test timeout
s = socket.socket()
s.connect(('localhost', 18080))
# invalid request, connection will be closed after timeout
s.send('''GET / HTTP/1.1
hHhHHefhwjkefhklwejfklwejf
''')
print s.recv(1024)

