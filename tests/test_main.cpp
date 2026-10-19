// Test runner and implementation of the shared test helpers.

#include "test_framework.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace hdrcal::test {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> testCases;
    return testCases;
}

Registrar::Registrar(std::string name, std::function<void()> body) {
    registry().push_back(TestCase{.name = std::move(name), .body = std::move(body)});
}

void fail(const char* file, int line, const std::string& message) {
    throw TestFailure(std::string(file) + ":" + std::to_string(line) + ": " + message);
}

TemporaryDirectory::TemporaryDirectory(const std::string& prefix) {
    const std::filesystem::path baseDirectory = std::filesystem::temp_directory_path();
    const std::string processId = std::to_string(static_cast<long long>(::getpid()));

    for (int attempt = 0; attempt < 128; ++attempt) {
        const std::filesystem::path candidate = baseDirectory / (prefix + "-" + processId + "-" + std::to_string(attempt));
        std::error_code filesystemError;
        if (std::filesystem::create_directory(candidate, filesystemError) && !filesystemError) {
            m_path = candidate;
            return;
        }
    }

    throw std::runtime_error("não foi possível criar diretório temporário para o teste");
}

TemporaryDirectory::~TemporaryDirectory() {
    std::error_code filesystemError;
    std::filesystem::remove_all(m_path, filesystemError);
}

} // namespace hdrcal::test

int main() {
    int failedTests = 0;

    for (const auto& testCase : hdrcal::test::registry()) {
        try {
            testCase.body();
            std::cout << "[PASS] " << testCase.name << "\n";
        } catch (const std::exception& exception) {
            ++failedTests;
            std::cout << "[FAIL] " << testCase.name << ": " << exception.what() << "\n";
        }
    }

    std::cout << "\n" << hdrcal::test::registry().size() - static_cast<std::size_t>(failedTests) << " passaram, " << failedTests << " falharam\n";

    return failedTests == 0 ? 0 : 1;
}
