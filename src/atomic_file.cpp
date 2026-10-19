// Implements atomic file replacement with a temporary file, fsync and rename.

#include "hdrcal/atomic_file.hpp"

#include "hdrcal/unique_fd.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <unistd.h>

namespace hdrcal {

std::optional<std::string> writeFileAtomically(const std::filesystem::path& destinationPath, std::string_view content) {
    if (destinationPath.empty())
        return "o caminho de destino não pode ser vazio";

    std::error_code filesystemError;
    const std::filesystem::path parentDirectory = destinationPath.parent_path();
    if (!parentDirectory.empty()) {
        std::filesystem::create_directories(parentDirectory, filesystemError);
        if (filesystemError)
            return "não foi possível criar o diretório de destino: " + filesystemError.message();
    }

    const std::string temporaryPath = destinationPath.string() + ".tmp." + std::to_string(static_cast<long long>(::getpid()));
    UniqueFileDescriptor fileDescriptor(::open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
    if (fileDescriptor.get() < 0)
        return "não foi possível abrir o arquivo temporário: " + std::string(std::strerror(errno));

    std::size_t bytesWritten = 0;
    while (bytesWritten < content.size()) {
        const ssize_t written = ::write(fileDescriptor.get(), content.data() + bytesWritten, content.size() - bytesWritten);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            const std::string writeError = std::string(std::strerror(errno));
            fileDescriptor.reset();
            ::unlink(temporaryPath.c_str());
            return "não foi possível escrever o arquivo: " + writeError;
        }
        bytesWritten += static_cast<std::size_t>(written);
    }

    if (::fsync(fileDescriptor.get()) != 0) {
        const std::string syncError = std::string(std::strerror(errno));
        fileDescriptor.reset();
        ::unlink(temporaryPath.c_str());
        return "não foi possível sincronizar o arquivo com o disco: " + syncError;
    }
    fileDescriptor.reset();

    if (::rename(temporaryPath.c_str(), destinationPath.c_str()) != 0) {
        const std::string renameError = std::string(std::strerror(errno));
        ::unlink(temporaryPath.c_str());
        return "não foi possível substituir o arquivo de forma atômica: " + renameError;
    }

    return std::nullopt;
}

} // namespace hdrcal
