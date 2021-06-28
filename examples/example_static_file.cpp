/*
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

//#define CROW_STATIC_DRIECTORY "alternative_directory/"
//#define CROW_STATIC_ENDPOINT "/alternative_endpoint/<path>"
#define CROW_MAIN
#include "crow.h"

int main()
{

	//Crow app initialization
	crow::SimpleApp app;

    //
CROW_ROUTE(app, "/")
([](const crow::request&, crow::response& res) {
    //replace cat.jpg with your file path
    res.set_static_file_info("cat.jpg");
    res.end();
});

	
	app.port(18080).run();
	
	
	return 0;
}

/// You can also use the `/static` directory and endpoint (the directory needs to have the same path as your executable).
/// Any file inside the static directory will have the same path it would in your filesystem.

/// TO change the static directory or endpoint, use the macros above (replace `alternative_directory` and/or `alternative_endpoint` with your own)
