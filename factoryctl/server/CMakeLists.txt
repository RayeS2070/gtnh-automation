add_executable(factoryctl_server
        src/main.cpp
        src/health.cpp
)
target_include_directories(factoryctl_server PRIVATE src)
target_link_libraries(factoryctl_server
        PRIVATE
        fctl_warnings
        userver::core
        userver::postgresql
)
