#ifndef MECRAFT_RESOURCE_PACK_ARCHIVE_EXTRACTOR_H
#define MECRAFT_RESOURCE_PACK_ARCHIVE_EXTRACTOR_H

#include <filesystem>
#include <vector>

namespace resource {

[[nodiscard]] std::vector<std::filesystem::path> prepareResourcePackArchives(
    const std::filesystem::path& resourcePacksRoot);

} // namespace resource

#endif // MECRAFT_RESOURCE_PACK_ARCHIVE_EXTRACTOR_H
