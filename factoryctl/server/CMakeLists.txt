# Everything but main(): linked by the server and by the tests.
add_library(factoryctl_lib STATIC
        src/health.cpp
        src/agent/lua.cpp
        src/agent/protocol.cpp
        src/agent/scan_handler.cpp
        src/agent/poll_handler.cpp
        src/agent/pumps_protocol.cpp
        src/agent/pumps_poll_handler.cpp
        src/api/stock_handler.cpp
        src/api/json.cpp
        src/api/targets_handler.cpp
        src/api/orders_handler.cpp
        src/api/cpus_handler.cpp
        src/api/events_handler.cpp
        src/api/pumps_handler.cpp
        src/domain/plan.cpp
        src/domain/order.cpp
        src/domain/scan.cpp
        src/domain/poll.cpp
        src/domain/pumps.cpp
        src/db/sql.cpp
        src/db/schema.cpp
        src/db/scan.cpp
        src/db/orders.cpp
        src/db/stock.cpp
        src/db/targets.cpp
        src/db/manual.cpp
        src/db/cpus.cpp
        src/db/events.cpp
        src/db/events_cleanup.cpp
        src/db/pumps.cpp
        src/config/dynamic.cpp
        src/config/file_updater.cpp
        src/http/compression.cpp
)
# Compression of responses (http/compression.cpp): both libraries are userver's
# dependencies already.
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
target_include_directories(factoryctl_lib PUBLIC src)
target_link_libraries(factoryctl_lib
        PUBLIC
        userver::core
        userver::postgresql
        ZLIB::ZLIB
        PkgConfig::ZSTD
        PRIVATE
        fctl_warnings
)

# The schema goes into the binary: the server creates it in an empty database.
userver_embed_file(factoryctl_schema NAME schema_sql FILEPATH schema/schema.sql)
target_link_libraries(factoryctl_lib PUBLIC factoryctl_schema)

add_executable(factoryctl_server src/main.cpp)
target_link_libraries(factoryctl_server PRIVATE fctl_warnings factoryctl_lib)

add_executable(factoryctl_unittest
        tests/lua_test.cpp
        tests/protocol_test.cpp
        tests/poll_protocol_test.cpp
        tests/api_json_test.cpp
        tests/plan_test.cpp
        tests/order_test.cpp
        tests/scan_test.cpp
        tests/poll_test.cpp
        tests/sql_test.cpp
        tests/config_test.cpp
        tests/compression_test.cpp
        tests/pumps_test.cpp
)
target_link_libraries(factoryctl_unittest
        PRIVATE
        fctl_warnings
        factoryctl_lib
        userver::utest
)
add_google_tests(factoryctl_unittest)

# Database tests: need the PostgreSQL of compose.yaml (`docker compose up -d`).
add_executable(factoryctl_dbtest
        tests/db/schema_dbtest.cpp
        tests/db/scan_dbtest.cpp
        tests/db/orders_dbtest.cpp
        tests/db/stock_dbtest.cpp
        tests/db/targets_dbtest.cpp
        tests/db/manual_dbtest.cpp
        tests/db/settling_dbtest.cpp
        tests/db/probe_dbtest.cpp
        tests/db/craftable_dbtest.cpp
        tests/db/cpus_dbtest.cpp
        tests/db/events_dbtest.cpp
        tests/db/pumps_dbtest.cpp
)
target_link_libraries(factoryctl_dbtest
        PRIVATE
        fctl_warnings
        factoryctl_lib
        userver-postgresql-utest
)
target_compile_definitions(factoryctl_dbtest PRIVATE
        FACTORYCTL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
)
add_google_tests(factoryctl_dbtest)
set_tests_properties(factoryctl_dbtest PROPERTIES
        LABELS db
        ENVIRONMENT
        "POSTGRES_TEST_DSN=postgresql://factoryctl:factoryctl@localhost:15432/factoryctl_test"
)
