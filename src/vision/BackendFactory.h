#pragma once

#include <memory>
#include <QString>

class IGenerationBackend;

class BackendFactory
{
public:
    static std::unique_ptr<IGenerationBackend> createBackend(const QString &backendName);
};
