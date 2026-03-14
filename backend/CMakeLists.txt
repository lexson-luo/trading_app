add_executable(hf_server
    main.cpp
    database/connection.cpp
    database/repository.cpp
    core/statistics.cpp
    core/data_loader.cpp
    core/strategy.cpp
    core/backtester.cpp
    core/risk_engine.cpp
    core/live_trader.cpp
    brokers/mock_broker.cpp
    brokers/rest_broker.cpp
    api/auth.cpp
    api/data_api.cpp
    api/strategy_api.cpp
    api/backtest_api.cpp
    api/trading_api.cpp
    api/portfolio_api.cpp
    api/risk_api.cpp
)

target_include_directories(hf_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(hf_server PRIVATE
    hf_common
    httplib
    SQLiteCpp
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# Disable overkill warnings from Eigen headers
target_compile_options(hf_server PRIVATE
    -Wno-array-bounds
    -Wno-stringop-overread
)
