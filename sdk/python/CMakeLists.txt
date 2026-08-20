add_test(
    NAME python-sdk-tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -v)
get_property(kearne_generated_python_root GLOBAL PROPERTY
    KEARNE_API_GENERATED_PYTHON_ROOT)
set_tests_properties(python-sdk-tests PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PYTHONPATH=path_list_prepend:${CMAKE_CURRENT_SOURCE_DIR}/src;PYTHONPATH=path_list_append:${kearne_generated_python_root}"
    TIMEOUT 90)

add_test(
    NAME python-sdk-ruff
    COMMAND ${Python3_EXECUTABLE} -m ruff check
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/tests
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
            ${PROJECT_SOURCE_DIR}/tools/bootstrap.py)
set_tests_properties(python-sdk-ruff PROPERTIES TIMEOUT 30)

add_test(
    NAME python-sdk-types
    COMMAND ${Python3_EXECUTABLE} -m mypy)
set_tests_properties(python-sdk-types PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    TIMEOUT 60)

add_test(
    NAME python-sdk-source-performance
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/source_edit.py
            --entities 100
            --samples 25
            --warmup 5
            --max-recognize-p95-ms 25
            --max-edit-p95-ms 20)
set_tests_properties(python-sdk-source-performance PROPERTIES
    LABELS performance
    TIMEOUT 30)

add_test(
    NAME python-sdk-source-scale
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/source_edit.py
            --entities 1000
            --samples 3
            --warmup 1
            --max-recognize-p95-ms 150
            --max-edit-p95-ms 220)
set_tests_properties(python-sdk-source-scale PROPERTIES
    LABELS performance
    TIMEOUT 30)

add_test(
    NAME python-sdk-source-session-performance
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/source_session.py
            --entities 100
            --batch-edits 8
            --samples 25
            --warmup 5
            --max-interactive-p95-ms 16
            --max-batch-p95-ms 16)
set_tests_properties(python-sdk-source-session-performance PROPERTIES
    LABELS performance
    TIMEOUT 30)

add_test(
    NAME python-sdk-source-session-scale
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/source_session.py
            --entities 1000
            --batch-edits 8
            --samples 5
            --warmup 1
            --max-interactive-p95-ms 160
            --max-batch-p95-ms 160)
set_tests_properties(python-sdk-source-session-scale PROPERTIES
    LABELS performance
    TIMEOUT 30)
