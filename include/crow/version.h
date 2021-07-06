#pragma once

namespace crow {

#ifdef CROW_MAIN
  constexpr char VERSION[] = "master";
#else
  extern constexpr char VERSION[];
#endif
}
