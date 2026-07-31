#ifndef MECRAFT_ATMOSPHERE_LUT_PROBE_H
#define MECRAFT_ATMOSPHERE_LUT_PROBE_H

#include <cstddef>
#include <string>

namespace resource {

bool probeAtmosphereLut(const std::string& name, const std::string& path, size_t expectedBytes = 0);

} // namespace resource

#endif // MECRAFT_ATMOSPHERE_LUT_PROBE_H
