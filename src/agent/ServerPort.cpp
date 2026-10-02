#include "ServerPort.h"

namespace mm {

const char* ServerPort::rootName(Root r) {
  switch (r) {
    case Root::IsrgRootX1: return "ISRG Root X1";
    case Root::CoreBundle: return "the core's bundle";
    case Root::HostPlain: return "no chain (plain HTTP, host build)";
  }
  return "?";
}

}  // namespace mm
