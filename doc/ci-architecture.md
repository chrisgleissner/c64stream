# CI Build Architecture

This document describes the CI/CD pipeline structure for c64stream.

## Pipeline Overview

```mermaid
flowchart TD
    subgraph Trigger["🎯 Trigger"]
        PUSH[Push to Branch]
        PR[Pull Request]
        MANUAL[Manual Dispatch]
    end

    subgraph Validation["✅ Validation Stage"]
        CHECK[Check Event]
        FORMAT[Format Check<br/>clang-format, gersemi]
    end

    subgraph Infrastructure["🐳 Infrastructure Stage"]
        DOCKER{Docker Images<br/>Need Rebuild?}
        BUILD_IMAGES[Build Docker Images<br/>Ubuntu, Debian, Fedora, Arch]
    end

    subgraph Build["🔨 Build Stage"]
        LINUX[Linux Plugin<br/>Ubuntu Build]
        MAC[macOS Plugin]
        WINDOWS[Windows Plugin]
    end

    subgraph E2E["🧪 E2E Test Stage (Linux Only)"]
        subgraph BasicE2E["Basic E2E (Matrix - All Distros)"]
            E2E_UBUNTU_BASIC[Ubuntu 24.04]
            E2E_DEBIAN[Debian 12]
            E2E_FEDORA[Fedora 40]
            E2E_ARCH[Arch Linux]
        end
        subgraph FullE2E["Full E2E (Matrix - Ubuntu, All Scenarios)"]
            E2E_S1[Scenario 1]
            E2E_S2[Scenario 2]
            E2E_SN[Scenario N...]
        end
    end

    subgraph Release["📦 Release Stage"]
        ARTIFACTS[Collect Artifacts]
        RELEASE{Tagged<br/>Release?}
        PUBLISH[Publish Release]
    end

    %% Trigger flow
    PUSH --> CHECK
    PR --> CHECK
    MANUAL --> CHECK

    %% Validation flow
    CHECK --> FORMAT
    FORMAT --> DOCKER

    %% Infrastructure flow
    DOCKER -->|Yes| BUILD_IMAGES
    DOCKER -->|No| LINUX
    BUILD_IMAGES --> LINUX

    %% Build stage (parallel, independent of Docker for Mac/Windows)
    FORMAT --> MAC
    FORMAT --> WINDOWS

    %% E2E dependencies - all use the same Linux plugin artifact
    LINUX --> E2E_UBUNTU_BASIC
    LINUX --> E2E_DEBIAN
    LINUX --> E2E_FEDORA
    LINUX --> E2E_ARCH

    %% Full E2E runs after Basic E2E completes (all scenarios in parallel)
    E2E_UBUNTU_BASIC --> E2E_S1
    E2E_DEBIAN --> E2E_S1
    E2E_FEDORA --> E2E_S1
    E2E_ARCH --> E2E_S1
    E2E_UBUNTU_BASIC --> E2E_S2
    E2E_DEBIAN --> E2E_S2
    E2E_FEDORA --> E2E_S2
    E2E_ARCH --> E2E_S2
    E2E_UBUNTU_BASIC --> E2E_SN
    E2E_DEBIAN --> E2E_SN
    E2E_FEDORA --> E2E_SN
    E2E_ARCH --> E2E_SN

    %% Release flow
    E2E_S1 --> ARTIFACTS
    E2E_S2 --> ARTIFACTS
    E2E_SN --> ARTIFACTS
    MAC --> ARTIFACTS
    WINDOWS --> ARTIFACTS
    ARTIFACTS --> RELEASE
    RELEASE -->|Yes| PUBLISH
```

## Linux Pipeline Detail

```mermaid
flowchart LR
    subgraph Stage1["1️⃣ Infrastructure"]
        DOCKER[Build Docker Images<br/>if Dockerfiles changed]
    end

    subgraph Stage2["2️⃣ Build"]
        BUILD[Build Plugin<br/>Ubuntu Container]
    end

    subgraph Stage3["3️⃣ Basic E2E (Matrix)"]
        direction TB
        U[Ubuntu]
        D[Debian]
        F[Fedora]
        A[Arch]
    end

    subgraph Stage4["4️⃣ Full E2E (Matrix - Ubuntu)"]
        direction TB
        S1[Scenario 1]
        S2[Scenario 2]
        SN[Scenario N...]
    end

    DOCKER --> BUILD
    BUILD --> Stage3
    Stage3 --> Stage4
```

## Stage Descriptions

### 1. Validation Stage
- **Check Event**: Determine commit hash, branch, and whether to run subsequent stages
- **Format Check**: Validate code formatting (clang-format 21+, gersemi for CMake)

### 2. Infrastructure Stage
- **Docker Images**: Rebuild only if Dockerfiles changed
- Images cached in GitHub Container Registry (GHCR)

### 3. Build Stage
- **Single plugin per OS**: Linux (.so), macOS (.dylib), Windows (.dll)
- Linux plugin built on Ubuntu (works across all distros)

### 4. E2E Test Stage (Linux Only)
- **Basic E2E (Matrix)**: Runs on all 4 distros in parallel - validates OBS compatibility
- **Full E2E (Matrix)**: Runs all scenarios in parallel on Ubuntu after basic E2E passes
- Each scenario gets its own dedicated runner VM (no CPU contention)
- Tests include A/V sync validation with PulseAudio

### 5. Release Stage
- Collect artifacts from all platforms
- On tagged releases, publish to GitHub Releases

## Key Principles

1. **Fail Fast**: Format checks run first to catch simple issues early
2. **Build Once**: Single Linux plugin artifact used across all distros
3. **Test Broadly**: E2E on multiple distros catches OBS version differences
4. **Test Deeply**: Full scenarios only need to run once (Ubuntu)
5. **Caching**: Docker images cached for fast builds
