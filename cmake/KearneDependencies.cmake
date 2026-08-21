include_guard(GLOBAL)
include(FetchContent)

set(_KEARNE_DEPENDENCY_LOCK "${PROJECT_SOURCE_DIR}/dependencies.lock.json")
file(READ "${_KEARNE_DEPENDENCY_LOCK}" _kearne_dependency_json)

function(_kearne_dependency output dependency property)
    string(JSON value GET "${_kearne_dependency_json}"
           dependencies "${dependency}" "${property}")
    set("${output}" "${value}" PARENT_SCOPE)
endfunction()

function(kearne_require_protobuf)
    if(TARGET protobuf::libprotobuf AND TARGET Kearne::Protoc)
        return()
    endif()

    _kearne_dependency(absl_url abseil url)
    _kearne_dependency(absl_sha256 abseil sha256)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(absl SYSTEM EXCLUDE_FROM_ALL
        URL "${absl_url}"
        URL_HASH "SHA256=${absl_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(absl)

    _kearne_dependency(protobuf_url protobuf url)
    _kearne_dependency(protobuf_sha256 protobuf sha256)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_LIBPROTOC OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
    set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE BOOL "" FORCE)
    FetchContent_Declare(protobuf SYSTEM EXCLUDE_FROM_ALL
        URL "${protobuf_url}"
        URL_HASH "SHA256=${protobuf_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(protobuf)
    if(MSVC)
        target_compile_options(libprotobuf PRIVATE /wd4996)
    else()
        target_compile_options(libprotobuf PRIVATE
            -Wno-deprecated -Wno-deprecated-declarations)
    endif()

    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" host_system)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" host_processor)
    if(host_processor MATCHES "^(amd64|x86_64)$")
        set(host_processor x86_64)
    elseif(host_processor MATCHES "^(arm64|aarch64)$")
        set(host_processor aarch64)
    endif()
    set(host_key "${host_system}-${host_processor}")
    string(JSON protoc_url ERROR_VARIABLE protoc_error
           GET "${_kearne_dependency_json}"
           dependencies protobuf protoc "${host_key}" url)
    if(protoc_error)
        message(FATAL_ERROR "No pinned protoc for ${host_key}")
    endif()
    string(JSON protoc_sha256 GET "${_kearne_dependency_json}"
           dependencies protobuf protoc "${host_key}" sha256)
    FetchContent_Declare(kearne_protoc
        URL "${protoc_url}"
        URL_HASH "SHA256=${protoc_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(kearne_protoc)

    set(protoc_name "protoc")
    if(CMAKE_HOST_WIN32)
        string(APPEND protoc_name ".exe")
    endif()
    set(protoc_path "${kearne_protoc_SOURCE_DIR}/bin/${protoc_name}")
    if(NOT CMAKE_HOST_WIN32)
        file(CHMOD "${protoc_path}"
             PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                         GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
    add_executable(Kearne::Protoc IMPORTED GLOBAL)
    set_target_properties(Kearne::Protoc PROPERTIES
        IMPORTED_LOCATION "${protoc_path}")
    set(KEARNE_PROTOBUF_INCLUDE_DIR
        "${kearne_protoc_SOURCE_DIR}/include" CACHE INTERNAL "")
endfunction()

function(kearne_require_document_dependencies)
    if(TARGET Kearne::Blake3 AND TARGET Kearne::Immer)
        return()
    endif()

    _kearne_dependency(immer_url immer url)
    _kearne_dependency(immer_sha256 immer sha256)
    FetchContent_Declare(kearne_immer SYSTEM EXCLUDE_FROM_ALL
        URL "${immer_url}"
        URL_HASH "SHA256=${immer_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR _kearne_headers_only)
    FetchContent_MakeAvailable(kearne_immer)
    add_library(kearne_immer INTERFACE)
    add_library(Kearne::Immer ALIAS kearne_immer)
    target_include_directories(kearne_immer SYSTEM INTERFACE
        "${kearne_immer_SOURCE_DIR}")

    _kearne_dependency(blake3_url blake3 url)
    _kearne_dependency(blake3_sha256 blake3 sha256)
    FetchContent_Declare(kearne_blake3_source SYSTEM EXCLUDE_FROM_ALL
        URL "${blake3_url}"
        URL_HASH "SHA256=${blake3_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR _kearne_sources_only)
    FetchContent_MakeAvailable(kearne_blake3_source)
    add_library(kearne_blake3 STATIC
        "${kearne_blake3_source_SOURCE_DIR}/c/blake3.c"
        "${kearne_blake3_source_SOURCE_DIR}/c/blake3_dispatch.c"
        "${kearne_blake3_source_SOURCE_DIR}/c/blake3_portable.c")
    add_library(Kearne::Blake3 ALIAS kearne_blake3)
    target_include_directories(kearne_blake3 SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${kearne_blake3_source_SOURCE_DIR}/c>")
    target_compile_definitions(kearne_blake3 PRIVATE
        BLAKE3_USE_NEON=0 BLAKE3_NO_SSE2 BLAKE3_NO_SSE41
        BLAKE3_NO_AVX2 BLAKE3_NO_AVX512)
    set_target_properties(kearne_blake3 PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    install(TARGETS kearne_blake3 EXPORT KearneTargets)
endfunction()

function(kearne_require_sqlite)
    if(TARGET Kearne::SQLite)
        return()
    endif()

    _kearne_dependency(sqlite_url sqlite url)
    _kearne_dependency(sqlite_sha256 sqlite sha256)
    FetchContent_Declare(kearne_sqlite_source SYSTEM EXCLUDE_FROM_ALL
        URL "${sqlite_url}"
        URL_HASH "SHA256=${sqlite_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR _kearne_sources_only)
    FetchContent_MakeAvailable(kearne_sqlite_source)
    add_library(kearne_sqlite STATIC
        "${kearne_sqlite_source_SOURCE_DIR}/sqlite3.c")
    add_library(Kearne::SQLite ALIAS kearne_sqlite)
    target_include_directories(kearne_sqlite SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${kearne_sqlite_source_SOURCE_DIR}>")
    target_compile_definitions(kearne_sqlite PRIVATE
        SQLITE_DQS=0
        SQLITE_ENABLE_API_ARMOR
        SQLITE_OMIT_DEPRECATED
        SQLITE_THREADSAFE=1)
    set_target_properties(kearne_sqlite PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(NOT MSVC)
        target_link_libraries(kearne_sqlite PRIVATE ${CMAKE_DL_LIBS})
    endif()
    install(TARGETS kearne_sqlite EXPORT KearneTargets)
endfunction()

function(kearne_require_sketch_solver)
    if(TARGET Ceres::ceres AND TARGET Eigen3::Eigen)
        return()
    endif()

    _kearne_dependency(eigen_url eigen url)
    _kearne_dependency(eigen_sha256 eigen sha256)
    set(EIGEN_BUILD_BLAS OFF CACHE BOOL "" FORCE)
    set(EIGEN_BUILD_LAPACK OFF CACHE BOOL "" FORCE)
    set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(kearne_eigen SYSTEM EXCLUDE_FROM_ALL
        URL "${eigen_url}"
        URL_HASH "SHA256=${eigen_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR _kearne_headers_only)
    FetchContent_MakeAvailable(kearne_eigen)
    add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
    set_target_properties(Eigen3::Eigen PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${kearne_eigen_SOURCE_DIR}")
    set(Eigen3_DIR "${PROJECT_SOURCE_DIR}/cmake/eigen3"
        CACHE PATH "" FORCE)

    _kearne_dependency(ceres_url ceres url)
    _kearne_dependency(ceres_sha256 ceres sha256)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(MINIGLOG ON CACHE BOOL "" FORCE)
    set(MINIGLOG_MAX_LOG_LEVEL 0 CACHE STRING "" FORCE)
    set(GFLAGS OFF CACHE BOOL "" FORCE)
    set(USE_CUDA OFF CACHE BOOL "" FORCE)
    set(LAPACK OFF CACHE BOOL "" FORCE)
    set(SUITESPARSE OFF CACHE BOOL "" FORCE)
    set(EIGENSPARSE ON CACHE BOOL "" FORCE)
    set(SCHUR_SPECIALIZATIONS OFF CACHE BOOL "" FORCE)
    set(CUSTOM_BLAS OFF CACHE BOOL "" FORCE)
    set(PROVIDE_UNINSTALL_TARGET OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(kearne_ceres SYSTEM EXCLUDE_FROM_ALL
        URL "${ceres_url}"
        URL_HASH "SHA256=${ceres_sha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(kearne_ceres)
endfunction()
