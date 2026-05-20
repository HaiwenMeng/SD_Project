#pragma once

#include "core/GenerateTypes.h"

#include <QString>

class ProjectStore;
struct ProjectData;

class IGenerationBackend
{
public:
    virtual ~IGenerationBackend() = default;

    virtual QString backendName() const = 0;
    virtual GenerateResult generate(const GenerateRequest &request,
                                    const ProjectData &projectData,
                                    const ProjectStore &projectStore) = 0;
};
