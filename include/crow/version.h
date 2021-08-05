#pragma once

namespace crow {

#ifdef CROW_MAIN
  char VERSION[] = "master";
#else
  extern char VERSION[];
#endif
}
