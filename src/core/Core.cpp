#include "Core.h"

#include <cstdlib>

namespace mc {

float Rand01() { return (rand() % 10000) / 10000.0f; }

} // namespace mc
